// dwg_text_dlg.prg
//
// ============================================================================
// Dialogo de texto para TDwgViewer (ver dwg_viewer.prg) -- pedido de
// Arturo 2026-08-26 ("agregar funcion para escribir texto definiendo el
// tipo de letra y tamaño"). Se abre con UN click en modo "TEXT"
// (TDwgViewer:DrawClick), pide el contenido, la fuente, y el tamaño
// (altura en unidades de mundo), y crea la entidad (Dwg_AddText, ver
// dwg_hbfunc.c) si el texto no quedo vacio.
//
// La fuente se elige de una LISTA de las que hay REALMENTE instaladas
// en Windows (pedido explicito de Arturo, correccion sobre la primera
// version de este dialogo que dejaba escribir cualquier nombre a mano)
// -- EnumFontFamilies (ya expuesto por FWH2603, ver
// source/winapi/printdc.c) con cFamily=NIL enumera TODA la lista real
// del sistema, no el selector nativo ChooseFont (descartado la vuelta
// pasada: su tamaño en puntos de pantalla es indirecto/riesgoso de
// convertir a unidades de mundo del dibujo -- eso se sigue evitando,
// el tamaño sigue siendo un campo propio en unidades de mundo).
//
// cTexto se preasigna con Space(180) (ajuste de Arturo): un GET de
// caracter en Harbour edita dentro del ancho FIJO del buffer inicial,
// no crece solo -- "" hubiera dejado el campo inutilizable para
// escribir nada mas largo que cero caracteres.
//
// Edicion in situ (pedido de Arturo 2026-08-27, "editar textos in
// situ") -- este MISMO dialogo ahora dobla como editor de un TEXT ya
// existente: pEntEdit (nuevo, opcional) llega desde
// TDwgBitmap:LDblClick (doble-click sobre un TEXT en el lienzo) con la
// entidad clickeada; si viene, el dialogo se precarga con su
// contenido/fuente/altura ACTUALES en vez de los valores de "Texto
// nuevo", el titulo cambia a "Editar texto", y Aceptar actualiza la
// entidad en vez de crear una nueva (posicion/angulo actuales se
// mantienen -- este dialogo no los edita, igual que antes). Ademas, en
// vez de ACTIVATE DIALOG CENTERED, el dialogo se reposiciona (ON INIT)
// junto al punto clickeado en pantalla -- lo mas parecido a "in situ"
// real sin construir un editor flotante nuevo desde cero. Aplica igual
// para crear texto nuevo (mismo nWx/nWy que ya se recibia), asi ambos
// flujos quedan consistentes.
// ============================================================================

#include "FiveWin.ch"
#include "font.ch"      // LF_FACENAME
#include "dwg_demo_ids.ch"

#define DWGTXTDLG_TEXT_BUFLEN 180
#define DWGTXTDLG_SCREEN_OFFSET_PX 12
#define DWGTXTDLG_SM_CXSCREEN 0     // GetSysMetrics -- constantes winapi crudas, no vienen en ningun .ch de FWH2603
#define DWGTXTDLG_SM_CYSCREEN 1

FUNCTION DwgTextDlg( oViewer, nWx, nWy, pEntEdit )

   local oDlg, oLbxFonts
   local oGetTexto, oGetAltura
   local aFonts := DwgListInstalledFonts()
   local cTexto  := Space( DWGTXTDLG_TEXT_BUFLEN )
   local cFuente := DwgDefaultFont( aFonts )
   local nAltura := 2.5   // mismo default que DWG_STYLE_DEFAULT_HEIGHT en dwg_style.h
   local aProps, cStyle
   local nPixRow, nPixCol, aScr

   if oViewer == NIL .or. oViewer:pDoc == NIL
      return nil
   endif

   // Edicion in situ -- precarga contenido/altura (mismo layout de
   // aProps que Dwg_EntityGetProps ya usa en dwg_props_dlg.prg: TEXT es
   // G1..G5 = x,y,z,altura,angulo, cTexto = aProps[11]) y fuente
   // (Dwg_TextGetStyle, nuevo -- ver dwg_hbfunc.c, DWG_ADDTEXT solo la
   // asignaba en la creacion, no habia forma de LEERLA de una entidad
   // ya existente). PadR al mismo buffer fijo que el GET espera (ver
   // doc-comment de arriba).
   if pEntEdit != NIL
      aProps := Dwg_EntityGetProps( oViewer:pDoc, pEntEdit )
      if aProps != NIL
         cTexto  := PadR( aProps[ 11 ], DWGTXTDLG_TEXT_BUFLEN )
         nAltura := aProps[ 8 ]
      endif

      cStyle := Dwg_TextGetStyle( oViewer:pDoc, pEntEdit )
      if !Empty( cStyle )
         cFuente := cStyle
      endif
   endif

   // Recurso real (dwg_demo.rc, pedido de Arturo 2026-08-27) en vez de
   // @ ROW,COL a mano -- DIALOG UNITS, Windows lo escala solo segun el
   // font/DPI real del sistema (la causa de raiz del bug de "GETs
   // gigantes" que antes habia que corregir con un font explicito).
   DEFINE DIALOG oDlg RESOURCE "TEXTO"

   oDlg:cTitle := IIF( pEntEdit == NIL, "Texto", "Editar texto" )

   REDEFINE GET oGetTexto VAR cTexto ID ID_TEXT_TEXTO OF oDlg

   REDEFINE LISTBOX oLbxFonts VAR cFuente ITEMS aFonts ID ID_TEXT_FONTS OF oDlg

   REDEFINE GET oGetAltura VAR nAltura ID ID_TEXT_ALTURA PICTURE "9999.99" OF oDlg

   REDEFINE BUTTON ID IDOK OF oDlg ;
      ACTION ( DwgTextDlgApply( oViewer, nWx, nWy, cTexto, cFuente, nAltura, pEntEdit ), ;
               oDlg:End() )

   REDEFINE BUTTON ID IDCANCEL OF oDlg ;
      ACTION oDlg:End()

   // Posicionado junto al punto clickeado (mundo -> pixel del control
   // -> pantalla, inversa de la formula que TDwgBitmap:LButtonDown/Up
   // ya usa) en vez de centrado -- pedido explicito de Arturo
   // ("editar textos in situ"). Un dialogo RESOURCE no admite el
   // clausulo FROM/TO de DEFINE DIALOG (ignorado por FiveWin para
   // dialogos con RESOURCE, confirmado leyendo dialog.prg:Activate),
   // asi que el reposicionamiento se hace a mano en ON INIT
   // (::hWnd ya existe ahi, todavia no se hizo visible). Clampeado a
   // pantalla, best-effort (mismo criterio que el atajo ESC de
   // TDwgBitmap:KeyDown) -- sin esto un click cerca del borde dejaria
   // el dialogo parcialmente fuera de vista.
   if oViewer:oBmp != NIL .and. !Empty( oViewer:nScale )
      nPixCol := ( nWx - oViewer:nOriginX ) / oViewer:nScale
      nPixRow := ( oViewer:nOriginY - nWy ) / oViewer:nScale
      aScr    := ClientToScreen( oViewer:oBmp:hWnd, { nPixRow, nPixCol } )

      ACTIVATE DIALOG oDlg ON INIT DwgTextDlgPosition( oDlg, aScr[ 1 ], aScr[ 2 ] )
   else
      ACTIVATE DIALOG oDlg CENTERED
   endif

return nil

//----------------------------------------------------------------------------//

// Mueve oDlg (ya creado, todavia no visible -- se llama desde ON INIT)
// para que quede cerca de {nScrRow,nScrCol} (coordenadas de PANTALLA,
// ya convertidas por ClientToScreen) en vez de en su posicion de
// plantilla (0,0 en dwg_demo.rc). Offset fijo para que el punto
// clickeado no quede tapado por la esquina del dialogo; clampeado
// contra el tamaño de pantalla para que nunca quede fuera de vista.
STATIC FUNCTION DwgTextDlgPosition( oDlg, nScrRow, nScrCol )

   local nTop     := nScrRow + DWGTXTDLG_SCREEN_OFFSET_PX
   local nLeft    := nScrCol + DWGTXTDLG_SCREEN_OFFSET_PX
   local nMaxTop  := GetSysMetrics( DWGTXTDLG_SM_CYSCREEN ) - oDlg:nHeight
   local nMaxLeft := GetSysMetrics( DWGTXTDLG_SM_CXSCREEN ) - oDlg:nWidth

   nTop  := Max( Min( nTop,  nMaxTop  ), 0 )
   nLeft := Max( Min( nLeft, nMaxLeft ), 0 )

   oDlg:Move( nTop, nLeft,,, .F. )

return nil

//----------------------------------------------------------------------------//

// Texto vacio -- no crea/deja nada (no tiene sentido una entidad TEXT
// en blanco, y draw_text_string del lado C ya ni la dibujaria).
// pEntEdit == NIL: crea una entidad nueva (comportamiento de siempre).
// pEntEdit != NIL: edicion in situ -- mantiene posicion/angulo actuales
// (la entidad no los expone en este dialogo), solo actualiza
// contenido/altura (Dwg_EntitySetProps, ya existente) y fuente
// (Dwg_TextSetStyle, nuevo -- ver dwg_hbfunc.c).
STATIC FUNCTION DwgTextDlgApply( oViewer, nWx, nWy, cTexto, cFuente, nAltura, pEntEdit )

   local pEnt, aProps

   cTexto := RTrim( cTexto )    // Space(180) de sobra -- ver doc-comment de arriba

   if Empty( cTexto )
      return nil
   endif

   if pEntEdit == NIL
      pEnt := Dwg_AddText( oViewer:pDoc, nWx, nWy, 0, nAltura, 0, cTexto, cFuente )
      oViewer:FinishEntity( pEnt )
   else
      aProps := Dwg_EntityGetProps( oViewer:pDoc, pEntEdit )
      if aProps != NIL
         Dwg_EntitySetProps( oViewer:pDoc, pEntEdit, "", -1, "", ;
            aProps[ 5 ], aProps[ 6 ], aProps[ 7 ], nAltura, aProps[ 9 ], 0, cTexto, .T. )
         Dwg_TextSetStyle( oViewer:pDoc, pEntEdit, cFuente )
         oViewer:Render()
      endif
   endif

return nil

//----------------------------------------------------------------------------//

// Enumera las fuentes REALMENTE instaladas en esta maquina Windows
// (EnumFontFamilies con cFamily=NIL -- ver el comentario de arriba) y
// devuelve los nombres unicos, ordenados. Resguardo (fuentes basicas
// hardcodeadas) si por algun motivo la enumeracion no trae nada -- para
// que el dialogo nunca quede con una lista vacia e inutilizable.
STATIC FUNCTION DwgListInstalledFonts()

   local aFonts := {}
   local hDC := GetDC( GetDesktopWindow() )

   EnumFontFamilies( hDC, NIL, {| aLogFont | DwgAddUniqueFont( aFonts, aLogFont[ LF_FACENAME ] ), 1 } )
   ReleaseDC( GetDesktopWindow(), hDC )

   if Len( aFonts ) == 0
      aFonts := { "Arial", "Times New Roman", "Courier New" }
   else
      ASort( aFonts )
   endif

return aFonts

//----------------------------------------------------------------------------//

STATIC FUNCTION DwgAddUniqueFont( aFonts, cName )

   if !Empty( cName ) .and. AScan( aFonts, {| c | c == cName } ) == 0
      AAdd( aFonts, cName )
   endif

return nil

//----------------------------------------------------------------------------//

// "Arial" preseleccionado si esta en la lista real (el default mas
// familiar/esperado), si no el primero de la lista ya ordenada.
STATIC FUNCTION DwgDefaultFont( aFonts )

   local n := AScan( aFonts, {| c | c == "Arial" } )

   if n > 0
      return aFonts[ n ]
   endif

return IIF( Len( aFonts ) > 0, aFonts[ 1 ], "Arial" )
