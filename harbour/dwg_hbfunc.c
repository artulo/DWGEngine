/* dwg_hbfunc.c
 *
 * Expone DWGEngine a Harbour/FiveWin, mismo patron que
 * D:\estudio\PDFEngine32\harbour\pdf_hbfunc.c (mismo par de toolchains
 * -- FWH2603 + Harbour + bcc770 -- confirmado por Arturo, ver
 * win32\Build.bat, clonado de D:\estudio\PDFEngine32\win32\Build.bat).
 *
 *   DWG_OPEN( cArchivo )                                    -> pDoc o NIL
 *   DWG_CLOSE( pDoc )                                        -> NIL
 *   DWG_ENTITYCOUNT( pDoc )                                  -> nEntidades
 *   DWG_GETEXTENTS( pDoc )                                   -> { minX, minY, maxX, maxY } o NIL
 *   DWG_RENDERTOHBITMAP( pDoc, nWidth, nHeight, nScale, nOriginX, nOriginY )
 *                                                             -> { hBitmap, nWidth, nHeight } o NIL
 *
 * DWG_OPEN prueba R2000 primero, R12 si eso falla -- cubre la mayoria
 * de archivos reales sin que el llamador tenga que saber la version de
 * antemano.
 *
 * DWG_RENDERTOHBITMAP crea un DIB section (24bpp, top-down -- mismo
 * formato que pdf_bitmap_to_hbitmap en pdf_hbfunc.c), selecciona su
 * propio HDC de memoria, y dibuja DIRECTO ahi via GDI (dwg_render.c),
 * a diferencia de PDFEngine32 que primero renderiza a un buffer de
 * pixeles propio y recien despues lo copia a un HBITMAP -- DWGEngine
 * usa GDI real (lineas/arcos/texto), no un rasterizador propio, asi
 * que no hay buffer intermedio que copiar.
 *
 * nScale/nOriginX/nOriginY: mismo contrato que dwg_render_to_hdc (ver
 * include/dwg_render.h) -- unidades-mundo por pixel, y el punto del
 * mundo que cae en el pixel (0,0) del bitmap resultante. El .prg
 * calcula estos valores (zoom/pan/fit-to-extents) usando
 * DWG_GETEXTENTS como base.
 *
 * IMPORTANTE (confirmado en pdf_hbfunc.c, mismo motivo aca): el
 * HBITMAP se devuelve como NUMERO (hb_arraySetNL), NO como puntero
 * (hb_arraySetPtr) -- FiveWin distingue handles GDI de punteros GDI+
 * por el TIPO Harbour del valor (imgtxtio.prg: "HB_ISNUMERIC(uValue)
 * .AND. ISHBITMAP(uValue)"); un HBITMAP devuelto como puntero hace que
 * FiveWin lo trate como imagen GDI+ y crashee.
 */

#include "hbapi.h"
#include "hbapiitm.h"
#include <windows.h>
#include <stdlib.h>

#include "dwg_types.h"
#include "dwg_document.h"
#include "dwg_file_io.h"
#include "dwg_r2000_reader.h"
#include "dwg_r1314_reader.h"
#include "dwg_r2004_reader.h"
#include "dwg_render.h"

typedef struct
{
    HDWG hDwg;
    int open;
} dwg_hb_doc;

HB_FUNC(DWG_OPEN)
{
    const char *file;
    dwg_hb_doc *h;
    HDWG hDwg;
    DWG_IO_RESULT result;

    file = hb_parc(1);
    if (file == NULL)
    {
        hb_ret();
        return;
    }

    hDwg = dwg_read_dwg_r2000(file, &result);
    if (hDwg == NULL)
        hDwg = dwg_read_dwg_r1314(file, &result);
    if (hDwg == NULL)
        hDwg = dwg_read_dwg_r2004(file, &result); /* AC1024/AC1027/AC1032 (R2010/R2013/R2018) only -- see dwg_r2004_reader.h */
    if (hDwg == NULL)
        hDwg = dwg_read_dwg_r12(file, &result);
    if (hDwg == NULL)
        hDwg = dwg_read_dxf(file, &result); /* DXF (ASCII, no "AC10xx" signature) -- the three binary readers above all reject it cleanly first, cheap since they only look at the first few bytes */

    if (hDwg == NULL)
    {
        hb_ret();
        return;
    }

    h = (dwg_hb_doc *)malloc(sizeof(dwg_hb_doc));
    if (h == NULL)
    {
        dwg_document_destroy(hDwg);
        hb_ret();
        return;
    }

    h->hDwg = hDwg;
    h->open = 1;
    hb_retptr((void *)h);
}

HB_FUNC(DWG_CLOSE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);

    if (h != NULL && h->open)
    {
        dwg_document_destroy(h->hDwg);
        h->open = 0;
        free(h);
    }
}

HB_FUNC(DWG_ENTITYCOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;

    if (h != NULL && h->open)
        count = dwg_document_entity_count(h->hDwg);

    hb_retnl((long)count);
}

HB_FUNC(DWG_GETEXTENTS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double min_x, min_y, max_x, max_y;

    if (h == NULL || !h->open ||
        !dwg_render_get_extents(h->hDwg, &min_x, &min_y, &max_x, &max_y))
    {
        hb_ret();
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(4);
        hb_arraySetND(aResult, 1, min_x);
        hb_arraySetND(aResult, 2, min_y);
        hb_arraySetND(aResult, 3, max_x);
        hb_arraySetND(aResult, 4, max_y);
        hb_itemReturnRelease(aResult);
    }
}

HB_FUNC(DWG_RENDERTOHBITMAP)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    int width = hb_parni(2);
    int height = hb_parni(3);
    double scale = hb_parnd(4);
    double origin_x = hb_parnd(5);
    double origin_y = hb_parnd(6);

    BITMAPINFO bmi;
    HDC screen_dc, mem_dc;
    HBITMAP hBmp, old_bmp;
    void *bits;

    if (h == NULL || !h->open || width <= 0 || height <= 0 || scale <= 0.0)
    {
        hb_ret();
        return;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height; /* negativo = top-down, igual que pdf_bitmap_to_hbitmap */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen_dc = GetDC(NULL);
    hBmp = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen_dc);

    if (hBmp == NULL || bits == NULL)
    {
        hb_ret();
        return;
    }

    mem_dc = CreateCompatibleDC(NULL);
    if (mem_dc == NULL)
    {
        DeleteObject(hBmp);
        hb_ret();
        return;
    }
    old_bmp = (HBITMAP)SelectObject(mem_dc, hBmp);

    dwg_render_to_hdc(h->hDwg, (void *)mem_dc, (long)width, (long)height,
                      scale, origin_x, origin_y);

    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);

    {
        PHB_ITEM aResult = hb_itemArrayNew(3);
        hb_arraySetNL(aResult, 1, (long)(HB_PTRUINT)hBmp);
        hb_arraySetNL(aResult, 2, (long)width);
        hb_arraySetNL(aResult, 3, (long)height);
        hb_itemReturnRelease(aResult);
    }
}
