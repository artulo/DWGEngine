// dwg_props_dlg.prg
//
// ============================================================================
// Dialogo modal de propiedades para TDwgViewer (ver dwg_viewer.prg) --
// pedido de Arturo 2026-08-26 ("lectura de propiedades", confirmado
// "editable desde ya"). Opera sobre la seleccion ACTUAL del documento
// (Dwg_SelCount/Dwg_SelGet, ver dwg_hbfunc.c) -- no toma la entidad como
// parametro, hay que seleccionar antes (click o ventana, ver
// dwg_viewer.prg) y despues abrir este dialogo.
//
// Alcance de esta vuelta (ver plan): capa/color/linetype editables
// siempre. Los campos GEOMETRICOS (X/Y/Z, radio, angulos, texto) solo se
// muestran/editan si hay EXACTAMENTE UNA entidad seleccionada Y es de un
// tipo con getters/setters de geometria (LINE/CIRCLE/ARC/TEXT/POINT --
// ver dwg_geometry.h/dwg_text.h). Con mas de una entidad seleccionada,
// o con un tipo sin geometria editable todavia (POLYLINE/HATCH/INSERT/
// MTEXT/SOLID/FACE/etc), los campos geometricos se deshabilitan y
// "Aceptar" aplica SOLO capa/color/linetype a TODA la seleccion
// (Dwg_EntitySetProps con lSetGeom=.F., ver dwg_hbfunc.c -- evita pisar
// la geometria real de cada entidad con basura).
// ============================================================================

#include "FiveWin.ch"
#include "dwg_demo_ids.ch"

// nTipo (DWG_ENTITY_TYPE, dwg_types.h) de los unicos tipos con geometria
// editable en esta vuelta -- ver alcance arriba.
#define DWG_T_POINT  1
#define DWG_T_LINE   2
#define DWG_T_CIRCLE 3
#define DWG_T_ARC    4
#define DWG_T_TEXT   7

FUNCTION DwgPropsDlg( oViewer )

   local oDlg
   local pDoc, nCount, pEnt, aProps
   local lSingleGeom := .F.
   local nTipo := 0
   local cCapa, nColor, cLinetype
   local nG1 := 0, nG2 := 0, nG3 := 0, nG4 := 0, nG5 := 0, nG6 := 0
   local cTexto := ""
   local cLbl1 := "", cLbl2 := "", cLbl3 := "", cLbl4 := "", cLbl5 := "", cLbl6 := ""
   local cLblTxt := ""
   local cGeomLabel
   local oGet1, oGet2, oGet3, oGet4, oGet5, oGet6, oGetTexto
   local oGetCapa, oGetColor, oGetLinetype
   local oSayLbl1, oSayLbl2, oSayLbl3, oSayLbl4, oSayLbl5, oSayLbl6, oSayLblTxt
   local i

   if oViewer == NIL .or. oViewer:pDoc == NIL
      MsgStop( "No hay ningun dibujo abierto.", "Propiedades" )
      return nil
   endif
   pDoc := oViewer:pDoc

   nCount := Dwg_SelCount( pDoc )
   if nCount == 0
      MsgStop( "No hay ninguna entidad seleccionada.", "Propiedades" )
      return nil
   endif

   pEnt := Dwg_SelGet( pDoc, 1 )
   aProps := Dwg_EntityGetProps( pDoc, pEnt )
   if aProps == NIL
      return nil
   endif

   nTipo     := aProps[ 1 ]
   cCapa     := aProps[ 2 ]
   nColor    := aProps[ 3 ]
   cLinetype := aProps[ 4 ]

   lSingleGeom := ( nCount == 1 .and. ;
                     ( nTipo == DWG_T_POINT .or. nTipo == DWG_T_LINE .or. ;
                       nTipo == DWG_T_CIRCLE .or. nTipo == DWG_T_ARC .or. ;
                       nTipo == DWG_T_TEXT ) )

   if lSingleGeom
      nG1 := aProps[ 5 ] ; nG2 := aProps[ 6 ] ; nG3 := aProps[ 7 ]
      nG4 := aProps[ 8 ] ; nG5 := aProps[ 9 ] ; nG6 := aProps[ 10 ]
      cTexto := aProps[ 11 ]

      do case
      case nTipo == DWG_T_POINT
         cLbl1 := "X" ; cLbl2 := "Y" ; cLbl3 := "Z"
      case nTipo == DWG_T_LINE
         cLbl1 := "X1" ; cLbl2 := "Y1" ; cLbl3 := "Z1"
         cLbl4 := "X2" ; cLbl5 := "Y2" ; cLbl6 := "Z2"
      case nTipo == DWG_T_CIRCLE
         cLbl1 := "Cx" ; cLbl2 := "Cy" ; cLbl3 := "Cz" ; cLbl4 := "Radio"
      case nTipo == DWG_T_ARC
         cLbl1 := "Cx" ; cLbl2 := "Cy" ; cLbl3 := "Cz"
         cLbl4 := "Radio" ; cLbl5 := "Ang.Ini" ; cLbl6 := "Ang.Fin"
      case nTipo == DWG_T_TEXT
         cLbl1 := "X" ; cLbl2 := "Y" ; cLbl3 := "Z"
         cLbl4 := "Altura" ; cLbl5 := "Angulo"
         cLblTxt := "Texto"
      endcase
   endif

   cGeomLabel := IIF( lSingleGeom, "Geometria:", "(sin geometria editable)" )

   // Recurso real (dwg_demo.rc, pedido de Arturo 2026-08-27) en vez de
   // @ ROW,COL a mano -- DIALOG UNITS, Windows lo escala solo segun el
   // font/DPI real del sistema (la causa de raiz del bug de "GETs
   // gigantes" que antes habia que corregir con un font explicito).
   DEFINE DIALOG oDlg RESOURCE "PROPIEDADES"

   oDlg:cTitle := IIF( nCount == 1, "Propiedades", ;
                        "Propiedades (" + LTrim( Str( nCount ) ) + " entidades)" )

   REDEFINE GET oGetCapa     VAR cCapa     ID ID_PROPS_CAPA     OF oDlg
   REDEFINE GET oGetColor    VAR nColor    ID ID_PROPS_COLOR    PICTURE "999" OF oDlg
   REDEFINE GET oGetLinetype VAR cLinetype ID ID_PROPS_LINETYPE OF oDlg

   REDEFINE SAY ID ID_PROPS_GEOMLABEL VAR cGeomLabel OF oDlg

   REDEFINE SAY oSayLbl1 ID ID_PROPS_LBL1 VAR cLbl1 OF oDlg
   REDEFINE GET oGet1    VAR nG1 ID ID_PROPS_G1 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLbl2 ID ID_PROPS_LBL2 VAR cLbl2 OF oDlg
   REDEFINE GET oGet2    VAR nG2 ID ID_PROPS_G2 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLbl3 ID ID_PROPS_LBL3 VAR cLbl3 OF oDlg
   REDEFINE GET oGet3    VAR nG3 ID ID_PROPS_G3 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLbl4 ID ID_PROPS_LBL4 VAR cLbl4 OF oDlg
   REDEFINE GET oGet4    VAR nG4 ID ID_PROPS_G4 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLbl5 ID ID_PROPS_LBL5 VAR cLbl5 OF oDlg
   REDEFINE GET oGet5    VAR nG5 ID ID_PROPS_G5 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLbl6 ID ID_PROPS_LBL6 VAR cLbl6 OF oDlg
   REDEFINE GET oGet6    VAR nG6 ID ID_PROPS_G6 PICTURE "9999999.9999" OF oDlg

   REDEFINE SAY oSayLblTxt ID ID_PROPS_LBLTXT VAR cLblTxt OF oDlg
   REDEFINE GET oGetTexto  VAR cTexto ID ID_PROPS_TEXTO OF oDlg

   // El par Texto/oGetTexto solo tiene sentido para TEXT -- a diferencia
   // de antes (donde ni se construia), un recurso ya trae los controles
   // siempre: se ocultan en vez de saltarse la construccion.
   if !( nTipo == DWG_T_TEXT .and. lSingleGeom )
      oSayLblTxt:Hide()
      oGetTexto:Hide()
   endif

   if !lSingleGeom
      oGet1:Disable() ; oGet2:Disable() ; oGet3:Disable()
      oGet4:Disable() ; oGet5:Disable() ; oGet6:Disable()
   else
      if Empty( cLbl4 ) ; oGet4:Disable() ; endif
      if Empty( cLbl5 ) ; oGet5:Disable() ; endif
      if Empty( cLbl6 ) ; oGet6:Disable() ; endif
   endif

   REDEFINE BUTTON ID IDOK OF oDlg ;
      ACTION ( DwgPropsDlgApply( pDoc, nCount, cCapa, nColor, cLinetype, ;
                                  lSingleGeom, nG1, nG2, nG3, nG4, nG5, nG6, cTexto ), ;
               oViewer:Render(), ;
               oDlg:End() )

   REDEFINE BUTTON ID IDCANCEL OF oDlg ;
      ACTION oDlg:End()

   ACTIVATE DIALOG oDlg CENTERED

return nil

//----------------------------------------------------------------------------//

// lSingleGeom .T.: una sola entidad, tipo con geometria editable --
// aplica capa/color/linetype/geometria a esa unica entidad (Dwg_SelGet(
// pDoc,1), mismo pEnt que abrio el dialogo). lSingleGeom .F.: aplica
// SOLO capa/color/linetype (lSetGeom=.F. en Dwg_EntitySetProps) a TODA
// la seleccion -- ver doc-comment de arriba para el porque.
STATIC FUNCTION DwgPropsDlgApply( pDoc, nCount, cCapa, nColor, cLinetype, ;
                                   lSingleGeom, nG1, nG2, nG3, nG4, nG5, nG6, cTexto )

   local i, pE

   if lSingleGeom
      Dwg_EntitySetProps( pDoc, Dwg_SelGet( pDoc, 1 ), cCapa, nColor, cLinetype, ;
                          nG1, nG2, nG3, nG4, nG5, nG6, cTexto, .T. )
   else
      for i := 1 to nCount
         pE := Dwg_SelGet( pDoc, i )
         if pE != NIL
            Dwg_EntitySetProps( pDoc, pE, cCapa, nColor, cLinetype, ;
                                0, 0, 0, 0, 0, 0, "", .F. )
         endif
      next
   endif

return nil
