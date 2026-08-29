// dwg_layers_dlg.prg
//
// ============================================================================
// Dialogo modal de capas para TDwgViewer (ver dwg_viewer.prg) -- pedido de
// Arturo 2026-08-26 ("aisla layer"). Lista todas las capas del documento y
// deja "aislar" (mostrar solo las elegidas) o volver a mostrar todas.
//
// Simplificacion deliberada: en vez de un checkbox real por fila (FiveWin
// no lo da gratis en un TListBox simple sin owner-draw -- mas superficie
// de la que esta vuelta necesita), se usa un LISTBOX de seleccion
// MULTIPLE: resaltado = capa visible, sin resaltar = capa que quedaria
// apagada al aislar. Funcionalmente equivalente para el flujo pedido
// ("aislar capa"), mas simple de construir y de usar (click/ctrl-click/
// shift-click, el mismo gesto que cualquier multi-seleccion de Windows).
// ============================================================================

#include "FiveWin.ch"
#include "dwg_demo_ids.ch"

FUNCTION DwgLayersDlg( oViewer )

   local oDlg, oLbx
   local pDoc, aNombres, aVisibles, aFlags
   local cCapa, i

   if oViewer == NIL .or. oViewer:pDoc == NIL
      MsgStop( "No hay ningun dibujo abierto.", "Capas" )
      return nil
   endif
   pDoc := oViewer:pDoc

   aNombres := Dwg_LayerList( pDoc )
   if aNombres == NIL
      aNombres := {}
   endif

   aVisibles := {}
   for i := 1 to Len( aNombres )
      aFlags := Dwg_LayerGetFlags( pDoc, aNombres[ i ] )
      if aFlags != NIL .and. !aFlags[ 1 ]      // aFlags[1] = lOff
         AAdd( aVisibles, i )
      endif
   next

   // Recurso real (dwg_demo.rc, pedido de Arturo 2026-08-27) en vez de
   // @ ROW,COL a mano -- DIALOG UNITS, Windows lo escala solo segun el
   // font/DPI real del sistema (la causa de raiz del bug de "GETs
   // gigantes" que antes habia que corregir con un font explicito).
   // LBS_MULTIPLESEL ya viene horneado en el LISTBOX del .rc -- un
   // recurso cargado no acepta la clausula MULTIPLE del lado .prg.
   DEFINE DIALOG oDlg RESOURCE "CAPAS"

   REDEFINE LISTBOX oLbx VAR cCapa ITEMS aNombres ID ID_CAPAS_LIST OF oDlg

   oLbx:SetSelItems( aVisibles )

   REDEFINE BUTTON ID ID_CAPAS_ISOLATE OF oDlg ;
      ACTION ( DwgLayersDlgIsolate( pDoc, aNombres, oLbx:GetSelItems() ), ;
               oViewer:Render() )

   REDEFINE BUTTON ID ID_CAPAS_SHOWALL OF oDlg ;
      ACTION ( DwgLayersDlgShowAll( pDoc, aNombres ), ;
               oLbx:SetSelItems( DwgLayersDlgAllIndexes( aNombres ) ), ;
               oLbx:Refresh(), ;
               oViewer:Render() )

   REDEFINE BUTTON ID IDCANCEL OF oDlg ;
      ACTION oDlg:End()

   ACTIVATE DIALOG oDlg CENTERED

return nil

//----------------------------------------------------------------------------//

// Apaga toda capa cuyo indice (en aNombres) no este en aSelIdx, prende
// las que si -- ver Dwg_LayerIsolate en dwg_hbfunc.c.
STATIC FUNCTION DwgLayersDlgIsolate( pDoc, aNombres, aSelIdx )

   local aVisNames := {}
   local i

   for i := 1 to Len( aSelIdx )
      AAdd( aVisNames, aNombres[ aSelIdx[ i ] ] )
   next

   Dwg_LayerIsolate( pDoc, aVisNames )

return nil

//----------------------------------------------------------------------------//

STATIC FUNCTION DwgLayersDlgShowAll( pDoc, aNombres )

   local i

   for i := 1 to Len( aNombres )
      Dwg_LayerSetOff( pDoc, aNombres[ i ], .F. )
   next

return nil

//----------------------------------------------------------------------------//

STATIC FUNCTION DwgLayersDlgAllIndexes( aNombres )

   local aIdx := {}
   local i

   for i := 1 to Len( aNombres )
      AAdd( aIdx, i )
   next

return aIdx
