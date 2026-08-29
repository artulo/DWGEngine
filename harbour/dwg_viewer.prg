// dwg_viewer.prg
//
// ============================================================================
// TDwgViewer -- primer visor de ventana para DWGEngine, mismo patron
// Harbour+FiveWin que TPdfViewer (ver PDFEngine32\harbour\pdf_viewer.prg),
// pero arquitectura de render MAS SIMPLE: a diferencia del visor de PDF
// (que arma un HBITMAP compuesto de paginas y despues SCROLLEA dentro de
// el con TBitmap:nX/nY), aca no hay "paginas" -- es un plano 2D continuo,
// asi que cada cambio de zoom/pan simplemente vuelve a pedir un render
// FRESCO del tamanio exacto del viewport actual (DWG_RENDERTOHBITMAP,
// ver harbour\dwg_hbfunc.c), con la escala/origen nuevos. ::oBmp nunca
// scrollea en si mismo (lScroll:=.F.) -- "scrollear" es simplemente
// re-renderizar con otro origen.
//
// Convencion de camara (ver include\dwg_render.h): nScale = unidades-
// mundo por pixel; nOriginX/nOriginY = punto del mundo que cae en el
// pixel (0,0) del viewport (esquina superior-izquierda), con Y invertido
// (pixel_y crece hacia abajo, mundo Y crece hacia arriba).
//
// Interaccion (ver notas de alcance mas abajo):
//   - arrastre con boton DEL MEDIO = pan (2D) / orbita (3D) -- convencion
//     CAD estandar (AutoCAD/etc), reasignado desde boton izquierdo el
//     2026-08-26 (pedido de Arturo: "seleccion en pantalla tanto de
//     grupo como de elementos individuales") para dejar el izquierdo
//     libre, igual que cualquier visor CAD real
//   - boton IZQUIERDO, SOLO en modo 2D (ver alcance -- pick en 3D
//     necesitaria raycasting real contra la camara, fuera de esta
//     vuelta): click simple = seleccion por punto (Dwg_SelectPoint);
//     arrastre = ventana de seleccion de goma (Dwg_SelectWindow) --
//     izquierda->derecha = ventana (solo lo completamente adentro),
//     derecha->izquierda = cruce (cualquier cosa que toque el
//     rectangulo), misma convencion CAD estandar. SHIFT apretado al
//     bajar el boton agrega a la seleccion existente en vez de
//     reemplazarla.
//   - rueda del mouse = zoom, CENTRADO EN EL VIEWPORT (no en el cursor
//     -- zoom cursor-centrado necesitaria confirmar si nXPos/nYPos de
//     MouseWheel vienen en coordenadas de pantalla o de cliente, que
//     esta sesion no pudo verificar interactivamente, ver memoria
//     "No simulated input on shared desktop"; centrado-en-viewport es
//     un default razonable y sin ambiguedad mientras tanto)
//
// Creacion de entidades (2026-08-26, pedido de Arturo: "crear lineas
// circulo rectangulos y otros elementos"), SOLO modo 2D: un boton de la
// barra (dwg_demo.prg) entra en modo dibujo (::oViewer:cDrawMode) --
// mientras esta activo, el click IZQUIERDO deja de seleccionar y en
// cambio pone puntos (::DrawClick), con una previsualizacion en vivo
// (XOR, ver DrawXorPoly/TogglePreview) siguiendo el mouse. LINE/CIRCLE/
// RECT/ARC se completan solos al poner el ultimo punto necesario (y
// quedan listos para la proxima figura, mismo modo); POLYLINE sigue
// acumulando vertices hasta click DERECHO (termina abierta) o hasta
// clickear cerca del primer vertice (cierra). ESC o el boton
// "Seleccionar" cancela y vuelve al modo de selecion de siempre.
// ============================================================================

#include "FiveWin.ch"

#define DWGVIEW_ZOOM_STEP   1.25
#define DWGVIEW_FIT_MARGIN  1.05    // 5% de aire alrededor de los extents, mismo criterio "fit" que cualquier visor CAD
#define IDC_HAND  32649
#define IDC_ARROW 32512

// MK_SHIFT: mismo valor (0x0004, wParam de WM_*BUTTONDOWN) que scrlpanl.prg/
// xbrowse.prg de FWH2603 ya definen localmente -- no expuesto por FiveWin.ch.
#define MK_SHIFT 0x0004

// Umbral en pixels para distinguir click (seleccion por punto) de arrastre
// (ventana de seleccion) con el boton izquierdo -- por debajo, ruido de
// mano/mouse normal no debe disparar una ventana de seleccion accidental.
#define DWGVIEW_DRAG_THRESHOLD_PX 4

// Tolerancia de pick por click, en PIXELS (convertida a unidades-mundo
// via nScale al momento del click, ver LButtonUp) -- unos pocos pixels
// de margen alrededor del punto exacto del cursor, picking real de CAD
// jamas exige precision de subpixel.
#define DWGVIEW_PICK_TOLERANCE_PX 4

// Creacion de entidades por click (pedido de Arturo 2026-08-26, "crear
// lineas circulo rectangulos y otros elementos") -- ver TDwgViewer:
// DrawClick/GetDrawPreview mas abajo.

// VK_ESCAPE ya viene definido transitivamente por FiveWin.ch (confirmado:
// #define propio aca disparaba "redefinition" al compilar) -- NO
// redefinir, a diferencia de MK_SHIFT/IDC_HAND/R2_NOT que si hacen
// falta definir a mano.

// R2_NOT: modo de dibujo GDI "invertir el destino, ignorar la fuente"
// (ROP2, ver SetROP2 en MSDN) -- perfecto para el rectangulo de goma/
// preview de dibujo: pintar dos veces exactamente los mismos pixels
// deja todo como estaba (dibuja, despues borra), sin necesitar un color
// de pluma especial. No expuesto por ningun .ch de FWH2603, valor
// estandar Win32 estable, se define aca local mismo criterio que arriba.
#define R2_NOT 6

// Umbral en PIXELS para "click cerca del primer vertice de la
// polilinea en progreso" = cerrarla y terminarla (gesto CAD estandar,
// alternativa al click derecho que la termina abierta) -- mas generoso
// que DWGVIEW_PICK_TOLERANCE_PX porque es una decision de alto impacto
// (cierra la forma) y conviene que sea facil de disparar a proposito.
#define DWGVIEW_CLOSE_TOLERANCE_PX 8

// Cantidad de segmentos para aproximar CIRCLE/ARC en el preview XOR
// mientras se dibuja -- mas bajo que los 72 que usa el render real
// (dwg_render.c, ver segment_count_for_sweep) porque este se recalcula
// y se vuelve a dibujar en CADA movimiento del mouse; alcanza sobrado
// para que se vea como un circulo/arco mientras se esta dibujando.
#define DWGVIEW_PREVIEW_CIRCLE_SEGMENTS 32
#define DWGVIEW_PREVIEW_ARC_SEGMENTS    24

// Edicion por grips (pedido de Arturo 2026-08-26, "editar lineas
// existentes") -- ver TDwgViewer:HitTestGrip/GetGripPreview/
// CommitGripDrag mas abajo.

// Mitad del lado (en PIXELS) del cuadradito que marca cada grip --
// DwgDrawGrips dibuja un cuadrado de 2x este valor centrado en el punto.
#define DWGVIEW_GRIP_HALFSIZE 3

// Tolerancia de pick de un grip, en PIXELS -- igual criterio que
// DWGVIEW_PICK_TOLERANCE_PX (seleccion por punto), separado por si
// conviene ajustarlos distinto mas adelante (un grip es un blanco mas
// chico que toda la entidad, podria necesitar mas margen).
#define DWGVIEW_GRIP_PICK_TOLERANCE_PX 6

// nTipo (DWG_ENTITY_TYPE, dwg_types.h) de los tipos con grips en esta
// vuelta -- mismo alcance que el dialogo de Propiedades
// (dwg_props_dlg.prg tiene el mismo set de #define, duplicado aca
// porque son archivos .prg distintos, cada uno con su propio
// preprocesador).
#define DWGVIEW_T_POINT  1
#define DWGVIEW_T_LINE   2
#define DWGVIEW_T_CIRCLE 3
#define DWGVIEW_T_ARC    4
#define DWGVIEW_T_TEXT   7
//----------------------------------------------------------------------------//

// Doble-click manual (pedido de Arturo 2026-08-27, "editar textos in
// situ" -- reporte "no funciona el dobleclick" sobre el primer intento
// de esto). Causa real: TBitmap:Register() (bitmap.prg de FWH2603)
// registra la clase de ventana con nOR(CS_VREDRAW,CS_HREDRAW) SIN
// CS_DBLCLKS -- sin ese estilo, Windows JAMAS manda WM_LBUTTONDBLCLK a
// esta ventana, asi que METHOD LDblClick nunca se dispara solo (fix
// confirmado leyendo bitmap.prg + window.prg, donde CS_DBLCLKS si se
// agrega para ventanas top-level pero no para controles TBitmap). En
// vez de tocar la libreria de FiveWin, se detecta a mano en
// LButtonDown: dos clicks seguidos, cerca en tiempo y en pixeles,
// cuentan como doble-click. GetDoubleClickTime() (el valor real
// configurado en Windows) no esta expuesto por FWH2603 -- se usa el
// default de fabrica de Windows (500ms) en su lugar.
#define DWGVIEW_DBLCLICK_MS      500
#define DWGVIEW_DBLCLICK_TOL_PX  4
//----------------------------------------------------------------------------//

CLASS TDwgBitmap FROM TBitmap

   DATA oViewer                     // TDwgViewer dueño (para ::nScale/::nOriginX/::nOriginY/::pDoc)

   DATA lPanning         INIT .F.
   DATA nPanStartRow
   DATA nPanStartCol
   DATA nPanStartOriginX
   DATA nPanStartOriginY

   // Modo 3D (::oViewer:l3DMode): el mismo arrastre con boton izquierdo
   // que en 2D hace pan, en 3D orbita la camara (ver DWG_CAMERA3D en
   // dwg_render.h) -- azimuth/elevation ajustados directo por el delta
   // en pixels, sin pasar por nOriginX/nOriginY (que en 3D no se tocan,
   // el render 3D siempre centra la vista en el target).
   DATA lOrbiting            INIT .F.
   DATA nOrbitStartRow
   DATA nOrbitStartCol
   DATA nOrbitStartAzimuth
   DATA nOrbitStartElevation

   // Seleccion con boton IZQUIERDO, solo modo 2D (ver doc-comment de
   // arriba) -- lLeftDown arranca en LButtonDown y no se sabe todavia si
   // termina en click o arrastre; lLeftDragging se activa recien al
   // cruzar DWGVIEW_DRAG_THRESHOLD_PX (ver MouseMove), momento en el que
   // arranca a dibujarse el rectangulo de goma.
   DATA lLeftDown        INIT .F.
   DATA lLeftDragging    INIT .F.
   DATA nLeftStartRow
   DATA nLeftStartCol
   DATA nLeftLastRow             // ultimo rectangulo de goma dibujado (para borrarlo, DrawFocusRect es XOR) -- NIL mientras no se dibujo ninguno todavia
   DATA nLeftLastCol
   DATA lLeftShift       INIT .F.        // SHIFT apretado al bajar el boton -- agrega a la seleccion en vez de reemplazarla

   // Deteccion manual de doble-click -- ver doc-comment de
   // DWGVIEW_DBLCLICK_MS mas arriba. nLastClickTick en 0 = "no hay
   // click previo reciente para comparar".
   DATA nLastClickTick   INIT 0
   DATA nLastClickRow
   DATA nLastClickCol

   // Preview XOR de creacion de entidades (::oViewer:cDrawMode != NIL,
   // ver doc-comment de arriba) -- aXorPreview guarda los ULTIMOS puntos
   // pixel dibujados (o NIL si no hay ninguno activo); volver a
   // dibujarlos exactamente iguales (mismo XOR que DrawRubberBand ya
   // usa) los borra, ver TogglePreview.
   DATA aXorPreview        INIT NIL
   DATA lXorPreviewClosed  INIT .F.

   METHOD LButtonDown( nRow, nCol, nKeyFlags )
   METHOD LButtonUp( nRow, nCol, nKeyFlags )
   METHOD LDblClick( nRow, nCol, nKeyFlags )             // doble-click en un TEXT existente -- edicion in situ (pedido de Arturo 2026-08-27), ver dwg_text_dlg.prg
   METHOD RButtonUp( nRow, nCol, nKeyFlags )             // termina una polilinea en progreso (abierta) -- ver doc-comment de arriba
   METHOD KeyDown( nKey, nFlags )                        // ESC cancela un dibujo en progreso -- atajo best-effort, mismo patron que TPdfBitmap:KeyDown en pdf_viewer.prg
   METHOD MButtonDown( nRow, nCol, nKeyFlags )
   METHOD MButtonUp( nRow, nCol, nKeyFlags )
   METHOD MouseMove( nRow, nCol, nKeyFlags )
   METHOD MouseWheel( nKeys, nDelta, nXPos, nYPos )
   METHOD DrawRubberBand( nRow1, nCol1, nRow2, nCol2 )   // dibuja/borra (XOR) el rectangulo de goma de seleccion -- llamar dos veces con el mismo rectangulo lo borra
   METHOD DrawXorPoly( aPixelPoints, lClosed )           // dibuja/borra (XOR) una cadena de segmentos -- llamar dos veces con los mismos puntos la borra
   METHOD TogglePreview( aPixelPoints, lClosed )         // borra el preview anterior (si habia) y dibuja el nuevo (si aPixelPoints != NIL) -- despachador central para todo el dibujo por click
   METHOD WorldPtsToPixel( aWorldPts )                   // convierte un array de {x,y} de MUNDO a {row,col} de pixel -- inversa de la formula que LButtonUp ya usa

ENDCLASS

//----------------------------------------------------------------------------//

// Pan (2D) / orbita (3D) -- boton DEL MEDIO desde el 2026-08-26 (ver
// doc-comment de arriba), mismo mecanismo que TPdfBitmap:MButtonDown/Up
// en pdf_viewer.prg. Logica interna sin cambios respecto de cuando esto
// vivia en LButtonDown/Up -- solo el boton que lo dispara.
METHOD MButtonDown( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   if ::oViewer != NIL .and. ::oViewer:pDoc != NIL
      if ::oViewer:l3DMode
         ::lOrbiting           := .T.
         ::nOrbitStartRow      := nRow
         ::nOrbitStartCol      := nCol
         ::nOrbitStartAzimuth  := ::oViewer:nCamAzimuth
         ::nOrbitStartElevation:= ::oViewer:nCamElevation
      else
         ::lPanning         := .T.
         ::nPanStartRow     := nRow
         ::nPanStartCol     := nCol
         ::nPanStartOriginX := ::oViewer:nOriginX
         ::nPanStartOriginY := ::oViewer:nOriginY
      endif
      ::Capture()
      SetCursor( LoadCursor( 0, IDC_HAND ) )
   endif

return ::Super:MButtonDown( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD MButtonUp( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   if ::lPanning .or. ::lOrbiting
      ::lPanning  := .F.
      ::lOrbiting := .F.
      ReleaseCapture()
      SetCursor( LoadCursor( 0, IDC_ARROW ) )
   endif

return ::Super:MButtonUp( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD DrawRubberBand( nRow1, nCol1, nRow2, nCol2 ) CLASS TDwgBitmap

   local hDC := GetDC( ::hWnd )

   DrawFocusRect( hDC, Min( nRow1, nRow2 ), Min( nCol1, nCol2 ), Max( nRow1, nRow2 ), Max( nCol1, nCol2 ) )
   ReleaseDC( ::hWnd, hDC )

return nil

//----------------------------------------------------------------------------//

// Preview de creacion de entidades (::oViewer:cDrawMode, ver doc-comment
// de arriba) -- una cadena de segmentos XOR (SetROP2 R2_NOT, ver el
// #define arriba) a traves de aPixelPoints ({row,col} cada uno), cerrada
// de vuelta al primer punto si lClosed. Volver a llamar con EXACTAMENTE
// los mismos puntos/lClosed la borra -- mismo truco que DrawRubberBand
// (DrawFocusRect) ya usa para la ventana de seleccion, generalizado a
// cualquier forma (linea/circulo/arco/rectangulo/polilinea) en vez de
// solo rectangulos, porque a diferencia de DrawFocusRect (que ya viene
// hecho para eso) GDI no tiene un unico primitivo que sirva para las 5.
METHOD DrawXorPoly( aPixelPoints, lClosed ) CLASS TDwgBitmap

   local hDC, nOldROP, i, n

   n := Len( aPixelPoints )
   if n < 2
      return nil
   endif

   hDC     := GetDC( ::hWnd )
   nOldROP := SetROP2( hDC, R2_NOT )

   MoveToEx( hDC, aPixelPoints[ 1 ][ 2 ], aPixelPoints[ 1 ][ 1 ] )   // {row,col} -> GDI x=col,y=row
   for i := 2 to n
      LineTo( hDC, aPixelPoints[ i ][ 2 ], aPixelPoints[ i ][ 1 ] )
   next
   if lClosed
      LineTo( hDC, aPixelPoints[ 1 ][ 2 ], aPixelPoints[ 1 ][ 1 ] )
   endif

   SetROP2( hDC, nOldROP )
   ReleaseDC( ::hWnd, hDC )

return nil

//----------------------------------------------------------------------------//

// Despachador central del preview: borra el anterior (si habia uno --
// ::aXorPreview guarda los puntos EXACTOS con los que se dibujo, para
// poder repetir el mismo trazo y anularlo), dibuja el nuevo (si
// aPixelPoints != NIL) y lo recuerda para la proxima vez. Llamar con
// aPixelPoints==NIL borra sin dibujar nada nuevo (cancelar/terminar).
METHOD TogglePreview( aPixelPoints, lClosed ) CLASS TDwgBitmap

   if ::aXorPreview != NIL
      ::DrawXorPoly( ::aXorPreview, ::lXorPreviewClosed )
   endif

   if aPixelPoints != NIL
      ::DrawXorPoly( aPixelPoints, lClosed )
   endif

   ::aXorPreview       := aPixelPoints
   ::lXorPreviewClosed := lClosed

return nil

//----------------------------------------------------------------------------//

// Inversa de la conversion mundo->pixel que LButtonUp ya usa (ver ahi
// mismo el comentario con la formula original) -- aWorldPts es un array
// de {x,y} de mundo (el formato que TDwgViewer:GetDrawPreview devuelve),
// el resultado un array de {row,col} de pixel (el formato que
// DrawXorPoly/TogglePreview esperan).
METHOD WorldPtsToPixel( aWorldPts ) CLASS TDwgBitmap

   local aOut := {}
   local i

   for i := 1 to Len( aWorldPts )
      AAdd( aOut, { ( ::oViewer:nOriginY - aWorldPts[ i ][ 2 ] ) / ::oViewer:nScale, ;
                    ( aWorldPts[ i ][ 1 ] - ::oViewer:nOriginX ) / ::oViewer:nScale } )
   next

return aOut

//----------------------------------------------------------------------------//

// Seleccion con boton izquierdo -- SOLO modo 2D (ver doc-comment de
// arriba, y el comentario de alcance del plan: pick en 3D necesita
// raycasting real contra la camara, fuera de esta vuelta). En 3D, este
// boton queda libre/sin usar (Dwg_SelectLayer via el dialogo de Capas
// sigue disponible en cualquier modo).
METHOD LButtonDown( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   local wx, wy, aHit
   local lIsDblClick := .F.

   // Doble-click manual -- ver doc-comment de DWGVIEW_DBLCLICK_MS mas
   // arriba (CS_DBLCLKS no esta en esta clase de ventana, WM_LBUTTON-
   // DBLCLK/METHOD LDblClick nunca llegan solos). Se compara ESTE
   // LButtonDown contra el ULTIMO que paso por aca -- si cae cerca en
   // tiempo y en pixeles, es el segundo click de un par. Se resetea el
   // rastro despues de reconocer un par para no encadenar un tercer
   // click cercano como otro doble-click.
   if ::nLastClickTick != 0 .and. ;
      ( GetTickCount() - ::nLastClickTick ) <= DWGVIEW_DBLCLICK_MS .and. ;
      Abs( nRow - ::nLastClickRow ) <= DWGVIEW_DBLCLICK_TOL_PX .and. ;
      Abs( nCol - ::nLastClickCol ) <= DWGVIEW_DBLCLICK_TOL_PX
      lIsDblClick      := .T.
      ::nLastClickTick := 0
   else
      ::nLastClickTick := GetTickCount()
      ::nLastClickRow  := nRow
      ::nLastClickCol  := nCol
   endif

   // LDblClick devuelve .T. solo si REALMENTE encontro un TEXT y abrio
   // el editor -- si no (nada clickeable ahi, o no hay texto), esta
   // sigue siendo una entrada de mouse valida y cae al comportamiento
   // normal de mas abajo (dibujo/seleccion/grips), en vez de perderse:
   // dos clicks rapidos colocando puntos de dibujo (p.ej. una LINEA
   // corta) no deben verse tragados solo por caer cerca en tiempo y
   // pixeles.
   if lIsDblClick .and. ::LDblClick( nRow, nCol, nKeyFlags )
      return 0
   endif

   if ::oViewer != NIL .and. ::oViewer:pDoc != NIL .and. !::oViewer:l3DMode
      if ::oViewer:cDrawMode != NIL
         // modo dibujo (ver doc-comment de arriba) -- cada click pone un
         // punto en vez de seleccionar. Se borra cualquier preview activo
         // ANTES de llamar DrawClick: si DrawClick completa una entidad
         // llama ::Render() (repinta todo, ya tapa el preview solo), pero
         // si solo acumula un punto mas (sin Render todavia) hace falta
         // borrarlo a mano para no dejar el XOR anterior pegado.
         wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
         wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale
         ::TogglePreview( NIL )
         ::oViewer:DrawClick( wx, wy )
      else
         // Edicion por grips (ver doc-comment de arriba) -- si el click
         // cae sobre un grip de una entidad YA SELECCIONADA, arranca un
         // arrastre de grip en vez de la seleccion de siempre.
         wx   := ::oViewer:nOriginX + nCol * ::oViewer:nScale
         wy   := ::oViewer:nOriginY - nRow * ::oViewer:nScale
         aHit := ::oViewer:HitTestGrip( wx, wy, DWGVIEW_GRIP_PICK_TOLERANCE_PX * ::oViewer:nScale )

         if aHit != NIL
            ::oViewer:StartGripDrag( aHit[ 1 ], aHit[ 2 ], aHit[ 3 ] )
            ::Capture()
         else
            ::lLeftDown     := .T.
            ::lLeftDragging := .F.
            ::nLeftStartRow := nRow
            ::nLeftStartCol := nCol
            ::nLeftLastRow  := NIL
            ::nLeftLastCol  := NIL
            ::lLeftShift    := lAnd( nKeyFlags, MK_SHIFT )
            ::Capture()
         endif
      endif
   endif

return ::Super:LButtonDown( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD LButtonUp( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   local wx, wy, wx2, wy2, lCrossing

   if ::oViewer != NIL .and. ::oViewer:pGripEntity != NIL
      wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
      wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale
      ::TogglePreview( NIL )
      ::oViewer:CommitGripDrag( wx, wy )
      ReleaseCapture()
      return ::Super:LButtonUp( nRow, nCol, nKeyFlags )
   endif

   if ::lLeftDown .and. ::oViewer != NIL
      if ::lLeftDragging
         // borra el ultimo rectangulo de goma dibujado (XOR -- redibujarlo
         // con el mismo par de esquinas lo borra) antes de actuar
         if ::nLeftLastRow != NIL
            ::DrawRubberBand( ::nLeftStartRow, ::nLeftStartCol, ::nLeftLastRow, ::nLeftLastCol )
         endif

         // pixel -> mundo, inversa de world_to_pixel (ver dwg_render.h):
         // world_x = originX + pixel_x*scale, world_y = originY - pixel_y*scale
         wx  := ::oViewer:nOriginX + ::nLeftStartCol * ::oViewer:nScale
         wy  := ::oViewer:nOriginY - ::nLeftStartRow * ::oViewer:nScale
         wx2 := ::oViewer:nOriginX + nCol * ::oViewer:nScale
         wy2 := ::oViewer:nOriginY - nRow * ::oViewer:nScale

         // izquierda->derecha = ventana (exacta), derecha->izquierda =
         // cruce (aproximada) -- convencion CAD estandar
         lCrossing := ( nCol < ::nLeftStartCol )

         if !::lLeftShift
            Dwg_SelClear( ::oViewer:pDoc )
         endif
         ::oViewer:SelectWindow( wx, wy, wx2, wy2, lCrossing )
      else
         wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
         wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale

         if !::lLeftShift
            Dwg_SelClear( ::oViewer:pDoc )
         endif
         ::oViewer:SelectPoint( wx, wy, DWGVIEW_PICK_TOLERANCE_PX * ::oViewer:nScale )
      endif

      ::lLeftDown     := .F.
      ::lLeftDragging := .F.
      ReleaseCapture()
   endif

return ::Super:LButtonUp( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

// Edicion in situ de texto (pedido de Arturo 2026-08-27, "editar textos
// in situ") -- doble-click sobre un TEXT ya existente reabre
// dwg_text_dlg.prg precargado con su contenido/fuente/tamaño actuales
// en vez de los valores de "Texto nuevo" (ver DwgTextDlg en
// dwg_text_dlg.prg). Devuelve .T. si REALMENTE encontro un TEXT ahi y
// abrio el editor, .F. si no (nada clickeable, u otro tipo de entidad)
// -- LButtonDown usa ese valor para decidir si el click se "consume"
// aca o cae al comportamiento normal (ver doc-comment de ahi).
//
// A PROPOSITO no exige ::oViewer:cDrawMode == NIL (bug real reportado
// por Arturo: "no se ve nada... si creo un texto no se puede editar" --
// DrawClick deja el modo de dibujo activo despues de crear una entidad,
// listo para la proxima sin volver a apretar el boton (ver doc-comment
// de TDwgViewer:DrawClick), asi que recien creado un texto NUNCA se
// esta en modo Seleccionar todavia; exigirlo hubiera dejado la edicion
// in situ inalcanzable justo despues de crear el texto que se quiere
// editar). Si hay un modo de dibujo en curso y el doble-click SI cae
// sobre un TEXT, se cancela ese modo primero (mismo TogglePreview(NIL)+
// CancelDraw que ya usa el atajo ESC en KeyDown) antes de abrir el
// editor -- doble-click sobre una entidad real siempre gana.
//
// IMPORTANTE: este metodo NUNCA lo dispara Windows solo -- se llama a
// mano desde LButtonDown, que detecta el doble-click el mismo (ver
// doc-comment de DWGVIEW_DBLCLICK_MS mas arriba: la clase de ventana de
// TBitmap no tiene CS_DBLCLKS, asi que WM_LBUTTONDBLCLK jamas llega
// aca). Queda como METHOD de todas formas (en vez de una funcion
// suelta) por si algun dia se agrega CS_DBLCLKS de verdad -- ese
// mensaje, si empezara a llegar, caeria en este mismo METHOD sin
// cambiar nada mas.
METHOD LDblClick( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   local wx, wy, pEnt, aProps
   local lHandled := .F.

   if ::oViewer != NIL .and. ::oViewer:pDoc != NIL .and. !::oViewer:l3DMode

      wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
      wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale

      Dwg_SelClear( ::oViewer:pDoc )
      ::oViewer:SelectPoint( wx, wy, DWGVIEW_PICK_TOLERANCE_PX * ::oViewer:nScale )

      if Dwg_SelCount( ::oViewer:pDoc ) == 1
         pEnt   := Dwg_SelGet( ::oViewer:pDoc, 1 )
         aProps := Dwg_EntityGetProps( ::oViewer:pDoc, pEnt )

         if aProps != NIL .and. aProps[ 1 ] == DWGVIEW_T_TEXT
            if ::oViewer:cDrawMode != NIL
               ::TogglePreview( NIL )
               ::oViewer:CancelDraw()
            endif
            ::oViewer:Render()
            DwgTextDlg( ::oViewer, wx, wy, pEnt )
            lHandled := .T.
         endif
      endif

      // Sin match: en modo Seleccionar (cDrawMode==NIL) se repinta para
      // limpiar cualquier resaltado que SelectPoint haya dejado a medio
      // camino (mismo comportamiento de siempre). En modo dibujo NO se
      // repinta -- ::Render() destruiria el preview XOR de la figura en
      // curso (aDrawPoints sigue intacto, DrawClick de mas abajo en
      // LButtonDown lo sigue necesitando).
      if !lHandled .and. ::oViewer:cDrawMode == NIL
         ::oViewer:Render()
      endif
   endif

   ::Super:LDblClick( nRow, nCol, nKeyFlags )

return lHandled

//----------------------------------------------------------------------------//

// Termina una POLILINEA en progreso como ABIERTA (ver doc-comment de
// arriba -- la alternativa, cerrarla, es clickear cerca del primer
// vertice, ver TDwgViewer:DrawClick). Sin efecto fuera de modo
// POLYLINE o sin puntos puestos todavia -- pasa al comportamiento por
// defecto (nunca sobreescrito antes de esto).
METHOD RButtonUp( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   if ::oViewer != NIL .and. ::oViewer:cDrawMode == "POLYLINE" .and. Len( ::oViewer:aDrawPoints ) > 0
      ::TogglePreview( NIL )
      ::oViewer:FinishPolyline( .F. )
      return 0
   endif

return ::Super:RButtonUp( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

// ESC cancela un dibujo en progreso -- atajo de respaldo aca (solo
// funciona si ESTE control tiene el foco de teclado, mismo criterio
// que ya usaba TPdfBitmap:KeyDown en pdf_viewer.prg), la via PRINCIPAL
// desde 2026-08-27 es el ACCELERATOR ACC_NORMAL, VK_ESCAPE del menu
// "Seleccionar" en dwg_demo.prg (funciona sin importar el foco -- ver
// doc-comment de TDwgViewer:EscapeAction, la logica real vive ahi para
// no duplicarla entre las dos vias).
METHOD KeyDown( nKey, nFlags ) CLASS TDwgBitmap

   if nKey == VK_ESCAPE .and. ::oViewer != NIL .and. ;
      ( ::oViewer:cDrawMode != NIL .or. ::oViewer:pGripEntity != NIL )
      ::oViewer:EscapeAction()
      return 0
   endif

return ::Super:KeyDown( nKey, nFlags )

//----------------------------------------------------------------------------//

METHOD MouseMove( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   // Orbit 3D: PI/500 radianes por pixel de arrastre (~0.36 grados/px --
   // una vuelta completa de horizonte necesita ~1750px de arrastre,
   // sensibilidad razonable de mouse sin verificacion interactiva
   // posible, ver memoria "sin input simulado en escritorio compartido"
   // -- ajustable a mano si se siente muy rapido/lento una vez probado).
   // Elevation clampeado a (-PI/2, PI/2) para no invertir la camara al
   // pasar por el cenit/nadir.
   local nSens := 3.14159265358979 / 500.0
   local wx, wy, aPreview

   if ::lOrbiting .and. ::oViewer != NIL
      ::oViewer:nCamAzimuth   := ::nOrbitStartAzimuth - ( nCol - ::nOrbitStartCol ) * nSens
      ::oViewer:nCamElevation := ::nOrbitStartElevation + ( nRow - ::nOrbitStartRow ) * nSens
      if ::oViewer:nCamElevation > 1.55
         ::oViewer:nCamElevation := 1.55
      elseif ::oViewer:nCamElevation < -1.55
         ::oViewer:nCamElevation := -1.55
      endif
      ::oViewer:Render()
      SetCursor( LoadCursor( 0, IDC_HAND ) )
      return 0
   endif

   // mismo mecanismo que TPdfBitmap:MouseMove (arrastre con boton
   // central) en pdf_viewer.prg -- delta en PIXELS desde donde se
   // apreto el boton, convertido a unidades-mundo via ::nScale. El
   // signo esta derivado (no copiado) de pixel_x=(world_x-originX)/
   // scale, pixel_y=(originY-world_y)/scale (ver dwg_render.h): para
   // que el punto del mundo bajo el cursor se mantenga bajo el cursor
   // mientras se arrastra, originX debe RESTAR el delta de columna y
   // originY debe SUMAR el delta de fila.
   if ::lPanning .and. ::oViewer != NIL
      ::oViewer:nOriginX := ::nPanStartOriginX - ( nCol - ::nPanStartCol ) * ::oViewer:nScale
      ::oViewer:nOriginY := ::nPanStartOriginY + ( nRow - ::nPanStartRow ) * ::oViewer:nScale
      ::oViewer:Render()
      SetCursor( LoadCursor( 0, IDC_HAND ) )
      return 0
   endif

   // Ventana de seleccion con boton izquierdo (ver LButtonDown/Up) --
   // recien se activa lLeftDragging (y arranca a dibujarse el rectangulo
   // de goma) al cruzar el umbral de arrastre, para no confundir un
   // click con temblor de mano con una ventana de seleccion accidental.
   if ::lLeftDown .and. ::oViewer != NIL
      if !::lLeftDragging .and. ;
         ( Abs( nCol - ::nLeftStartCol ) > DWGVIEW_DRAG_THRESHOLD_PX .or. ;
           Abs( nRow - ::nLeftStartRow ) > DWGVIEW_DRAG_THRESHOLD_PX )
         ::lLeftDragging := .T.
      endif

      if ::lLeftDragging
         if ::nLeftLastRow != NIL
            ::DrawRubberBand( ::nLeftStartRow, ::nLeftStartCol, ::nLeftLastRow, ::nLeftLastCol )   // borra el anterior (XOR)
         endif
         ::DrawRubberBand( ::nLeftStartRow, ::nLeftStartCol, nRow, nCol )                          // dibuja el nuevo
         ::nLeftLastRow := nRow
         ::nLeftLastCol := nCol
      endif

      return 0
   endif

   // Preview de arrastre de grip (ver doc-comment de arriba) --
   // ::oViewer:pGripEntity != NIL mientras dura el arrastre.
   if ::oViewer != NIL .and. ::oViewer:pGripEntity != NIL
      wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
      wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale

      aPreview := ::oViewer:GetGripPreview( wx, wy )
      if aPreview != NIL
         ::TogglePreview( ::WorldPtsToPixel( aPreview[ 1 ] ), aPreview[ 2 ] )
      endif

      return 0
   endif

   // Preview de creacion de entidades (ver doc-comment de arriba) --
   // solo tiene sentido una vez que ya hay al menos 1 punto puesto
   // (::aDrawPoints); antes de eso no hay nada que previsualizar todavia.
   if ::oViewer != NIL .and. ::oViewer:cDrawMode != NIL .and. !::oViewer:l3DMode .and. ;
      Len( ::oViewer:aDrawPoints ) > 0

      wx := ::oViewer:nOriginX + nCol * ::oViewer:nScale
      wy := ::oViewer:nOriginY - nRow * ::oViewer:nScale

      aPreview := ::oViewer:GetDrawPreview( wx, wy )
      if aPreview != NIL
         ::TogglePreview( ::WorldPtsToPixel( aPreview[ 1 ] ), aPreview[ 2 ] )
      endif

      return 0
   endif

return ::Super:MouseMove( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD MouseWheel( nKeys, nDelta, nXPos, nYPos ) CLASS TDwgBitmap

   local aRect, nCenterRow, nCenterCol
   local wx, wy, nNewScale

   ( nKeys )
   ( nXPos )
   ( nYPos )

   if ::oViewer == NIL .or. ::oViewer:pDoc == NIL
      return 0
   endif

   if nDelta > 0
      nNewScale := ::oViewer:nScale / DWGVIEW_ZOOM_STEP    // rueda hacia adelante = acercar = menos unidades-mundo por pixel
   else
      nNewScale := ::oViewer:nScale * DWGVIEW_ZOOM_STEP
   endif
   if nNewScale < 0.000000001                                     // guarda contra underflow si se gira la rueda muchas veces seguidas
      nNewScale := 0.000000001
   endif

   // Modo 3D: el render 3D siempre centra la vista en nCamTarget (ver
   // Dwg_RenderToHBitmap3D en dwg_hbfunc.c) -- alcanza con cambiar la
   // escala, sin la matematica de recentrado por nOriginX/nOriginY que
   // solo tiene sentido en la proyeccion plana 2D.
   if ::oViewer:l3DMode
      ::oViewer:nScale := nNewScale
      ::oViewer:Render()
      return 0
   endif

   aRect      := GetClientRect( ::hWnd )
   nCenterRow := ( aRect[ 3 ] - aRect[ 1 ] ) / 2
   nCenterCol := ( aRect[ 4 ] - aRect[ 2 ] ) / 2

   // punto del mundo que hoy cae en el centro del viewport -- se
   // mantiene fijo ahi despues del zoom (si no, cada zoom "corre" el
   // dibujo hacia una esquina).
   wx := ::oViewer:nOriginX + nCenterCol * ::oViewer:nScale
   wy := ::oViewer:nOriginY - nCenterRow * ::oViewer:nScale

   ::oViewer:nScale := nNewScale

   ::oViewer:nOriginX := wx - nCenterCol * ::oViewer:nScale
   ::oViewer:nOriginY := wy + nCenterRow * ::oViewer:nScale

   ::oViewer:Render()

return 0

//----------------------------------------------------------------------------//

CLASS TDwgViewer

   DATA oWnd
   DATA oBmp
   DATA pDoc
   DATA cFile

   DATA nScale        INIT 1.0
   DATA nOriginX      INIT 0.0
   DATA nOriginY      INIT 0.0

   // Navegacion 3D real (ver DWG_CAMERA3D en dwg_render.h): l3DMode
   // alterna entre el render plano de siempre (Dwg_RenderToHBitmap) y
   // la camara orbital (Dwg_RenderToHBitmap3D) -- misma ::nScale para
   // ambos modos (unidades-mundo por pixel), pero SIN usar nOriginX/
   // nOriginY en 3D (el render 3D siempre centra la vista en
   // nCamTarget). azimuth/elevation en RADIANES.
   DATA l3DMode          INIT .F.
   DATA nCamAzimuth      INIT 0.7853981634   // 45 grados -- vista de 3/4 razonable al entrar en 3D por primera vez
   DATA nCamElevation    INIT 0.4636476090   // ~26.57 grados (isometrico clasico atan(1/sqrt(2)))
   DATA nCamTargetX      INIT 0.0
   DATA nCamTargetY      INIT 0.0
   DATA nCamTargetZ      INIT 0.0

   DATA nDispTop      INIT 0
   DATA nDispLeft     INIT 0
   DATA nDispWidth    INIT 0
   DATA nDispHeight   INIT 0

   DATA hBitmap       INIT 0        // HBITMAP actualmente asignado a ::oBmp (para liberarlo antes de reemplazarlo)

   METHOD New( oWnd, nTop, nLeft, nWidth, nHeight ) CONSTRUCTOR

   METHOD Open( cFile )             // abre (o reemplaza) el dibujo mostrado. .T./.F.
   METHOD Close()
   METHOD End() INLINE ::Close()

   // Nuevo/Guardar (pedido de Arturo 2026-08-26) -- ver doc-comment del
   // inicio de dwg_hbfunc.c para el porque de la logica de formato en
   // WriteTo (R2000 con plantilla si se puede, si no R12; DXF si el
   // usuario elige esa extension).
   METHOD NewDocument()                // cierra lo que haya y arranca un documento vacio. .T./.F.
   METHOD Save()                       // sobreescribe ::cFile (o redirige a SaveAs si todavia no hay archivo). .T./.F.
   METHOD SaveAs()                     // pide un archivo nuevo (dialogo "Guardar como") y escribe ahi. .T./.F.
   METHOD WriteTo( cPath )             // escribe ::pDoc en cPath, formato segun la extension. .T./.F.

   METHOD Resize( nWidth, nHeight )

   METHOD ZoomFit()                 // recalcula nScale/nOriginX/nOriginY contra DWG_GETEXTENTS y renderiza
   METHOD ZoomIn()  INLINE ::ZoomAroundCenter( 1 / DWGVIEW_ZOOM_STEP )
   METHOD ZoomOut() INLINE ::ZoomAroundCenter( DWGVIEW_ZOOM_STEP )
   METHOD ZoomAroundCenter( nFactor )

   METHOD Toggle3D()                // alterna l3DMode; al activar, recalcula camara contra DWG_GETEXTENTS3D (::Fit3D()) y renderiza
   METHOD Fit3D()                   // centra nCamTarget/nScale en los extents 3D del dibujo

   // Seleccion/edicion/capas/propiedades (pedido de Arturo 2026-08-26) --
   // wrappers finos sobre harbour\dwg_hbfunc.c, cada uno re-renderiza al
   // final para que el resaltado/cambio se vea de inmediato (ver
   // dwg_render.c: dwg_document_sel_contains fuerza un color fijo).
   METHOD SelectPoint( nWx, nWy, nTolWorld )         // -> nAgregados
   METHOD SelectWindow( nWx1, nWy1, nWx2, nWy2, lCrossing )  // -> nAgregados
   METHOD ClearSelection()
   METHOD Move( nDx, nDy, nDz )
   METHOD Rotate( nCx, nCy, nCz, nAngleDeg )
   METHOD Scale( nCx, nCy, nCz, nFactor )
   METHOD Mirror( nX1, nY1, nX2, nY2 )
   METHOD Erase()                                    // -> nBorrados
   METHOD Copy()                                     // -> nCopiados
   METHOD Explode()                                  // -> nCreados
   METHOD Join( nTolerance )                         // -> nCreados

   // Creacion de entidades por click (pedido de Arturo 2026-08-26, "crear
   // lineas circulo rectangulos y otros elementos") -- cDrawMode NIL =
   // modo seleccion de siempre; "LINE"/"CIRCLE"/"RECT"/"ARC"/"POLYLINE" =
   // modo dibujo activo. aDrawPoints son los puntos de MUNDO {x,y} ya
   // clickeados para la entidad en progreso (ver TDwgBitmap:LButtonDown/
   // RButtonUp mas arriba, que son quienes llaman DrawClick/
   // FinishPolyline en cada click).
   DATA cDrawMode        INIT NIL
   DATA aDrawPoints      INIT {}

   METHOD StartDraw( cModo )                          // entra en modo dibujo
   METHOD CancelDraw()                                // vuelve a modo seleccion (ESC o boton "Seleccionar")
   METHOD DrawClick( nWx, nWy )                        // acumula un punto; completa la entidad sola cuando ya hay los necesarios
   METHOD FinishPolyline( lCerrar )                    // termina la POLYLINE en progreso (click derecho=abierta, o cerca del primer vertice=cerrada)
   METHOD FinishEntity( pEntidad )                      // comun a los 4 casos de arriba: capa "0" + auto-seleccion + limpia aDrawPoints + Render()
   METHOD GetDrawPreview( nWxCursor, nWyCursor )        // -> { aPuntosMundo, lCerrado } o NIL -- ver TDwgBitmap:MouseMove

   // Edicion por grips (pedido de Arturo 2026-08-26, "editar lineas
   // existentes") -- agarrar y arrastrar un punto de control de una
   // entidad YA SELECCIONADA (distinto del dialogo de Propiedades, que
   // sigue andando igual para editar a mano). Alcance: POINT/LINE/
   // CIRCLE/ARC/TEXT, los mismos tipos que ya soporta Propiedades.
   // pGripEntity/nGripIndex/aGripProps NIL = no hay arrastre de grip
   // activo; aGripProps es la foto completa de Dwg_EntityGetProps al
   // empezar el arrastre, para poder reescribir los campos NO tocados
   // sin perderlos al confirmar (ver CommitGripDrag).
   DATA pGripEntity      INIT NIL
   DATA nGripIndex       INIT 0
   DATA aGripProps       INIT NIL

   METHOD HitTestGrip( nWx, nWy, nTolWorld )            // -> { pEntidad, nIndiceGrip, aProps } del primer grip dentro de tolerancia, o NIL
   METHOD StartGripDrag( pEntidad, nIndiceGrip, aProps )
   METHOD CancelGripDrag()
   METHOD GetGripPreview( nWxCursor, nWyCursor )        // -> { aPuntosMundo, lCerrado } o NIL, mismo formato que GetDrawPreview
   METHOD CommitGripDrag( nWxCursor, nWyCursor )        // aplica el arrastre (Dwg_EntitySetProps) y renderiza

   METHOD EscapeAction()                                // ESC/"Seleccionar" -- cancela dibujo o arrastre de grip en curso, EN CUALQUIER PARTE (ver doc-comment de mas abajo)

   METHOD Render()                  // pide un DWG_RENDERTOHBITMAP(3D) fresco al tamanio actual del viewport y lo asigna a ::oBmp

ENDCLASS

//----------------------------------------------------------------------------//

METHOD New( oWnd, nTop, nLeft, nWidth, nHeight ) CLASS TDwgViewer

   DEFAULT nTop := 0, nLeft := 0, nWidth := 100, nHeight := 100

   ::oWnd        := oWnd
   ::nDispTop    := nTop
   ::nDispLeft   := nLeft
   ::nDispWidth  := nWidth
   ::nDispHeight := nHeight

   // mismo fondo que ya calcula dwg_render.c (DWG_RENDER_BG_COLOR,
   // gris azulado oscuro (RGB 33,40,48 -- valor exacto que pidio
   // Arturo) para el area DENTRO del dibujo -- ponemos el mismo tono
   // en el brush de la ventana para que el margen FUERA del bitmap
   // (si el control no llena toda la ventana) combine.
   if oWnd != NIL
      oWnd:SetBrush( TBrush():New( , RGB( 33, 40, 48 ) ) )
   endif

   ::oBmp := TDwgBitmap():New( nTop, nLeft, nWidth, nHeight, ;
                             NIL, NIL, ;     // cResName, cBmpFile: nada que cargar de archivo/recurso -- el bitmap se asigna a mano en ::Render()
                             .T., ;          // lNoBorder
                             oWnd, ;
                             NIL, NIL, ;     // bLClicked, bRClicked
                             .F., .F., ;     // lScroll: .F. -- no hay scroll propio, cada pan/zoom re-renderiza (ver comentario de arriba); lStretch: .F.
                             NIL, NIL, ;     // oCursor, cMsg
                             NIL, NIL, ;     // lUpdate, bWhen
                             .T. )           // lPixel
   ::oBmp:oViewer := Self

   if oWnd != NIL
      oWnd:bMouseWheel := {| nKeys, nDelta, nXPos, nYPos | ::oBmp:MouseWheel( nKeys, nDelta, nXPos, nYPos ) }
   endif

return Self

//----------------------------------------------------------------------------//

METHOD Open( cFile ) CLASS TDwgViewer

   local pNewDoc

   if ::pDoc != NIL
      ::Close()
   endif

   pNewDoc := Dwg_Open( cFile )
   if pNewDoc == NIL
      return .F.
   endif

   ::pDoc  := pNewDoc
   ::cFile := cFile

return ::ZoomFit()

//----------------------------------------------------------------------------//

METHOD Close() CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_Close( ::pDoc )
      ::pDoc := NIL
   endif

   if ::hBitmap != NIL .and. ::hBitmap != 0
      DeleteObject( ::hBitmap )
      ::hBitmap := 0
      if ::oBmp != NIL
         ::oBmp:hBitmap := 0
      endif
   endif

return nil

//----------------------------------------------------------------------------//

METHOD NewDocument() CLASS TDwgViewer

   local pNewDoc

   if ::pDoc != NIL
      ::Close()
   endif

   pNewDoc := Dwg_New()
   if pNewDoc == NIL
      return .F.
   endif

   ::pDoc  := pNewDoc
   ::cFile := NIL

return ::ZoomFit()

//----------------------------------------------------------------------------//

METHOD Save() CLASS TDwgViewer

   if Empty( ::cFile )
      return ::SaveAs()
   endif

return ::WriteTo( ::cFile )

//----------------------------------------------------------------------------//

METHOD SaveAs() CLASS TDwgViewer

   local cPath

   if ::pDoc == NIL
      return .F.
   endif

   cPath := cGetFile( "Archivos DWG|*.dwg|Archivos DXF|*.dxf|Todos|*.*", ;
                       "Guardar dibujo como", NIL, NIL, .T. )
   if Empty( cPath )
      return .F.
   endif

   if !::WriteTo( cPath )
      return .F.
   endif

   ::cFile := cPath

return .T.

//----------------------------------------------------------------------------//

// Elige el escritor segun la EXTENSION de cPath -- ".dxf" siempre desde
// cero; cualquier otra (".dwg" por default) intenta primero R2000 CON
// PLANTILLA (::cFile, si sigue existiendo en disco -- el mejor formato,
// pero dwg_write_dwg_r2000 devuelve error si ese archivo no es
// realmente R2000, sin que haga falta revisar la firma a mano aca) y
// si eso falla cae a R12 (sin plantilla, siempre funciona). Cuando
// cPath es EL MISMO ::cFile (Guardar sobreescribiendo el original), la
// escritura R2000 va primero a un temporal y recien se reemplaza el
// original si salio bien -- para no arriesgarlo si algo falla a mitad
// de camino.
METHOD WriteTo( cPath ) CLASS TDwgViewer

   local cExt, cTmp, lOk

   if ::pDoc == NIL .or. Empty( cPath )
      return .F.
   endif

   cExt := Lower( SubStr( cPath, RAt( ".", cPath ) ) )

   if cExt == ".dxf"
      return Dwg_WriteDxf( ::pDoc, cPath )
   endif

   lOk := .F.

   if !Empty( ::cFile ) .and. File( ::cFile )
      if ::cFile == cPath
         cTmp := cPath + ".tmp"
         if Dwg_WriteDwgR2000( ::pDoc, ::cFile, cTmp )
            FErase( cPath )
            FRename( cTmp, cPath )
            lOk := .T.
         else
            FErase( cTmp )
         endif
      else
         lOk := Dwg_WriteDwgR2000( ::pDoc, ::cFile, cPath )
      endif
   endif

   if !lOk
      lOk := Dwg_WriteDwgR12( ::pDoc, cPath )
   endif

return lOk

//----------------------------------------------------------------------------//

METHOD Resize( nWidth, nHeight ) CLASS TDwgViewer

   ::nDispWidth  := nWidth
   ::nDispHeight := nHeight

   if ::oBmp != NIL
      ::oBmp:Move( ::nDispTop, ::nDispLeft, ::nDispWidth, ::nDispHeight, .F. )
   endif

   if ::pDoc != NIL
      ::Render()          // el viewport cambio de tamanio -- volver a pedir el render al nuevo ancho/alto (misma escala/origen, no es un re-fit)
   endif

   // mismo motivo que el InvalidateRect/UpdateWindow ya agregado dentro
   // de ::Render() -- este cubre especificamente el area de la VENTANA
   // (fuera del control oBmp, por si el margen que combina con el
   // fondo del render tambien queda con contenido viejo tras agrandar).
   if ::oWnd != NIL
      InvalidateRect( ::oWnd:hWnd, 0, .T. )
      UpdateWindow( ::oWnd:hWnd )
   endif

return nil

//----------------------------------------------------------------------------//

METHOD ZoomAroundCenter( nFactor ) CLASS TDwgViewer

   local aRect, nCenterRow, nCenterCol, wx, wy

   if ::pDoc == NIL .or. ::oBmp == NIL
      return nil
   endif

   aRect      := GetClientRect( ::oBmp:hWnd )
   nCenterRow := ( aRect[ 3 ] - aRect[ 1 ] ) / 2
   nCenterCol := ( aRect[ 4 ] - aRect[ 2 ] ) / 2

   wx := ::nOriginX + nCenterCol * ::nScale
   wy := ::nOriginY - nCenterRow * ::nScale

   ::nScale := ::nScale * nFactor
   if ::nScale < 0.000000001
      ::nScale := 0.000000001
   endif

   ::nOriginX := wx - nCenterCol * ::nScale
   ::nOriginY := wy + nCenterRow * ::nScale

return ::Render()

//----------------------------------------------------------------------------//

METHOD ZoomFit() CLASS TDwgViewer

   local aExt, wx0, wy0, wx1, wy1, ww, wh
   local aRect, pw, ph, sx, sy

   if ::pDoc == NIL .or. ::oBmp == NIL
      return .F.
   endif

   aExt := Dwg_GetExtents( ::pDoc )

   aRect := GetClientRect( ::oBmp:hWnd )
   pw    := aRect[ 4 ] - aRect[ 2 ]
   ph    := aRect[ 3 ] - aRect[ 1 ]
   if pw < 1 ; pw := 1 ; endif
   if ph < 1 ; ph := 1 ; endif

   if aExt == NIL
      // dibujo sin entidades medibles (ver dwg_render_get_extents) --
      // arranca en una vista de 1 unidad-mundo == 1 pixel centrada en
      // el origen, mejor que fallar del todo.
      ::nScale   := 1.0
      ::nOriginX := -pw / 2
      ::nOriginY := ph / 2
      return ::Render()
   endif

   wx0 := aExt[ 1 ]
   wy0 := aExt[ 2 ]
   wx1 := aExt[ 3 ]
   wy1 := aExt[ 4 ]

   ww := wx1 - wx0
   wh := wy1 - wy0
   if ww <= 0 ; ww := 1 ; endif
   if wh <= 0 ; wh := 1 ; endif

   sx := ww / pw
   sy := wh / ph
   ::nScale := Max( sx, sy ) * DWGVIEW_FIT_MARGIN

   // centra los extents en el viewport
   ::nOriginX := wx0 - ( ( pw * ::nScale ) - ww ) / 2
   ::nOriginY := wy1 + ( ( ph * ::nScale ) - wh ) / 2

return ::Render()

//----------------------------------------------------------------------------//

METHOD Toggle3D() CLASS TDwgViewer

   ::l3DMode := !::l3DMode

   if ::l3DMode
      ::Fit3D()
   else
      ::ZoomFit()
   endif

return ::l3DMode

//----------------------------------------------------------------------------//

METHOD Fit3D() CLASS TDwgViewer

   local aExt, wx0, wy0, wz0, wx1, wy1, wz1
   local aRect, pw, ph, ww, wh, wd, wDiag

   if ::pDoc == NIL
      return nil
   endif

   aExt := Dwg_GetExtents3D( ::pDoc )

   aRect := IIF( ::oBmp != NIL, GetClientRect( ::oBmp:hWnd ), { 0, 0, 100, 100 } )
   pw    := aRect[ 4 ] - aRect[ 2 ]
   ph    := aRect[ 3 ] - aRect[ 1 ]
   if pw < 1 ; pw := 1 ; endif
   if ph < 1 ; ph := 1 ; endif

   if aExt == NIL
      // sin entidades medibles -- misma salida razonable que ZoomFit
      // para este caso: target en el origen, escala 1:1.
      ::nCamTargetX := 0.0
      ::nCamTargetY := 0.0
      ::nCamTargetZ := 0.0
      ::nScale      := 1.0
      return ::Render()
   endif

   wx0 := aExt[ 1 ] ; wy0 := aExt[ 2 ] ; wz0 := aExt[ 3 ]
   wx1 := aExt[ 4 ] ; wy1 := aExt[ 5 ] ; wz1 := aExt[ 6 ]

   ::nCamTargetX := ( wx0 + wx1 ) / 2.0
   ::nCamTargetY := ( wy0 + wy1 ) / 2.0
   ::nCamTargetZ := ( wz0 + wz1 ) / 2.0

   // la diagonal del bounding box es una cota segura del tamanio real
   // proyectado sea cual sea el angulo de camara (la proyeccion nunca
   // puede ser mas grande que la caja que la contiene) -- mas simple y
   // robusto que proyectar los 8 vertices con la camara actual para un
   // fit exacto, a costa de dejar algo de aire de mas alrededor.
   ww := wx1 - wx0 ; wh := wy1 - wy0 ; wd := wz1 - wz0
   wDiag := Sqrt( ww * ww + wh * wh + wd * wd )
   if wDiag <= 0 ; wDiag := 1 ; endif

   ::nScale := ( wDiag * DWGVIEW_FIT_MARGIN ) / Min( pw, ph )

return ::Render()

//----------------------------------------------------------------------------//

METHOD Render() CLASS TDwgViewer

   local aRect, nW, nH, aResult
   local hOldBitmap := ::hBitmap

   if ::pDoc == NIL .or. ::oBmp == NIL
      return .F.
   endif

   aRect := GetClientRect( ::oBmp:hWnd )
   nW    := aRect[ 4 ] - aRect[ 2 ]
   nH    := aRect[ 3 ] - aRect[ 1 ]
   if nW < 1 .or. nH < 1
      return .F.
   endif

   if ::l3DMode
      aResult := Dwg_RenderToHBitmap3D( ::pDoc, nW, nH, ::nScale, ;
                                        ::nCamAzimuth, ::nCamElevation, ;
                                        ::nCamTargetX, ::nCamTargetY, ::nCamTargetZ )
   else
      aResult := Dwg_RenderToHBitmap( ::pDoc, nW, nH, ::nScale, ::nOriginX, ::nOriginY )
   endif
   if aResult == NIL
      return .F.
   endif

   // Grips de la seleccion actual (ver doc-comment de arriba) --
   // horneados DENTRO del bitmap recien generado, no un overlay aparte
   // en pantalla, para que sobrevivan resize/minimizar/superposicion de
   // ventanas sin logica extra (mismo bitmap que ::oBmp ya administra).
   // Solo 2D, mismo alcance que seleccion/dibujo.
   if !::l3DMode .and. Dwg_SelCount( ::pDoc ) > 0
      DwgDrawGrips( ::pDoc, aResult[ 1 ], ::nOriginX, ::nOriginY, ::nScale )
   endif

   ::hBitmap      := aResult[ 1 ]
   ::oBmp:hBitmap := ::hBitmap

   // BUG REAL ENCONTRADO (Arturo: captura mostrando la ventana partida
   // en dos -- mitad con el render nuevo/fondo negro correcto, mitad
   // con contenido viejo/fondo mas claro sin actualizar) -- ::Refresh()
   // de TBitmap a veces no invalida el area de cliente COMPLETA
   // (especialmente al agrandar la ventana, cuando el control crece y
   // el area recien expuesta nunca recibe un WM_PAINT real). Forzar
   // InvalidateRect con bErase=.T. + UpdateWindow sincronico garantiza
   // que TODO el rectangulo se vuelva a pintar con el bitmap nuevo,
   // sin depender de que TBitmap calcule bien el area sucia.
   ::oBmp:Refresh()
   InvalidateRect( ::oBmp:hWnd, 0, .T. )
   UpdateWindow( ::oBmp:hWnd )

   if hOldBitmap != NIL .and. hOldBitmap != 0 .and. hOldBitmap != ::hBitmap
      DeleteObject( hOldBitmap )   // DWG_RENDERTOHBITMAP entrega un HBITMAP propio en cada llamada (ver dwg_hbfunc.c), igual que PDF_RENDERTOHBITMAP
   endif

return .T.

//----------------------------------------------------------------------------//

METHOD SelectPoint( nWx, nWy, nTolWorld ) CLASS TDwgViewer

   local nAdded := 0

   if ::pDoc != NIL
      nAdded := Dwg_SelectPoint( ::pDoc, nWx, nWy, nTolWorld )
      ::Render()
   endif

return nAdded

//----------------------------------------------------------------------------//

METHOD SelectWindow( nWx1, nWy1, nWx2, nWy2, lCrossing ) CLASS TDwgViewer

   local nAdded := 0

   if ::pDoc != NIL
      nAdded := Dwg_SelectWindow( ::pDoc, nWx1, nWy1, nWx2, nWy2, lCrossing )
      ::Render()
   endif

return nAdded

//----------------------------------------------------------------------------//

METHOD ClearSelection() CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_SelClear( ::pDoc )
      ::Render()
   endif

return nil

//----------------------------------------------------------------------------//

METHOD Move( nDx, nDy, nDz ) CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_SelMove( ::pDoc, nDx, nDy, nDz )
      ::Render()
   endif

return nil

//----------------------------------------------------------------------------//

METHOD Rotate( nCx, nCy, nCz, nAngleDeg ) CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_SelRotate( ::pDoc, nCx, nCy, nCz, nAngleDeg )
      ::Render()
   endif

return nil

//----------------------------------------------------------------------------//

METHOD Scale( nCx, nCy, nCz, nFactor ) CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_SelScale( ::pDoc, nCx, nCy, nCz, nFactor )
      ::Render()
   endif

return nil

//----------------------------------------------------------------------------//

METHOD Mirror( nX1, nY1, nX2, nY2 ) CLASS TDwgViewer

   if ::pDoc != NIL
      Dwg_SelMirror( ::pDoc, nX1, nY1, nX2, nY2 )
      ::Render()
   endif

return nil

//----------------------------------------------------------------------------//

METHOD Erase() CLASS TDwgViewer

   local n := 0

   if ::pDoc != NIL
      n := Dwg_SelErase( ::pDoc )
      ::Render()
   endif

return n

//----------------------------------------------------------------------------//

METHOD Copy() CLASS TDwgViewer

   local n := 0

   if ::pDoc != NIL
      n := Dwg_SelCopy( ::pDoc )
      ::Render()
   endif

return n

//----------------------------------------------------------------------------//

METHOD Explode() CLASS TDwgViewer

   local n := 0

   if ::pDoc != NIL
      n := Dwg_SelExplode( ::pDoc )
      ::Render()
   endif

return n

//----------------------------------------------------------------------------//

METHOD Join( nTolerance ) CLASS TDwgViewer

   local n := 0

   if ::pDoc != NIL
      n := Dwg_SelJoin( ::pDoc, nTolerance )
      ::Render()
   endif

return n

//----------------------------------------------------------------------------//

METHOD StartDraw( cModo ) CLASS TDwgViewer

   ::cDrawMode   := cModo
   ::aDrawPoints := {}

return nil

//----------------------------------------------------------------------------//

METHOD CancelDraw() CLASS TDwgViewer

   ::cDrawMode   := NIL
   ::aDrawPoints := {}

return nil

//----------------------------------------------------------------------------//

// Cada rama pone su punto en ::aDrawPoints hasta juntar los que necesita
// esa entidad y recien ahi crea la real (Dwg_Add*) -- se queda en el
// MISMO ::cDrawMode despues (::FinishEntity limpia ::aDrawPoints, no
// ::cDrawMode), listo para la proxima figura sin volver a apretar el
// boton, igual que cualquier comando LINE/CIRCLE real de un CAD.
METHOD DrawClick( nWx, nWy ) CLASS TDwgViewer

   local pEnt, aCorners, i
   local cx, cy, radio, angIni, angFin
   local dx, dy

   if ::pDoc == NIL .or. ::cDrawMode == NIL
      return nil
   endif

   do case
   case ::cDrawMode == "LINE"
      if Len( ::aDrawPoints ) == 0
         AAdd( ::aDrawPoints, { nWx, nWy } )
      else
         pEnt := Dwg_AddLine( ::pDoc, ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ], 0, nWx, nWy, 0 )
         ::FinishEntity( pEnt )
      endif

   case ::cDrawMode == "CIRCLE"
      if Len( ::aDrawPoints ) == 0
         AAdd( ::aDrawPoints, { nWx, nWy } )
      else
         cx    := ::aDrawPoints[ 1 ][ 1 ]
         cy    := ::aDrawPoints[ 1 ][ 2 ]
         dx    := nWx - cx
         dy    := nWy - cy
         radio := Sqrt( dx * dx + dy * dy )
         pEnt  := Dwg_AddCircle( ::pDoc, cx, cy, 0, radio )
         ::FinishEntity( pEnt )
      endif

   case ::cDrawMode == "RECT"
      if Len( ::aDrawPoints ) == 0
         AAdd( ::aDrawPoints, { nWx, nWy } )
      else
         aCorners := DwgRectCorners( ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ], nWx, nWy )
         pEnt := Dwg_AddPolyline( ::pDoc )
         if pEnt != NIL
            for i := 1 to Len( aCorners )
               Dwg_AddVertex( pEnt, aCorners[ i ][ 1 ], aCorners[ i ][ 2 ], 0 )
            next
            Dwg_PolySetClosed( pEnt, .T. )
         endif
         ::FinishEntity( pEnt )
      endif

   case ::cDrawMode == "ARC"
      // 3 puntos: centro, un punto que fija radio+angulo inicial, un
      // punto que fija el angulo final (mismo radio) -- simplificacion
      // deliberada, no es el "arco de 3 puntos" real de AutoCAD (que
      // pasa por los 3 puntos sobre el arco y necesitaria el circulo
      // circunscrito) sino una construccion mas simple que encaja
      // directo con el modelo center+radius+angles ya existente.
      if Len( ::aDrawPoints ) < 2
         AAdd( ::aDrawPoints, { nWx, nWy } )
      else
         cx     := ::aDrawPoints[ 1 ][ 1 ]
         cy     := ::aDrawPoints[ 1 ][ 2 ]
         dx     := ::aDrawPoints[ 2 ][ 1 ] - cx
         dy     := ::aDrawPoints[ 2 ][ 2 ] - cy
         radio  := Sqrt( dx * dx + dy * dy )
         angIni := Atan2Deg( dy, dx )
         angFin := Atan2Deg( nWy - cy, nWx - cx )
         pEnt   := Dwg_AddArc( ::pDoc, cx, cy, 0, radio, angIni, angFin )
         ::FinishEntity( pEnt )
      endif

   case ::cDrawMode == "DIM"
      // 3 puntos: xline1, xline2 (los dos puntos a medir), def_pt
      // (ubica la linea de cota, offset perpendicular a la direccion
      // xline1->xline2) -- cota lineal ALINEADA, ver dwg_dimension.h
      // para el porque no hay un cuarto modo/subtipo que elegir.
      if Len( ::aDrawPoints ) < 2
         AAdd( ::aDrawPoints, { nWx, nWy } )
      else
         pEnt := Dwg_AddDimensionLinear( ::pDoc, ;
                     ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ], 0, ;
                     ::aDrawPoints[ 2 ][ 1 ], ::aDrawPoints[ 2 ][ 2 ], 0, ;
                     nWx, nWy, 0 )
         ::FinishEntity( pEnt )
      endif

   case ::cDrawMode == "TEXT"
      // UN solo click (sin acumular puntos, a diferencia de LINE/
      // CIRCLE/etc) -- abre el dialogo de Texto/Fuente/Tamaño
      // (dwg_text_dlg.prg), que crea la entidad y llama ::FinishEntity
      // por su cuenta (o no crea nada si se cancela). ::aDrawPoints
      // nunca se usa en este modo, se queda vacio y listo para el
      // proximo click.
      DwgTextDlg( Self, nWx, nWy )

   case ::cDrawMode == "POLYLINE"
      AAdd( ::aDrawPoints, { nWx, nWy } )
      // cierre automatico: 3+ vertices puestos y este cae cerca del
      // primero -- se interpreta como "cerrar y terminar" (gesto CAD
      // estandar, alternativa al click derecho que termina abierta).
      dx := nWx - ::aDrawPoints[ 1 ][ 1 ]
      dy := nWy - ::aDrawPoints[ 1 ][ 2 ]
      if Len( ::aDrawPoints ) >= 3 .and. ;
         Sqrt( dx * dx + dy * dy ) < ( DWGVIEW_CLOSE_TOLERANCE_PX * ::nScale )
         ASize( ::aDrawPoints, Len( ::aDrawPoints ) - 1 )   // saca el punto de cierre, redundante con el primero
         ::FinishPolyline( .T. )
      endif

   endcase

return nil

//----------------------------------------------------------------------------//

METHOD FinishPolyline( lCerrar ) CLASS TDwgViewer

   local pEnt, i

   if ::pDoc == NIL .or. Len( ::aDrawPoints ) < 2
      ::aDrawPoints := {}
      return nil
   endif

   pEnt := Dwg_AddPolyline( ::pDoc )
   if pEnt != NIL
      for i := 1 to Len( ::aDrawPoints )
         Dwg_AddVertex( pEnt, ::aDrawPoints[ i ][ 1 ], ::aDrawPoints[ i ][ 2 ], 0 )
      next
      Dwg_PolySetClosed( pEnt, lCerrar )
   endif

   ::FinishEntity( pEnt )

return nil

//----------------------------------------------------------------------------//

// Comun a LINE/CIRCLE/RECT/ARC (llamado directo desde DrawClick) y a
// POLYLINE (via FinishPolyline): clasifica la entidad recien creada en
// capa "0" (Dwg_EntitySetProps con lSetGeom=.F. -- no toca la geometria
// recien puesta), la deja como la UNICA seleccionada (Dwg_SelAdd) para
// que sea facil reclasificarla/ajustarla desde Propiedades, limpia
// ::aDrawPoints (::cDrawMode NO se toca -- se sigue en el mismo modo,
// ver DrawClick) y repinta.
METHOD FinishEntity( pEntidad ) CLASS TDwgViewer

   if pEntidad != NIL
      Dwg_EntitySetProps( ::pDoc, pEntidad, "0", -1, "", 0, 0, 0, 0, 0, 0, "", .F. )
      Dwg_SelClear( ::pDoc )
      Dwg_SelAdd( ::pDoc, pEntidad )
   endif

   ::aDrawPoints := {}
   ::Render()

return nil

//----------------------------------------------------------------------------//

// Devuelve que previsualizar dado el punto de mundo bajo el cursor
// AHORA MISMO -- { aPuntosMundo, lCerrado }, aPuntosMundo un array de
// {x,y} (CIRCLE/ARC ya vienen teselados aca, ver DwgCirclePoints/
// DwgArcPoints, para que TDwgBitmap no tenga que saber nada de
// geometria, solo convertir a pixel y dibujar la cadena de segmentos).
// NIL si todavia no hay ningun punto puesto (nada que mostrar).
METHOD GetDrawPreview( nWxCursor, nWyCursor ) CLASS TDwgViewer

   local cx, cy, radio, angIni, angFin, dx, dy
   local aPts, i

   if ::cDrawMode == NIL .or. Len( ::aDrawPoints ) == 0
      return NIL
   endif

   do case
   case ::cDrawMode == "LINE"
      return { { { ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ] }, { nWxCursor, nWyCursor } }, .F. }

   case ::cDrawMode == "RECT"
      return { DwgRectCorners( ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ], nWxCursor, nWyCursor ), .T. }

   case ::cDrawMode == "CIRCLE" .or. ( ::cDrawMode == "ARC" .and. Len( ::aDrawPoints ) == 1 )
      // ARC con solo el centro puesto todavia -- se previsualiza como un
      // circulo (el radio recien queda fijo con el segundo click).
      cx    := ::aDrawPoints[ 1 ][ 1 ]
      cy    := ::aDrawPoints[ 1 ][ 2 ]
      dx    := nWxCursor - cx
      dy    := nWyCursor - cy
      radio := Sqrt( dx * dx + dy * dy )
      return { DwgCirclePoints( cx, cy, radio ), .T. }

   case ::cDrawMode == "ARC"   // ya con centro + punto de radio/angulo inicial puestos
      cx     := ::aDrawPoints[ 1 ][ 1 ]
      cy     := ::aDrawPoints[ 1 ][ 2 ]
      dx     := ::aDrawPoints[ 2 ][ 1 ] - cx
      dy     := ::aDrawPoints[ 2 ][ 2 ] - cy
      radio  := Sqrt( dx * dx + dy * dy )
      angIni := Atan2Deg( dy, dx )
      angFin := Atan2Deg( nWyCursor - cy, nWxCursor - cx )
      return { DwgArcPoints( cx, cy, radio, angIni, angFin ), .F. }

   case ::cDrawMode == "DIM" .and. Len( ::aDrawPoints ) == 1
      // solo xline1 puesto todavia -- preview simple linea hasta el
      // cursor, igual que el modo LINE con 1 punto.
      return { { { ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ] }, { nWxCursor, nWyCursor } }, .F. }

   case ::cDrawMode == "DIM"   // xline1+xline2 ya puestos, cursor = def_pt
      return { DwgDimensionPreviewPoints( ::aDrawPoints[ 1 ][ 1 ], ::aDrawPoints[ 1 ][ 2 ], ;
                                          ::aDrawPoints[ 2 ][ 1 ], ::aDrawPoints[ 2 ][ 2 ], ;
                                          nWxCursor, nWyCursor ), .F. }

   case ::cDrawMode == "POLYLINE"
      aPts := {}
      for i := 1 to Len( ::aDrawPoints )
         AAdd( aPts, { ::aDrawPoints[ i ][ 1 ], ::aDrawPoints[ i ][ 2 ] } )
      next
      AAdd( aPts, { nWxCursor, nWyCursor } )
      return { aPts, .F. }

   endcase

return NIL

//----------------------------------------------------------------------------//

// Recorre la seleccion actual (Dwg_SelGet 1..Dwg_SelCount) y sus grips
// (DwgEntityGrips mas abajo) buscando el primero dentro de nTolWorld del
// punto de mundo (nWx,nWy) -- devuelve { pEntidad, nIndiceGrip, aProps }
// (aProps es la foto completa de Dwg_EntityGetProps, para que
// StartGripDrag no tenga que volver a leerla) o NIL si ninguno esta lo
// bastante cerca. Solo tiene sentido en 2D (mismo alcance que
// seleccion), aunque no hace falta chequear ::l3DMode aca -- TDwgBitmap
// ya no llama esto en 3D.
METHOD HitTestGrip( nWx, nWy, nTolWorld ) CLASS TDwgViewer

   local nCount, i, j, pEnt, aProps, aGrips, dx, dy

   if ::pDoc == NIL
      return NIL
   endif

   nCount := Dwg_SelCount( ::pDoc )
   for i := 1 to nCount
      pEnt   := Dwg_SelGet( ::pDoc, i )
      aProps := Dwg_EntityGetProps( ::pDoc, pEnt )
      if aProps != NIL
         aGrips := DwgEntityGrips( aProps )
         for j := 1 to Len( aGrips )
            dx := nWx - aGrips[ j ][ 1 ]
            dy := nWy - aGrips[ j ][ 2 ]
            if Sqrt( dx * dx + dy * dy ) <= nTolWorld
               return { pEnt, j, aProps }
            endif
         next
      endif
   next

return NIL

//----------------------------------------------------------------------------//

METHOD StartGripDrag( pEntidad, nIndiceGrip, aProps ) CLASS TDwgViewer

   ::pGripEntity := pEntidad
   ::nGripIndex  := nIndiceGrip
   ::aGripProps  := aProps

return nil

//----------------------------------------------------------------------------//

METHOD CancelGripDrag() CLASS TDwgViewer

   ::pGripEntity := NIL
   ::nGripIndex  := 0
   ::aGripProps  := NIL

return nil

//----------------------------------------------------------------------------//

// ESC/"Seleccionar" (pedido de Arturo 2026-08-27, "cuando se hace ESC
// deberia volver el mouse a modo seleccionar") -- logica compartida
// entre TDwgBitmap:KeyDown (atajo "best-effort", solo funciona si el
// LIENZO tiene el foco de teclado) y el ACCELERATOR ACC_NORMAL,
// VK_ESCAPE nuevo en el menu "Seleccionar" de dwg_demo.prg (funciona
// SIEMPRE, sin importar que control tenga el foco -- un accelerator de
// Windows se resuelve contra la ventana TOP-LEVEL, no contra el
// control enfocado, exactamente el motivo por el que se agrego: el
// atajo de teclado por si solo no alcanzaba si el foco habia quedado
// en un boton de la barra despues de clickear una herramienta).
// ::oBmp:TogglePreview(NIL) vive en TDwgBitmap (no en TDwgViewer) --
// borra el preview XOR en curso, si habia uno, antes de cancelar.
METHOD EscapeAction() CLASS TDwgViewer

   if ::cDrawMode != NIL
      if ::oBmp != NIL
         ::oBmp:TogglePreview( NIL )
      endif
      ::CancelDraw()
   elseif ::pGripEntity != NIL
      if ::oBmp != NIL
         ::oBmp:TogglePreview( NIL )
      endif
      ::CancelGripDrag()
   endif

return nil

//----------------------------------------------------------------------------//

// Mismo formato que GetDrawPreview ({ aPuntosMundo, lCerrado } o NIL).
// LINE: el grip fijo queda donde estaba, el que se arrastra sigue al
// cursor. CIRCLE: arrastrar el centro (grip 1) mueve todo con el mismo
// radio; arrastrar la maneta (grip 2) mantiene el centro y cambia el
// radio. ARC: arrastrar el centro (grip 1) mueve todo con radio/angulos
// iguales; arrastrar un punto de angulo (grip 2 o 3) SOLO recalcula ese
// angulo (el grip queda restringido al circulo del radio actual, mismo
// comportamiento de grip de arco que cualquier CAD real). POINT/TEXT:
// sin forma real que previsualizar -- una linea de referencia desde la
// posicion original hasta el cursor alcanza.
METHOD GetGripPreview( nWxCursor, nWyCursor ) CLASS TDwgViewer

   local nTipo, g1, g2, g4, dx, dy

   if ::pGripEntity == NIL .or. ::aGripProps == NIL
      return NIL
   endif

   nTipo := ::aGripProps[ 1 ]
   g1    := ::aGripProps[ 5 ]
   g2    := ::aGripProps[ 6 ]
   g4    := ::aGripProps[ 8 ]

   do case
   case nTipo == DWGVIEW_T_LINE
      if ::nGripIndex == 1
         return { { { nWxCursor, nWyCursor }, { ::aGripProps[ 8 ], ::aGripProps[ 9 ] } }, .F. }
      endif
      return { { { g1, g2 }, { nWxCursor, nWyCursor } }, .F. }

   case nTipo == DWGVIEW_T_CIRCLE
      if ::nGripIndex == 1
         return { DwgCirclePoints( nWxCursor, nWyCursor, g4 ), .T. }
      endif
      dx := nWxCursor - g1
      dy := nWyCursor - g2
      return { DwgCirclePoints( g1, g2, Sqrt( dx * dx + dy * dy ) ), .T. }

   case nTipo == DWGVIEW_T_ARC
      if ::nGripIndex == 1
         return { DwgArcPoints( nWxCursor, nWyCursor, g4, ::aGripProps[ 9 ], ::aGripProps[ 10 ] ), .F. }
      elseif ::nGripIndex == 2
         return { DwgArcPoints( g1, g2, g4, Atan2Deg( nWyCursor - g2, nWxCursor - g1 ), ::aGripProps[ 10 ] ), .F. }
      endif
      return { DwgArcPoints( g1, g2, g4, ::aGripProps[ 9 ], Atan2Deg( nWyCursor - g2, nWxCursor - g1 ) ), .F. }

   case nTipo == DWGVIEW_T_POINT .or. nTipo == DWGVIEW_T_TEXT
      return { { { g1, g2 }, { nWxCursor, nWyCursor } }, .F. }

   endcase

return NIL

//----------------------------------------------------------------------------//

// Aplica el arrastre: recalcula SOLO el/los campo(s) que corresponden a
// ::nGripIndex (misma logica que GetGripPreview, pero para confirmar en
// vez de previsualizar) y llama Dwg_EntitySetProps pasando el resto de
// ::aGripProps TAL CUAL para no perderlo -- capa/color/linetype "" /-1
// (no tocar, mismo contrato ya usado en FinishEntity/Propiedades).
METHOD CommitGripDrag( nWxCursor, nWyCursor ) CLASS TDwgViewer

   local nTipo, g1, g2, g3, g4, g5, g6, cTexto, dx, dy

   if ::pGripEntity == NIL .or. ::aGripProps == NIL
      ::CancelGripDrag()
      return nil
   endif

   nTipo  := ::aGripProps[ 1 ]
   g1     := ::aGripProps[ 5 ]
   g2     := ::aGripProps[ 6 ]
   g3     := ::aGripProps[ 7 ]
   g4     := ::aGripProps[ 8 ]
   g5     := ::aGripProps[ 9 ]
   g6     := ::aGripProps[ 10 ]
   cTexto := ::aGripProps[ 11 ]

   do case
   case nTipo == DWGVIEW_T_POINT .or. nTipo == DWGVIEW_T_TEXT
      g1 := nWxCursor
      g2 := nWyCursor

   case nTipo == DWGVIEW_T_LINE
      if ::nGripIndex == 1
         g1 := nWxCursor
         g2 := nWyCursor
      else
         g4 := nWxCursor
         g5 := nWyCursor
      endif

   case nTipo == DWGVIEW_T_CIRCLE
      if ::nGripIndex == 1
         g1 := nWxCursor
         g2 := nWyCursor
      else
         dx := nWxCursor - g1
         dy := nWyCursor - g2
         g4 := Sqrt( dx * dx + dy * dy )
      endif

   case nTipo == DWGVIEW_T_ARC
      if ::nGripIndex == 1
         g1 := nWxCursor
         g2 := nWyCursor
      elseif ::nGripIndex == 2
         g5 := Atan2Deg( nWyCursor - g2, nWxCursor - g1 )
      else
         g6 := Atan2Deg( nWyCursor - g2, nWxCursor - g1 )
      endif

   endcase

   Dwg_EntitySetProps( ::pDoc, ::pGripEntity, "", -1, "", g1, g2, g3, g4, g5, g6, cTexto, .T. )

   ::CancelGripDrag()
   ::Render()

return nil

//----------------------------------------------------------------------------//
// Helpers de geometria para creacion/preview -- sin estado, reusados por
// TDwgViewer:DrawClick/GetDrawPreview de arriba, y por HitTestGrip/
// GetGripPreview/CommitGripDrag/DwgDrawGrips de edicion por grips.
//----------------------------------------------------------------------------//

// Deriva los puntos de control ("grips") de una entidad a partir de un
// aProps ya leido (Dwg_EntityGetProps) -- reusado por el dibujo de
// grips (DwgDrawGrips), el hit-test de arrastre (TDwgViewer:
// HitTestGrip) y el preview/commit (GetGripPreview/CommitGripDrag).
// Mismo alcance de tipos que el dialogo de Propiedades (POINT/LINE/
// CIRCLE/ARC/TEXT); cualquier otro tipo no tiene grips (array vacio).
STATIC FUNCTION DwgEntityGrips( aProps )

   local nTipo := aProps[ 1 ]
   local g1 := aProps[ 5 ], g2 := aProps[ 6 ]
   local g4 := aProps[ 8 ], g5 := aProps[ 9 ], g6 := aProps[ 10 ]
   local angIniRad, angFinRad

   do case
   case nTipo == DWGVIEW_T_POINT .or. nTipo == DWGVIEW_T_TEXT
      return { { g1, g2 } }

   case nTipo == DWGVIEW_T_LINE
      return { { g1, g2 }, { g4, g5 } }

   case nTipo == DWGVIEW_T_CIRCLE
      return { { g1, g2 }, { g1 + g4, g2 } }

   case nTipo == DWGVIEW_T_ARC
      angIniRad := g5 * Pi() / 180.0
      angFinRad := g6 * Pi() / 180.0
      return { { g1, g2 }, ;
               { g1 + g4 * Cos( angIniRad ), g2 + g4 * Sin( angIniRad ) }, ;
               { g1 + g4 * Cos( angFinRad ), g2 + g4 * Sin( angFinRad ) } }

   endcase

return {}

//----------------------------------------------------------------------------//

// Dibuja un cuadradito amarillo (color de grip clasico, distinto del
// magenta que ya marca la seleccion, ver DWG_RENDER_SELECTED_PSEUDO_COLOR
// en dwg_render.c) en cada grip de cada entidad seleccionada -- HORNEADO
// dentro de hBitmap (CreateCompatibleDC+SelectObject sobre ESE bitmap,
// mismo patron que BuildComposite ya usa en pdf_viewer.prg para componer
// bitmaps), no un overlay en pantalla, para que sobreviva repintados sin
// logica extra. Llamado desde TDwgViewer:Render() antes de asignar el
// bitmap a ::oBmp.
STATIC FUNCTION DwgDrawGrips( pDoc, hBitmap, nOriginX, nOriginY, nScale )

   local hDC, hBrush, hOldBrush
   local nCount, i, j, aProps, aGrips, nRow, nCol

   nCount := Dwg_SelCount( pDoc )
   if nCount == 0
      return nil
   endif

   hDC       := CreateCompatibleDC( 0 )
   SelectObject( hDC, hBitmap )
   hBrush    := CreateSolidBrush( RGB( 255, 255, 0 ) )
   hOldBrush := SelectObject( hDC, hBrush )

   for i := 1 to nCount
      aProps := Dwg_EntityGetProps( pDoc, Dwg_SelGet( pDoc, i ) )
      if aProps != NIL
         aGrips := DwgEntityGrips( aProps )
         for j := 1 to Len( aGrips )
            nCol := ( aGrips[ j ][ 1 ] - nOriginX ) / nScale
            nRow := ( nOriginY - aGrips[ j ][ 2 ] ) / nScale
            Rectangle( hDC, nRow - DWGVIEW_GRIP_HALFSIZE, nCol - DWGVIEW_GRIP_HALFSIZE, ;
                            nRow + DWGVIEW_GRIP_HALFSIZE, nCol + DWGVIEW_GRIP_HALFSIZE )
         next
      endif
   next

   SelectObject( hDC, hOldBrush )
   DeleteObject( hBrush )
   DeleteDC( hDC )

return nil

//----------------------------------------------------------------------------//
// Helpers de geometria para creacion/preview -- sin estado, reusados por
// TDwgViewer:DrawClick/GetDrawPreview de arriba.
//----------------------------------------------------------------------------//

// atan2(dy,dx) en GRADOS -- Harbour/CT3 (hbct.lib, ya linkeado, ver
// win32\BuildMSVC.bat) solo tiene Atan() de un argumento (radianes,
// como su equivalente C), sin version de 2 argumentos con cuadrante
// correcto -- construccion estandar de atan2 a partir de atan, valida
// para el rango de entrada real aca (dx/dy vienen de una resta de dos
// puntos ya en (-180,180], nunca hace falta el caso dx==dy==0 mas que
// como borde defensivo).
STATIC FUNCTION Atan2Deg( dy, dx )

   local nRad

   if dx > 0
      nRad := Atan( dy / dx )
   elseif dx < 0
      if dy >= 0
         nRad := Atan( dy / dx ) + Pi()
      else
         nRad := Atan( dy / dx ) - Pi()
      endif
   else
      if dy > 0
         nRad := Pi() / 2
      elseif dy < 0
         nRad := -Pi() / 2
      else
         nRad := 0
      endif
   endif

return nRad * 180.0 / Pi()

//----------------------------------------------------------------------------//

// Las 4 esquinas de un rectangulo axis-aligned dado por dos esquinas
// opuestas (x1,y1)-(x2,y2), en orden de recorrido -- reusado tanto por
// el preview (GetDrawPreview) como por la creacion real (DrawClick),
// misma derivacion para que previsualizacion y resultado final coincidan
// exactamente.
STATIC FUNCTION DwgRectCorners( x1, y1, x2, y2 )
return { { x1, y1 }, { x2, y1 }, { x2, y2 }, { x1, y2 } }

//----------------------------------------------------------------------------//

// Aproxima un CIRCLE como un poligono de DWGVIEW_PREVIEW_CIRCLE_SEGMENTS
// puntos -- solo para el preview XOR mientras se dibuja (Dwg_AddCircle
// toma centro+radio directo, no necesita esto; ver dwg_render.c's
// draw_circle para el equivalente real, mas segmentos, del circulo ya
// creado).
STATIC FUNCTION DwgCirclePoints( cx, cy, radio )

   local aPts := {}
   local i, a

   for i := 0 to DWGVIEW_PREVIEW_CIRCLE_SEGMENTS - 1
      a := 2 * Pi() * ( i / DWGVIEW_PREVIEW_CIRCLE_SEGMENTS )
      AAdd( aPts, { cx + radio * Cos( a ), cy + radio * Sin( a ) } )
   next

return aPts

//----------------------------------------------------------------------------//

// Aproxima un ARC (centro+radio+angulos en GRADOS) como una cadena de
// DWGVIEW_PREVIEW_ARC_SEGMENTS+1 puntos, barriendo SIEMPRE en sentido
// antihorario de angIni a angFin -- misma convencion que dwg_render.c's
// draw_arc ("DWG arcs always sweep counterclockwise, start->end") y
// Dwg_AddArc, para que el preview trace EXACTAMENTE el mismo arco que
// la entidad real va a tener (deliberadamente NO se usa el Arc() nativo
// de GDI para el preview -- infiere su propio sentido de barrido a
// partir de dos puntos sobre la elipse en coordenadas de PANTALLA, que
// al estar Y invertida respecto del mundo puede terminar trazando el
// arco opuesto; tesela manual con la MISMA formula que el resto del
// motor ya usa elimina esa ambiguedad de raiz).
STATIC FUNCTION DwgArcPoints( cx, cy, radio, angIniDeg, angFinDeg )

   local angIniRad := angIniDeg * Pi() / 180.0
   local angFinRad := angFinDeg * Pi() / 180.0
   local sweep := angFinRad - angIniRad
   local aPts := {}
   local i, a

   if sweep <= 0
      sweep += 2 * Pi()
   endif

   for i := 0 to DWGVIEW_PREVIEW_ARC_SEGMENTS
      a := angIniRad + sweep * ( i / DWGVIEW_PREVIEW_ARC_SEGMENTS )
      AAdd( aPts, { cx + radio * Cos( a ), cy + radio * Sin( a ) } )
   next

return aPts

//----------------------------------------------------------------------------//

// Cadena de 4 puntos { xline1, dl1, dl2, xline2 } (extension1 + linea
// de cota + extension2, sin flechas ni texto -- alcanza como preview)
// para el modo "DIM" -- MISMA formula de proyeccion perpendicular
// (def_pt contra la direccion xline1->xline2) que dwg_render.c's
// draw_dimension y bridge_dimension en dwg_libredwg_bridge.c usan del
// lado C, duplicada aca a proposito (igual criterio que ya se duplico
// para ARC/CIRCLE entre el render C y este preview .prg) para que la
// forma final coincida exacto con lo que se ve mientras se dibuja.
STATIC FUNCTION DwgDimensionPreviewPoints( x1, y1, x2, y2, defx, defy )

   local dx := x2 - x1
   local dy := y2 - y1
   local dist := Sqrt( dx * dx + dy * dy )
   local dirx, diry, perpx, perpy
   local defPerp, xl1Perp, xl2Perp, delta1, delta2
   local dl1x, dl1y, dl2x, dl2y

   if dist < 0.000000001
      dirx := 1 ; diry := 0
   else
      dirx := dx / dist ; diry := dy / dist
   endif
   perpx := -diry
   perpy := dirx

   defPerp := defx * perpx + defy * perpy
   xl1Perp := x1 * perpx + y1 * perpy
   xl2Perp := x2 * perpx + y2 * perpy
   delta1  := defPerp - xl1Perp
   delta2  := defPerp - xl2Perp
   dl1x := x1 + delta1 * perpx
   dl1y := y1 + delta1 * perpy
   dl2x := x2 + delta2 * perpx
   dl2y := y2 + delta2 * perpy

return { { x1, y1 }, { dl1x, dl1y }, { dl2x, dl2y }, { x2, y2 } }
