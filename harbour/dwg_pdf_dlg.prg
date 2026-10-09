// dwg_pdf_dlg.prg
//
// ============================================================================
// Dialogo de opciones de "Exportar a PDF" para TDwgViewer (ver
// TDwgViewer:ExportPdf en dwg_viewer.prg) -- pedido de Arturo 2026-10-08,
// "necesito que al imprimir me permita elegir el area que deseo". Recurso
// real EXPORTPDF de dwg_demo.rc (mismo criterio que los demas dialogos).
//
// Devuelve { nArea, nPapel, nOrient, lColor } o NIL si se cancelo:
//   nArea:   1 = todo el dibujo, 2 = vista actual (lo que se ve en
//            pantalla), 3 = elegir en pantalla (el visor pide dos clicks)
//   nPapel:  codigo DMPAPER de Windows (9 = A4, 8 = A3, 66 = A2)
//   nOrient: 0 = automatica, 1 = vertical, 2 = apaisada
//   lColor:  .T. = colores del dibujo, .F. = todo en negro
// aDefault (opcional, mismo formato) precarga lo ultimo que se uso.
// ============================================================================

#include "FiveWin.ch"
#include "dwg_demo_ids.ch"

FUNCTION DwgPdfDlg( aDefault )

   local oDlg, oRadArea, oRadPaper, oRadOrient, oChkColor
   local aPapers := { 9, 8, 66 }
   local nArea   := 1
   local nPaper  := 2        // A3, el formato de los planos de ejemplo
   local nOrient := 1        // automatica
   local lColor  := .T.
   local lOk     := .F.

   if aDefault != NIL
      nArea   := aDefault[ 1 ]
      nPaper  := Max( 1, AScan( aPapers, aDefault[ 2 ] ) )
      nOrient := aDefault[ 3 ] + 1
      lColor  := aDefault[ 4 ]
   endif

   DEFINE DIALOG oDlg RESOURCE "EXPORTPDF"

   REDEFINE RADIO oRadArea VAR nArea ;
      ID ID_PDF_AREA1, ID_PDF_AREA2, ID_PDF_AREA3 OF oDlg

   REDEFINE RADIO oRadPaper VAR nPaper ;
      ID ID_PDF_PAPER1, ID_PDF_PAPER2, ID_PDF_PAPER3 OF oDlg

   REDEFINE RADIO oRadOrient VAR nOrient ;
      ID ID_PDF_ORIENT1, ID_PDF_ORIENT2, ID_PDF_ORIENT3 OF oDlg

   REDEFINE CHECKBOX oChkColor VAR lColor ID ID_PDF_COLOR OF oDlg

   REDEFINE BUTTON ID IDOK OF oDlg ;
      ACTION ( lOk := .T., oDlg:End() )

   REDEFINE BUTTON ID IDCANCEL OF oDlg ;
      ACTION oDlg:End()

   ACTIVATE DIALOG oDlg CENTERED

   if !lOk
      return NIL
   endif

return { nArea, aPapers[ nPaper ], nOrient - 1, lColor }
