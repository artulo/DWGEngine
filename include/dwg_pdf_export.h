#ifndef DWG_PDF_EXPORT_H
#define DWG_PDF_EXPORT_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exportar un documento a PDF (pedido de Arturo 2026-10-08, "quiero un
 * proceso para llevar un dwg a pdf").
 *
 * Camino elegido: el MISMO renderizador GDI de pantalla (dwg_render.c)
 * dibuja sobre el contexto de dispositivo de la impresora virtual
 * "Microsoft Print to PDF" de Windows, con StartDoc(lpszOutput=archivo)
 * para que escriba directo al PDF sin dialogo. El resultado es vector
 * (lineas/arcos/texto quedan como geometria, no como imagen) y respeta
 * exactamente lo que el visor dibuja -- capas apagadas/congeladas
 * incluidas -- sin duplicar la geometria de cada entidad en un segundo
 * backend de dibujo. Costo asumido: depende de que esa impresora virtual
 * exista (Windows 10/11 la trae; se puede pasar otro nombre).
 *
 * Auto-ajuste: el dibujo se escala para entrar completo en el area
 * imprimible de la hoja (menos margin_mm por lado), centrado, con la
 * misma relacion de aspecto. Orientacion automatica segun el extents.
 */

#define DWG_PDF_PAPER_DEFAULT 0
#define DWG_PDF_PAPER_A4      9
#define DWG_PDF_PAPER_A3      8
#define DWG_PDF_PAPER_A2      66

#define DWG_PDF_ORIENT_AUTO      0
#define DWG_PDF_ORIENT_PORTRAIT  1
#define DWG_PDF_ORIENT_LANDSCAPE 2

/* Codigos de retorno de dwg_export_pdf */
#define DWG_PDF_OK                0
#define DWG_PDF_ERR_ARGS         (-1)
#define DWG_PDF_ERR_EMPTY        (-2)  /* el documento no tiene nada medible para dibujar */
#define DWG_PDF_ERR_PRINTER      (-3)  /* no se pudo abrir la impresora/crear su DC (no esta instalada?) */
#define DWG_PDF_ERR_STARTDOC     (-4)  /* StartDoc fallo (ruta invalida, archivo en uso...) */
#define DWG_PDF_ERR_TIMEOUT      (-5)  /* el spooler no termino de escribir el archivo a tiempo */

/*
 * printer NULL/"" = "Microsoft Print to PDF". mono != 0 = todo en negro
 * (plot monocromo); 0 = colores ACI sobre papel (el ACI 7 sale negro).
 * margin_mm = margen extra por lado, ademas del margen fisico del
 * dispositivo (la impresora virtual no tiene).
 */
long dwg_export_pdf(HDWG hDwg, const char *pdf_path, const char *printer,
                    int paper, int orientation, int mono, double margin_mm,
                    const double *area);

/*
 * area (pedido de Arturo 2026-10-08, "necesito que al imprimir me
 * permita elegir el area que deseo"): NULL = todo el dibujo (extents,
 * como siempre); si no, 4 doubles {x1,y1,x2,y2} en coordenadas de MUNDO
 * (dos esquinas opuestas, en cualquier orden). Solo ESA ventana se
 * escala para llenar la hoja (misma relacion de aspecto, centrada) y
 * todo lo que cae fuera se recorta -- una ventana de ancho/alto cero da
 * DWG_PDF_ERR_ARGS. La orientacion automatica usa la forma del AREA, no
 * la del dibujo completo.
 */

#ifdef __cplusplus
}
#endif

#endif
