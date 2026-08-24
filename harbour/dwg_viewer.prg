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
// Interaccion (primera pasada, ver notas de alcance mas abajo):
//   - arrastre con boton IZQUIERDO = pan (no hay seleccion de texto que
//     lo dispute, a diferencia de PDF -- CAD no tiene ese conflicto)
//   - rueda del mouse = zoom, CENTRADO EN EL VIEWPORT (no en el cursor
//     -- zoom cursor-centrado necesitaria confirmar si nXPos/nYPos de
//     MouseWheel vienen en coordenadas de pantalla o de cliente, que
//     esta sesion no pudo verificar interactivamente, ver memoria
//     "No simulated input on shared desktop"; centrado-en-viewport es
//     un default razonable y sin ambiguedad mientras tanto)
// ============================================================================

#include "FiveWin.ch"

#define DWGVIEW_ZOOM_STEP   1.25
#define DWGVIEW_FIT_MARGIN  1.05    // 5% de aire alrededor de los extents, mismo criterio "fit" que cualquier visor CAD
#define IDC_HAND  32649
#define IDC_ARROW 32512
//----------------------------------------------------------------------------//

CLASS TDwgBitmap FROM TBitmap

   DATA oViewer                     // TDwgViewer dueño (para ::nScale/::nOriginX/::nOriginY/::pDoc)

   DATA lPanning         INIT .F.
   DATA nPanStartRow
   DATA nPanStartCol
   DATA nPanStartOriginX
   DATA nPanStartOriginY

   METHOD LButtonDown( nRow, nCol, nKeyFlags )
   METHOD LButtonUp( nRow, nCol, nKeyFlags )
   METHOD MouseMove( nRow, nCol, nKeyFlags )
   METHOD MouseWheel( nKeys, nDelta, nXPos, nYPos )

ENDCLASS

//----------------------------------------------------------------------------//

METHOD LButtonDown( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   if ::oViewer != NIL .and. ::oViewer:pDoc != NIL
      ::lPanning         := .T.
      ::nPanStartRow     := nRow
      ::nPanStartCol     := nCol
      ::nPanStartOriginX := ::oViewer:nOriginX
      ::nPanStartOriginY := ::oViewer:nOriginY
      ::Capture()
      SetCursor( LoadCursor( 0, IDC_HAND ) )
   endif

return ::Super:LButtonDown( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD LButtonUp( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

   if ::lPanning
      ::lPanning := .F.
      ReleaseCapture()
      SetCursor( LoadCursor( 0, IDC_ARROW ) )
   endif

return ::Super:LButtonUp( nRow, nCol, nKeyFlags )

//----------------------------------------------------------------------------//

METHOD MouseMove( nRow, nCol, nKeyFlags ) CLASS TDwgBitmap

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

   aRect      := GetClientRect( ::hWnd )
   nCenterRow := ( aRect[ 3 ] - aRect[ 1 ] ) / 2
   nCenterCol := ( aRect[ 4 ] - aRect[ 2 ] ) / 2

   // punto del mundo que hoy cae en el centro del viewport -- se
   // mantiene fijo ahi despues del zoom (si no, cada zoom "corre" el
   // dibujo hacia una esquina).
   wx := ::oViewer:nOriginX + nCenterCol * ::oViewer:nScale
   wy := ::oViewer:nOriginY - nCenterRow * ::oViewer:nScale

   if nDelta > 0
      nNewScale := ::oViewer:nScale / DWGVIEW_ZOOM_STEP    // rueda hacia adelante = acercar = menos unidades-mundo por pixel
   else
      nNewScale := ::oViewer:nScale * DWGVIEW_ZOOM_STEP
   endif
   if nNewScale < 0.000000001                                     // guarda contra underflow si se gira la rueda muchas veces seguidas
      nNewScale := 0.000000001
   endif
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

   DATA nDispTop      INIT 0
   DATA nDispLeft     INIT 0
   DATA nDispWidth    INIT 0
   DATA nDispHeight   INIT 0

   DATA hBitmap       INIT 0        // HBITMAP actualmente asignado a ::oBmp (para liberarlo antes de reemplazarlo)

   METHOD New( oWnd, nTop, nLeft, nWidth, nHeight ) CONSTRUCTOR

   METHOD Open( cFile )             // abre (o reemplaza) el dibujo mostrado. .T./.F.
   METHOD Close()
   METHOD End() INLINE ::Close()

   METHOD Resize( nWidth, nHeight )

   METHOD ZoomFit()                 // recalcula nScale/nOriginX/nOriginY contra DWG_GETEXTENTS y renderiza
   METHOD ZoomIn()  INLINE ::ZoomAroundCenter( 1 / DWGVIEW_ZOOM_STEP )
   METHOD ZoomOut() INLINE ::ZoomAroundCenter( DWGVIEW_ZOOM_STEP )
   METHOD ZoomAroundCenter( nFactor )

   METHOD Render()                  // pide un DWG_RENDERTOHBITMAP fresco al tamanio actual del viewport y lo asigna a ::oBmp

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

   aResult := Dwg_RenderToHBitmap( ::pDoc, nW, nH, ::nScale, ::nOriginX, ::nOriginY )
   if aResult == NIL
      return .F.
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
