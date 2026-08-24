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

#define ALTO_TOOLBAR   26

function Main( cDwg )

   local oWnd, oBar
   local oDwg

   DEFAULT cDwg := "..\tests\entities-2d.dwg"

   DEFINE WINDOW oWnd TITLE "DWGEngine - " + cDwg ;
      FROM 0, 0 TO 700, 900 PIXEL

      DEFINE BUTTONBAR oBar OF oWnd SIZE 60, 24 3D 2007

      DEFINE BUTTON OF oBar ;
         PROMPT "Abrir" ;
         ACTION ( cDwg := DwgAskFile(), ;
                  IIF( !Empty( cDwg ), ;
                       IIF( oDwg:Open( cDwg ), ;
                            oWnd:cTitle := "DWGEngine - " + cDwg, ;
                            MsgStop( "No se pudo abrir " + cDwg, "DWGEngine" ) ), NIL ), ;
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

      oDwg := TDwgViewer():New( oWnd, ALTO_TOOLBAR, 0, ;
                                 oWnd:nWidth, oWnd:nHeight - ALTO_TOOLBAR )

   ACTIVATE WINDOW oWnd ;
      ON INIT ( IIF( oDwg:Open( cDwg ), oDwg:ZoomFit(), ;
                     MsgStop( "No se pudo abrir " + cDwg, "DWGEngine" ) ) ) ;
      ON RESIZE ( oDwg:Resize( nWidth, nHeight - ALTO_TOOLBAR ) ) ;
      VALID ( oDwg:Close(), .T. )

return nil

//----------------------------------------------------------------------------//

static function DwgAskFile()

   local cFile := cGetFile( "Archivos DWG/DXF|*.dwg;*.dxf|Archivos DWG|*.dwg|Archivos DXF|*.dxf|Todos|*.*", "Abrir dibujo" )

return cFile
