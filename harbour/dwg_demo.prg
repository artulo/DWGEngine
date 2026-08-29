// dwg_demo.prg
//
// ============================================================================
// Demo de TDwgViewer (ver dwg_viewer.prg) -- ventana con barra de zoom y
// un boton "Abrir" para elegir otro DWG (a diferencia de PDFEngine32, que
// tenia un unico archivo de prueba fijo, aca no hay un DWG "canonico"
// unico -- reverse\samples\2000\entities-2d.dwg se usa solo como default
// inicial porque cubre varios tipos de entidad a la vez).
// ============================================================================

#include "FiveWin.ch"
#include "dwg_demo_ids.ch"

#define ALTO_TOOLBAR   26
static oWnd
static oDwg
static cDwg := ""
function Main()
	Local Obar
  // DEFAULT cDwg := "..\tests\02_Planta 1 Baja_A3.dwg"

   DEFINE WINDOW oWnd TITLE "DWGEngine - " + cDwg ;
      FROM 0, 0 TO 700, 900 PIXEL MENU BuildMenu()

      DEFINE BUTTONBAR oBar OF oWnd SIZE 60, 24 3D 2007

      // Nuevo/Guardar/Guardar como (pedido de Arturo 2026-08-26,
      // "deberia haber uno crear nuevo y guardar") -- ver TDwgViewer:
      // NewDocument/Save/SaveAs en dwg_viewer.prg. Guardar/Guardar como
      // no avisan nada si devuelven .F. -- eso pasa tanto si el usuario
      // cancelo el dialogo "Guardar como" como si la escritura fallo de
      // verdad, no hay forma de distinguirlas desde aca, y avisar en el
      // caso de simple cancelacion seria molesto.
      DEFINE BUTTON OF oBar ;
         PROMPT "Nuevo" ;
         ACTION ( oDwg:NewDocument(), ;
                  cDwg := "", ;
                  oWnd:cTitle := "DWGEngine - (nuevo)", ;
                  oWnd:Refresh() )

      DEFINE BUTTON OF oBar ;
         PROMPT "Abrir" ;
         ACTION ( cDwg := DwgAskFile(), ;
                  IIF( !Empty( cDwg ), ;
                       IIF( oDwg:Open( cDwg ), ;
                            oWnd:cTitle := "DWGEngine - " + cDwg, ;
                            MsgStop( "No se pudo abrir " + cDwg, "DWGEngine" ) ), NIL ), ;
                  oWnd:Refresh() )

      DEFINE BUTTON OF oBar ;
         PROMPT "Guardar" ;
         ACTION ( IIF( oDwg:Save(), ;
                       ( cDwg := oDwg:cFile, oWnd:cTitle := "DWGEngine - " + cDwg ), NIL ), ;
                  oWnd:Refresh() )

      DEFINE BUTTON OF oBar ;
         PROMPT "Guardar como" ;
         ACTION ( IIF( oDwg:SaveAs(), ;
                       ( cDwg := oDwg:cFile, oWnd:cTitle := "DWGEngine - " + cDwg ), NIL ), ;
                  oWnd:Refresh() )

      DEFINE BUTTON OF oBar ;
         PROMPT "Ajustar" ;
         ACTION ( oDwg:ZoomFit() )

      DEFINE BUTTON OF oBar ;
         PROMPT "-" ;
         ACTION ( oDwg:ZoomOut() )

      DEFINE BUTTON OF oBar ;
         PROMPT "+" ;
         ACTION ( oDwg:ZoomIn() )

      DEFINE BUTTON OF oBar ;
         PROMPT "3D" ;
         ACTION ( oDwg:Toggle3D() )

      DEFINE BUTTON OF oBar ;
         PROMPT "Capas" ;
         ACTION ( DwgLayersDlg( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Propied." ;
         ACTION ( DwgPropsDlg( oDwg ) )
/*
      DEFINE BUTTON OF oBar ;
         PROMPT "Mover" ;
         ACTION ( DwgAskMove( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Rotar" ;
         ACTION ( DwgAskRotate( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Escalar" ;
         ACTION ( DwgAskScale( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Espejar" ;
         ACTION ( DwgAskMirror( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Borrar" ;
         ACTION ( DwgAskErase( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Copiar" ;
         ACTION ( DwgAskCopy( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Explotar" ;
         ACTION ( DwgAskExplode( oDwg ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Unir" ;
         ACTION ( DwgAskJoin( oDwg ) )
*/
    
      DEFINE BUTTON OF oBar ;
         PROMPT "Linea" ;
         ACTION ( oDwg:StartDraw( "LINE" ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Circulo" ;
         ACTION ( oDwg:StartDraw( "CIRCLE" ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Rectang." ;
         ACTION ( oDwg:StartDraw( "RECT" ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Arco" ;
         ACTION ( oDwg:StartDraw( "ARC" ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Polilinea" ;
         ACTION ( oDwg:StartDraw( "POLYLINE" ) )

      // Cota (pedido de Arturo 2026-08-26, "permitir colocar cotas") --
      // 3 clicks: punto 1, punto 2 (los dos a medir), tercer punto que
      // ubica la linea de cota. Ver dwg_dimension.h/TDwgViewer:DrawClick.
      DEFINE BUTTON OF oBar ;
         PROMPT "Cota" ;
         ACTION ( oDwg:StartDraw( "DIM" ) )

      // Texto (pedido de Arturo 2026-08-26, "agregar funcion para
      // escribir texto definiendo el tipo de letra y tamaño") -- UN
      // click abre el dialogo (dwg_text_dlg.prg), que pide el
      // contenido/fuente/tamaño y crea la entidad.
      DEFINE BUTTON OF oBar ;
         PROMPT "Texto" ;
         ACTION ( oDwg:StartDraw( "TEXT" ) )

      DEFINE BUTTON OF oBar ;
         PROMPT "Seleccionar" ;
         ACTION ( oDwg:EscapeAction() )

      oDwg := TDwgViewer():New( oWnd, ALTO_TOOLBAR, 0, ;
                                 oWnd:nWidth, oWnd:nHeight - ALTO_TOOLBAR )

   ACTIVATE WINDOW oWnd ;
      ON INIT (  oDwg:NewDocument(), oDwg:ZoomFit() ) ;
      ON RESIZE ( oDwg:Resize( nWidth, nHeight - ALTO_TOOLBAR ) ) ;
      VALID ( oDwg:Close(), .T. )

return nil

function BuildMenu()
local omenu

MENU oMenu
	MENUITEM "&Archivos"
		MENU
			MENUITEM  "Nuevo"  ACTION ( oDwg:NewDocument(), cDwg := "", ;
				oWnd:cTitle := "DWGEngine - (nuevo)", ;
				oWnd:Refresh() )
			SEPARATOR
			MENUITEM "Abrir" ;
				ACTION ( cDwg := DwgAskFile(), ;
				IIF( !Empty( cDwg ), ;
				IIF( oDwg:Open( cDwg ), ;
				oWnd:cTitle := "DWGEngine - " + cDwg, ;
				MsgStop( "No se pudo abrir " + cDwg, "DWGEngine" ) ), NIL ), ;
			oWnd:Refresh() )

			MENUITEM "Guardar" ;
				ACTION ( IIF( oDwg:Save(), ;
				( cDwg := oDwg:cFile, oWnd:cTitle := "DWGEngine - " + cDwg ), NIL ), ;
				oWnd:Refresh() )

			MENUITEM "Guardar como" ;
				ACTION ( IIF( oDwg:SaveAs(), ;
				( cDwg := oDwg:cFile, oWnd:cTitle := "DWGEngine - " + cDwg ), NIL ), ;
				oWnd:Refresh() )

			MENUITEM"Ajustar" ;
				ACTION ( oDwg:ZoomFit() )

			MENUITEM "Zoomm -" ;
				ACTION ( oDwg:ZoomOut() )

			MENUITEM "Zoom +" ;
				ACTION ( oDwg:ZoomIn() )
		ENDMENU

	MENUITEM "&Editar"
		MENU

			MENUITEM "Capas" ;
			ACTION ( DwgLayersDlg( oDwg ) )
			MENUITEM "Propied." ;
			ACTION ( DwgPropsDlg( oDwg ) )

			MENUITEM "Mover" ;
			ACTION ( DwgAskMove( oDwg ) )

			MENUITEM "Rotar" ;
			ACTION ( DwgAskRotate( oDwg ) )

			MENUITEM "Escalar" ;
			ACTION ( DwgAskScale( oDwg ) )

			MENUITEM "Espejar" ;
			ACTION ( DwgAskMirror( oDwg ) )  
			
			MENUITEM "Borrar" ;
			ACTION ( DwgAskErase( oDwg ) )

			MENUITEM "Copiar" ;
			ACTION ( DwgAskCopy( oDwg ) )

			MENUITEM  "Explotar" ;
			ACTION ( DwgAskExplode( oDwg ) )

			MENUITEM  "Unir" ;
			ACTION ( DwgAskJoin( oDwg ) )

		ENDMENU

	MENUITEM "&Ver"
		MENU
			MENUITEM "Ver 3D" ;
			ACTION ( oDwg:Toggle3D() )
		ENDMENU

	MENUITEM "&Dibujo"
		MENU

			MENUITEM "Linea" ;
			ACTION ( oDwg:StartDraw( "LINE" ) )

			MENUITEM "Circulo" ;
			ACTION ( oDwg:StartDraw( "CIRCLE" ) )

			MENUITEM "Rectang." ;
			ACTION ( oDwg:StartDraw( "RECT" ) )

			MENUITEM "Arco" ;
			ACTION ( oDwg:StartDraw( "ARC" ) )

			MENUITEM "Polilinea" ;
			ACTION ( oDwg:StartDraw( "POLYLINE" ) )

		ENDMENU
		
	MENUITEM "&Anotar"	
		MENU
			MENUITEM "Cota" ;
			ACTION ( oDwg:StartDraw( "DIM" ) )

			// ACCELERATOR (pedido de Arturo 2026-08-27, "cuando se hace
			// ESC deberia volver el mouse a modo seleccionar") -- un
			// accelerator de Windows se resuelve contra la ventana
			// TOP-LEVEL, sin importar que control tenga el foco de
			// teclado (a diferencia del atajo ESC que ya tenia
			// TDwgBitmap:KeyDown, que solo funcionaba si el LIENZO
			// tenia el foco -- p.ej. justo despues de clickear un boton
			// de la barra, el foco queda en el boton, no en el lienzo).
			// ACTION llama TDwgViewer:EscapeAction (misma logica que
			// KeyDown ya usaba, factorizada ahi para no duplicarla).
			MENUITEM "Seleccionar" ;
			ACTION ( oDwg:EscapeAction() ) ;
			ACCELERATOR ACC_NORMAL, VK_ESCAPE
		ENDMENU
ENDMENU
   
return oMenu

//----------------------------------------------------------------------------//

static function DwgAskFile()

   local cFile := cGetFile( "Archivos DWG/DXF|*.dwg;*.dxf|Archivos DWG|*.dwg|Archivos DXF|*.dxf|Todos|*.*", "Abrir dibujo" )

return cFile

//----------------------------------------------------------------------------//

// Operaciones de edicion (pedido de Arturo 2026-08-26) -- piden sus
// parametros por un dialogo simple (DwgAskParams, hasta 4 numeros), NO
// por gestos interactivos en el lienzo (click origen/destino etc.) --
// alcance explicito de esta vuelta, ver plan "Seleccion, capas y
// propiedades interactivas en TDwgViewer". Cada METHOD de TDwgViewer
// (Move/Rotate/Scale/Mirror/Erase/Copy/Explode/Join, ver dwg_viewer.prg)
// ya re-renderiza solo, no hace falta hacerlo aca.

static function DwgAskMove( oDwg )

   local aVal := DwgAskParams( "Mover", { "Dx", "Dy", "Dz" }, { 0, 0, 0 } )

   if aVal != NIL
      oDwg:Move( aVal[ 1 ], aVal[ 2 ], aVal[ 3 ] )
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskRotate( oDwg )

   local aVal := DwgAskParams( "Rotar", { "Cx", "Cy", "Cz", "Angulo (grados)" }, { 0, 0, 0, 0 } )

   if aVal != NIL
      oDwg:Rotate( aVal[ 1 ], aVal[ 2 ], aVal[ 3 ], aVal[ 4 ] )
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskScale( oDwg )

   local aVal := DwgAskParams( "Escalar", { "Cx", "Cy", "Cz", "Factor" }, { 0, 0, 0, 1 } )

   if aVal != NIL
      oDwg:Scale( aVal[ 1 ], aVal[ 2 ], aVal[ 3 ], aVal[ 4 ] )
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskMirror( oDwg )

   local aVal := DwgAskParams( "Espejar (eje X1,Y1 - X2,Y2)", { "X1", "Y1", "X2", "Y2" }, { 0, 0, 0, 0 } )

   if aVal != NIL
      oDwg:Mirror( aVal[ 1 ], aVal[ 2 ], aVal[ 3 ], aVal[ 4 ] )
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskErase( oDwg )

   local n

   if MsgYesNo( "Borrar la seleccion actual?", "Borrar" )
      n := oDwg:Erase()
      MsgInfo( LTrim( Str( n ) ) + " entidad(es) borrada(s).", "Borrar" )
   endif

return nil

//----------------------------------------------------------------------------//

// Dwg_SelCopy (ver dwg_selection.h) deja las COPIAS como la seleccion
// activa -- el desplazamiento pedido aca se aplica DESPUES via
// oDwg:Move(), que actua sobre esa misma seleccion (las copias), asi no
// quedan exactamente superpuestas a los originales.
static function DwgAskCopy( oDwg )

   local aVal := DwgAskParams( "Copiar (desplazamiento)", { "Dx", "Dy", "Dz" }, { 0, 0, 0 } )
   local n

   if aVal != NIL
      n := oDwg:Copy()
      if n > 0 .and. ( aVal[ 1 ] != 0 .or. aVal[ 2 ] != 0 .or. aVal[ 3 ] != 0 )
         oDwg:Move( aVal[ 1 ], aVal[ 2 ], aVal[ 3 ] )
      endif
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskExplode( oDwg )

   local n

   if MsgYesNo( "Explotar la seleccion actual (POLYLINE/INSERT)?", "Explotar" )
      n := oDwg:Explode()
      MsgInfo( LTrim( Str( n ) ) + " entidad(es) nueva(s) creada(s).", "Explotar" )
   endif

return nil

//----------------------------------------------------------------------------//

static function DwgAskJoin( oDwg )

   local aVal := DwgAskParams( "Unir (LINE/ARC conectados)", { "Tolerancia" }, { 0.01 } )
   local n

   if aVal != NIL
      n := oDwg:Join( aVal[ 1 ] )
      MsgInfo( LTrim( Str( n ) ) + " polilinea(s) creada(s).", "Unir" )
   endif

return nil

//----------------------------------------------------------------------------//

// Dialogo generico de hasta 4 parametros numericos -- reusado por todas
// las DwgAsk* de arriba en vez de un dialogo a medida para cada
// operacion (Mover/Escalar/Rotar/Espejar/Copiar/Unir difieren solo en
// cuantos campos y como se llaman). aLabels/aDefaults: 1 a 4 elementos;
// una etiqueta vacia ("") o ausente deshabilita ese campo. Devuelve un
// array de 4 numeros (los no usados quedan en 0) o NIL si se cancelo.
static function DwgAskParams( cTitle, aLabels, aDefaults )

   local oDlg
   local nCount := Len( aLabels )
   local n1 := IIF( nCount >= 1, aDefaults[ 1 ], 0 )
   local n2 := IIF( nCount >= 2, aDefaults[ 2 ], 0 )
   local n3 := IIF( nCount >= 3, aDefaults[ 3 ], 0 )
   local n4 := IIF( nCount >= 4, aDefaults[ 4 ], 0 )
   local cLbl1 := IIF( nCount >= 1, aLabels[ 1 ], "" )
   local cLbl2 := IIF( nCount >= 2, aLabels[ 2 ], "" )
   local cLbl3 := IIF( nCount >= 3, aLabels[ 3 ], "" )
   local cLbl4 := IIF( nCount >= 4, aLabels[ 4 ], "" )
   local oGet1, oGet2, oGet3, oGet4
   local lOk := .F.
   local aResult := NIL

   // Recurso real (dwg_demo.rc, pedido de Arturo 2026-08-27) en vez de
   // @ ROW,COL a mano -- DIALOG UNITS, Windows lo escala solo segun el
   // font/DPI real del sistema (la causa de raiz del bug de "GETs
   // gigantes" que antes habia que corregir con un font explicito).
   DEFINE DIALOG oDlg RESOURCE "PARAMETROS"

   oDlg:cTitle := cTitle

   REDEFINE SAY ID ID_PARAMS_LBL1 VAR cLbl1 OF oDlg
   REDEFINE GET oGet1 VAR n1 ID ID_PARAMS_F1 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY ID ID_PARAMS_LBL2 VAR cLbl2 OF oDlg
   REDEFINE GET oGet2 VAR n2 ID ID_PARAMS_F2 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY ID ID_PARAMS_LBL3 VAR cLbl3 OF oDlg
   REDEFINE GET oGet3 VAR n3 ID ID_PARAMS_F3 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY ID ID_PARAMS_LBL4 VAR cLbl4 OF oDlg
   REDEFINE GET oGet4 VAR n4 ID ID_PARAMS_F4 PICTURE "9999999.9999" OF oDlg

   if nCount < 4 ; oGet4:Disable() ; endif
   if nCount < 3 ; oGet3:Disable() ; endif
   if nCount < 2 ; oGet2:Disable() ; endif

   REDEFINE BUTTON ID IDOK OF oDlg ;
      ACTION ( lOk := .T., oDlg:End() )

   REDEFINE BUTTON ID IDCANCEL OF oDlg ;
      ACTION oDlg:End()

   ACTIVATE DIALOG oDlg CENTERED

   if lOk
      aResult := { n1, n2, n3, n4 }
   endif

return aResult
