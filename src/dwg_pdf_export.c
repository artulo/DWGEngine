#include <windows.h>
#include <winspool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dwg_pdf_export.h"
#include "dwg_render.h"

/* Ver el doc-comment de dwg_pdf_export.h para el porque del camino
   elegido (renderizador GDI sobre el DC de "Microsoft Print to PDF"). */

#define DWG_PDF_DEFAULT_PRINTER "Microsoft Print to PDF"

/* Ancho de lapiz de impresion: 0.25 mm, el trazo "fino" estandar de un
   plano -- en pixeles de dispositivo segun los dpi reales, nunca menos
   de 1. */
#define DWG_PDF_PEN_MM 0.25

/* DEVMODE de la impresora con papel/orientacion pedidos. Devuelve un
   bloque malloc'eado (el llamador hace free) o NULL. */
static DEVMODEA *build_devmode(const char *printer, int paper, int orient_dm)
{
    HANDLE hPrinter = NULL;
    LONG size;
    DEVMODEA *dm;
    PRINTER_DEFAULTSA pd;

    memset(&pd, 0, sizeof(pd));
    if (!OpenPrinterA((LPSTR)printer, &hPrinter, &pd) || hPrinter == NULL)
        return NULL;

    size = DocumentPropertiesA(NULL, hPrinter, (LPSTR)printer, NULL, NULL, 0);
    if (size <= 0)
    {
        ClosePrinter(hPrinter);
        return NULL;
    }

    dm = (DEVMODEA *)calloc(1, (size_t)size);
    if (dm == NULL)
    {
        ClosePrinter(hPrinter);
        return NULL;
    }

    if (DocumentPropertiesA(NULL, hPrinter, (LPSTR)printer, dm, NULL, DM_OUT_BUFFER) != IDOK)
    {
        free(dm);
        ClosePrinter(hPrinter);
        return NULL;
    }

    if (orient_dm != 0)
    {
        dm->dmFields |= DM_ORIENTATION;
        dm->dmOrientation = (short)orient_dm;
    }
    if (paper != DWG_PDF_PAPER_DEFAULT)
    {
        dm->dmFields |= DM_PAPERSIZE;
        dm->dmPaperSize = (short)paper;
    }

    /* que el driver valide/normalice lo que se acaba de pedir */
    DocumentPropertiesA(NULL, hPrinter, (LPSTR)printer, dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER);

    ClosePrinter(hPrinter);
    return dm;
}

/* El spooler de "Microsoft Print to PDF" escribe el archivo DESPUES de
   que EndDoc ya devolvio (confirmado midiendolo: ~9 segundos para un
   plano de arquitectura real) -- sin esperar, quien llama ve "OK" con un
   archivo que todavia no existe. Espera a que aparezca y a que su
   tamaño deje de crecer durante ~600 ms seguidos. Devuelve 1 si quedo
   completo, 0 si vencio el tiempo. */
static int wait_for_stable_file(const char *path, DWORD timeout_ms)
{
    DWORD start = GetTickCount();
    DWORD last_size = 0xFFFFFFFFUL;
    DWORD stable_since = 0;

    while (GetTickCount() - start < timeout_ms)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;

        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 0)
        {
            if (fa.nFileSizeLow == last_size)
            {
                if (stable_since != 0 && GetTickCount() - stable_since >= 600)
                    return 1;
            }
            else
            {
                last_size = fa.nFileSizeLow;
                stable_since = GetTickCount();
            }
        }
        Sleep(100);
    }
    return 0;
}

long dwg_export_pdf(HDWG hDwg, const char *pdf_path, const char *printer,
                    int paper, int orientation, int mono, double margin_mm,
                    const double *area)
{
    double minx, miny, maxx, maxy;
    HRGN clip = NULL;
    double w, h, scale, avail_w, avail_h, draw_w_px, draw_h_px;
    double origin_x, origin_y;
    int orient_dm, margin_px, pen_px, dpi;
    long horz, vert;
    DEVMODEA *dm;
    HDC hdc;
    DOCINFOA di;

    if (hDwg == NULL || pdf_path == NULL || pdf_path[0] == '\0')
        return DWG_PDF_ERR_ARGS;
    if (printer == NULL || printer[0] == '\0')
        printer = DWG_PDF_DEFAULT_PRINTER;

    if (area != NULL)
    {
        /* area elegida por el usuario (ventana en coordenadas de MUNDO,
           esquinas en cualquier orden) en vez del extents completo */
        minx = (area[0] < area[2]) ? area[0] : area[2];
        maxx = (area[0] < area[2]) ? area[2] : area[0];
        miny = (area[1] < area[3]) ? area[1] : area[3];
        maxy = (area[1] < area[3]) ? area[3] : area[1];
        if (maxx - minx <= 0.0 || maxy - miny <= 0.0)
            return DWG_PDF_ERR_ARGS;
    }
    else if (!dwg_render_get_extents(hDwg, &minx, &miny, &maxx, &maxy))
        return DWG_PDF_ERR_EMPTY;

    w = maxx - minx;
    h = maxy - miny;

    if (orientation == DWG_PDF_ORIENT_PORTRAIT)
        orient_dm = DMORIENT_PORTRAIT;
    else if (orientation == DWG_PDF_ORIENT_LANDSCAPE)
        orient_dm = DMORIENT_LANDSCAPE;
    else
        orient_dm = (w >= h) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;

    dm = build_devmode(printer, paper, orient_dm);
    if (dm == NULL)
        return DWG_PDF_ERR_PRINTER;

    hdc = CreateDCA("WINSPOOL", printer, NULL, dm);
    free(dm);
    if (hdc == NULL)
        return DWG_PDF_ERR_PRINTER;

    horz = GetDeviceCaps(hdc, HORZRES);
    vert = GetDeviceCaps(hdc, VERTRES);
    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    if (horz <= 0 || vert <= 0 || dpi <= 0)
    {
        DeleteDC(hdc);
        return DWG_PDF_ERR_PRINTER;
    }

    margin_px = (int)(margin_mm / 25.4 * dpi + 0.5);
    if (margin_px < 0)
        margin_px = 0;
    avail_w = (double)(horz - 2 * margin_px);
    avail_h = (double)(vert - 2 * margin_px);
    if (avail_w < 1.0) avail_w = 1.0;
    if (avail_h < 1.0) avail_h = 1.0;

    /* un dibujo de un solo punto o una linea perfectamente horizontal/
       vertical tiene w o h en 0 -- escala por el lado que SI tiene
       tamaño; si ambos son 0, un tamaño arbitrario (se ve como un punto
       centrado, no divide por cero) */
    if (w <= 0.0 && h <= 0.0)
        scale = 1.0 / avail_w;
    else
    {
        double sw = (w > 0.0) ? w / avail_w : 0.0;
        double sh = (h > 0.0) ? h / avail_h : 0.0;
        scale = (sw > sh) ? sw : sh;
    }

    draw_w_px = w / scale;
    draw_h_px = h / scale;

    /* centrado en la hoja: pixel_x = (wx-origin_x)/scale -> el borde
       izquierdo del dibujo cae en (horz-draw_w)/2; pixel_y = (origin_y-wy)
       /scale -> el borde SUPERIOR (maxy) en (vert-draw_h)/2 */
    origin_x = minx - ((double)horz - draw_w_px) / 2.0 * scale;
    origin_y = maxy + ((double)vert - draw_h_px) / 2.0 * scale;

    pen_px = (int)(DWG_PDF_PEN_MM / 25.4 * dpi + 0.5);
    if (pen_px < 1)
        pen_px = 1;

    /* borrar un PDF previo de la misma ruta: asi "el archivo aparecio y
       dejo de crecer" (wait_for_stable_file) se refiere SIEMPRE al
       nuevo, nunca al viejo. Si no se puede borrar (abierto en un
       visor) es un error claro, no un PDF viejo disfrazado de nuevo. */
    if (GetFileAttributesA(pdf_path) != INVALID_FILE_ATTRIBUTES && !DeleteFileA(pdf_path))
    {
        DeleteDC(hdc);
        return DWG_PDF_ERR_STARTDOC;
    }

    memset(&di, 0, sizeof(di));
    di.cbSize = (int)sizeof(di);
    di.lpszDocName = "DWGEngine";
    di.lpszOutput = pdf_path;

    if (StartDocA(hdc, &di) <= 0)
    {
        DeleteDC(hdc);
        return DWG_PDF_ERR_STARTDOC;
    }

    StartPage(hdc);

    /* con un area elegida, TODO lo que cae fuera de ella no se dibuja:
       el renderizador pinta cada entidad entera, sin recortar a ninguna
       ventana, asi que se recorta el DC a la caja del area ya mapeada a
       la hoja (el borde izquierdo del area cae en (horz-draw_w)/2, el
       superior en (vert-draw_h)/2, ver origin_x/origin_y arriba) */
    if (area != NULL)
    {
        int cl = (int)(((double)horz - draw_w_px) / 2.0 + 0.5);
        int ct = (int)(((double)vert - draw_h_px) / 2.0 + 0.5);

        clip = CreateRectRgn(cl, ct, cl + (int)(draw_w_px + 0.5), ct + (int)(draw_h_px + 0.5));
        if (clip != NULL)
            SelectClipRgn(hdc, clip);
    }

    dwg_render_to_hdc_ex(hDwg, hdc, horz, vert, scale, origin_x, origin_y, NULL,
                         DWG_RENDER_FLAG_PRINT | (mono ? DWG_RENDER_FLAG_MONO : 0UL),
                         pen_px);
    if (clip != NULL)
    {
        SelectClipRgn(hdc, NULL);
        DeleteObject(clip);
    }
    EndPage(hdc);
    EndDoc(hdc);
    DeleteDC(hdc);

    if (!wait_for_stable_file(pdf_path, 120000UL))
        return DWG_PDF_ERR_TIMEOUT;

    return DWG_PDF_OK;
}
