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
 * --- Document info ---
 *   DWG_LAYERCOUNT( pDoc )                                   -> nLayers
 *   DWG_BLOCKCOUNT( pDoc )                                   -> nBlocks
 *   DWG_STYLECOUNT( pDoc )                                   -> nStyles
 *   DWG_LINETYPECOUNT( pDoc )                                -> nLinetypes
 *   DWG_LAYERS( pDoc )                                       -> { {name, color, linetype, flags}, ... }
 *   DWG_BLOCKS( pDoc )                                       -> { {name, entity_count}, ... }
 *   DWG_STYLES( pDoc )                                       -> { {name, font, height, width_factor}, ... }
 *
 * --- Entity iteration ---
 *   DWG_FIRSTENTITY( pDoc )                                  -> pEntity o NIL
 *   DWG_NEXTENTITY( pDoc, pEntity )                          -> pEntity o NIL
 *   DWG_GETENTITYBYID( pDoc, nId )                           -> pEntity o NIL
 *
 * --- Entity properties ---
 *   DWG_ENTITYTYPE( pEntity )                                -> nType (ver DWG_ENTITY_TYPE enum)
 *   DWG_ENTITYTYPESTR( pEntity )                             -> cType ("LINE", "CIRCLE", etc.)
 *   DWG_ENTITYLAYER( pEntity )                               -> cLayerName
 *   DWG_ENTITYCOLOR( pEntity )                               -> nColor (ACI index)
 *   DWG_ENTITYSETCOLOR( pEntity, nColor )                    -> NIL
 *   DWG_ENTITYSETLAYER( pEntity, cLayerName )                -> NIL
 *   DWG_ENTITYID( pEntity )                                  -> nId
 *
 * --- Geometry access ---
 *   DWG_GETLINEGEOMETRY( pEntity )                            -> { x1,y1,z1, x2,y2,z2 }
 *   DWG_GETCIRCLEGEOMETRY( pEntity )                          -> { cx,cy,cz, radius }
 *   DWG_GETARCGEOMETRY( pEntity )                             -> { cx,cy,cz, radius, start_angle, end_angle }
 *   DWG_GETELLIPSEGEOMETRY( pEntity )                         -> { cx,cy,cz, max,max,mz, axis_ratio, start_param, end_param }
 *   DWG_GETPOINTGEOMETRY( pEntity )                           -> { x,y,z }
 *   DWG_GETTEXTGEOMETRY( pEntity )                            -> { x,y,z, text, height, angle }
 *   DWG_GETMTEXTGEOMETRY( pEntity )                           -> { x,y,z, text, height, rect_width }
 *   DWG_GETINSERTGEOMETRY( pEntity )                          -> { x,y,z, block_name, scale_x, scale_y, scale_z, angle }
 *   DWG_GETSOLIDGEOMETRY( pEntity )                           -> { x1,y1,z1, x2,y2,z2, x3,y3,z3, x4,y4,z4 }
 *   DWG_GETPOLYLINEVERTICES( pEntity )                        -> { {x,y,z,bulge}, ... }
 *   DWG_POLYLINEISCLOSED( pEntity )                           -> lClosed
 *
 * --- Geometry creation ---
 *   DWG_ADDLINE( pDoc, x1,y1,z1, x2,y2,z2 )                 -> pEntity
 *   DWG_ADDCIRCLE( pDoc, cx,cy,cz, radius )                  -> pEntity
 *   DWG_ADDARC( pDoc, cx,cy,cz, radius, start_angle, end_angle ) -> pEntity
 *   DWG_ADDPOINT( pDoc, x, y, z )                            -> pEntity
 *   DWG_ADDTEXT( pDoc, x, y, z, height, angle, cText )       -> pEntity
 *   DWG_ADDMTEXT( pDoc, x, y, z, height, rect_width, cText ) -> pEntity
 *   DWG_ADDPOLYLINE( pDoc )                                  -> pEntity
 *   DWG_ADDPOLYLINEVERTEX( pEntity, x, y, z, bulge )        -> NIL
 *   DWG_CLOSEPOLYLINE( pEntity, lClosed )                    -> NIL
 *
 * --- Transformations ---
 *   DWG_MOVE( pEntity, dx, dy, dz )                          -> NIL
 *   DWG_ROTATE( pEntity, cx, cy, cz, angle_degrees )        -> NIL
 *   DWG_SCALE( pEntity, cx, cy, cz, factor )                -> NIL
 *   DWG_MIRROR( pEntity, x1,y1, x2,y2 )                     -> NIL
 *   DWG_COPY( pDoc, pEntity )                                -> pNewEntity o NIL
 *   DWG_EXPLODE( pDoc, pEntity )                             -> nNewEntities
 *
 * --- Selection ---
 *   DWG_SELECTPOINT( pDoc, px, py, tolerance )               -> nSelected
 *   DWG_SELECTWINDOW( pDoc, x1,y1, x2,y2, lCrossing )       -> nSelected
 *   DWG_SELECTLAYER( pDoc, cLayerName )                      -> nSelected
 *   DWG_SELCLEAR( pDoc )                                     -> NIL
 *   DWG_SELCOUNT( pDoc )                                     -> nCount
 *   DWG_SELGET( pDoc, nIndex )                               -> pEntity o NIL
 *   DWG_SELMOVE( pDoc, dx, dy, dz )                          -> NIL
 *   DWG_SELROTATE( pDoc, cx, cy, cz, angle_degrees )        -> NIL
 *   DWG_SELSCALE( pDoc, cx, cy, cz, factor )                -> NIL
 *   DWG_SELMIRROR( pDoc, x1,y1, x2,y2 )                     -> NIL
 *   DWG_SELERASE( pDoc )                                     -> nErased
 *   DWG_SELCOPY( pDoc )                                      -> nCopied
 *   DWG_SELEXPLODE( pDoc )                                   -> nExploded
 *   DWG_SELJOIN( pDoc, tolerance )                           -> nJoined
 */

#include "hbapi.h"
#include "hbapiitm.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dwg_types.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_file_io.h"
#include "dwg_r2000_reader.h"
#include "dwg_r1314_reader.h"
#include "dwg_r2004_reader.h"
#include "dwg_render.h"
#include "dwg_geometry.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_insert.h"
#include "dwg_polyline.h"
#include "dwg_vertex.h"
#include "dwg_solid.h"
#include "dwg_hatch.h"
#include "dwg_leader.h"
#include "dwg_transform.h"
#include "dwg_selection.h"
#include "dwg_layer.h"
#include "dwg_block.h"
#include "dwg_style.h"
#include "dwg_linetype.h"

/* --- entity type name table --- */
static const char *dwg_entity_type_name(DWG_ENTITY_TYPE t)
{
    switch (t)
    {
        case DWG_ENTITY_POINT:     return "POINT";
        case DWG_ENTITY_LINE:      return "LINE";
        case DWG_ENTITY_CIRCLE:    return "CIRCLE";
        case DWG_ENTITY_ARC:       return "ARC";
        case DWG_ENTITY_POLYLINE:  return "POLYLINE";
        case DWG_ENTITY_VERTEX:    return "VERTEX";
        case DWG_ENTITY_TEXT:      return "TEXT";
        case DWG_ENTITY_MTEXT:     return "MTEXT";
        case DWG_ENTITY_ELLIPSE:   return "ELLIPSE";
        case DWG_ENTITY_RAY:       return "RAY";
        case DWG_ENTITY_XLINE:     return "XLINE";
        case DWG_ENTITY_INSERT:    return "INSERT";
        case DWG_ENTITY_BLOCK:     return "BLOCK";
        case DWG_ENTITY_HATCH:     return "HATCH";
        case DWG_ENTITY_DIMENSION: return "DIMENSION";
        case DWG_ENTITY_SOLID:     return "SOLID";
        case DWG_ENTITY_FACE:      return "3DFACE";
        case DWG_ENTITY_LEADER:    return "LEADER";
        default:                   return "UNKNOWN";
    }
}

static void dwg_debug_log(const char *msg)
{
    FILE *f = fopen("debug_dwg.log", "a");
    if (f != NULL)
    {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

static void dwg_debug_log_path(const char *tag, const char *path)
{
    FILE *f = fopen("debug_dwg.log", "a");
    if (f != NULL)
    {
        fprintf(f, "[%s] path=%s\n", tag, path ? path : "(NULL)");
        fclose(f);
    }
}

typedef struct
{
    HDWG hDwg;
    int open;
} dwg_hb_doc;

/* ============================================================================
 * DWG_OPEN / DWG_CLOSE / DWG_ENTITYCOUNT / DWG_GETEXTENTS / DWG_RENDERTOHBITMAP
 * ============================================================================ */

HB_FUNC(DWG_OPEN)
{
    const char *file;
    dwg_hb_doc *h;
    HDWG hDwg;
    DWG_IO_RESULT result;

    file = hb_parc(1);
    if (file == NULL)
    {
        dwg_debug_log("[DWG_OPEN] file param is NULL, returning");
        hb_ret();
        return;
    }

    dwg_debug_log_path("DWG_OPEN", file);

    hDwg = dwg_read_dwg_r2000(file, &result);
    dwg_debug_log("[DWG_OPEN] R2000 reader done");
    if (hDwg == NULL)
    {
        hDwg = dwg_read_dwg_r1314(file, &result);
        dwg_debug_log("[DWG_OPEN] R1314 reader done");
    }
    if (hDwg == NULL)
    {
        hDwg = dwg_read_dwg_r2004(file, &result);
        dwg_debug_log("[DWG_OPEN] R2004 reader done");
    }
    if (hDwg == NULL)
    {
        hDwg = dwg_read_dwg_r12(file, &result);
        dwg_debug_log("[DWG_OPEN] R12 reader done");
    }
    if (hDwg == NULL)
    {
        hDwg = dwg_read_dxf(file, &result);
        dwg_debug_log("[DWG_OPEN] DXF reader done");
    }

    if (hDwg == NULL)
    {
        dwg_debug_log("[DWG_OPEN] All readers failed, returning NIL");
        hb_ret();
        return;
    }

    dwg_debug_log("[DWG_OPEN] Document opened successfully");

    h = (dwg_hb_doc *)malloc(sizeof(dwg_hb_doc));
    if (h == NULL)
    {
        dwg_debug_log("[DWG_OPEN] malloc(dwgbdoc) failed");
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
    dwg_debug_log("[DWG_CLOSE] called");

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

    dwg_debug_log("[DWG_GETEXTENTS] called");

    if (h == NULL || !h->open)
    {
        dwg_debug_log("[DWG_GETEXTENTS] bad handle or doc closed, returning NIL");
        hb_ret();
        return;
    }

    if (!dwg_render_get_extents(h->hDwg, &min_x, &min_y, &max_x, &max_y))
    {
        dwg_debug_log("[DWG_GETEXTENTS] dwg_render_get_extents returned 0 (no entities), returning NIL");
        hb_ret();
        return;
    }

    {
        char buf[256];
        sprintf(buf, "[DWG_GETEXTENTS] extents=(%g,%g)-(%g,%g)", min_x, min_y, max_x, max_y);
        dwg_debug_log(buf);
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
    char buf[256];

    BITMAPINFO bmi;
    HDC screen_dc, mem_dc;
    HBITMAP hBmp, old_bmp;
    void *bits;

    sprintf(buf, "[DWG_RENDERTOHBITMAP] called: w=%d h=%d scale=%g ox=%g oy=%g", width, height, scale, origin_x, origin_y);
    dwg_debug_log(buf);

    if (h == NULL || !h->open || width <= 0 || height <= 0 || scale <= 0.0)
    {
        dwg_debug_log("[DWG_RENDERTOHBITMAP] bad params, returning NIL");
        hb_ret();
        return;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;
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

    dwg_debug_log("[DWG_RENDERTOHBITMAP] dwg_render_to_hdc completed");

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

/* ============================================================================
 * DOCUMENT INFO
 * ============================================================================ */

HB_FUNC(DWG_LAYERCOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_document_layer_count(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_BLOCKCOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_document_block_count(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_STYLECOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_document_style_count(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_LINETYPECOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_document_linetype_count(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_LAYERS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HLAYER layer;
    unsigned long count, i;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    count = dwg_document_layer_count(h->hDwg);
    if (count == 0)
    {
        hb_reta(0);
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(count);
        layer = dwg_document_first_layer(h->hDwg);
        i = 0;
        while (layer != NULL && i < count)
        {
            PHB_ITEM aRow = hb_itemArrayNew(4);
            hb_arraySetC(aRow, 1, dwg_layer_get_name(layer));
            hb_arraySetNL(aRow, 2, (long)dwg_layer_get_color(layer));
            hb_arraySetC(aRow, 3, dwg_layer_get_linetype(layer));
            hb_arraySetNL(aRow, 4, (long)dwg_layer_get_flags(layer));
            hb_arraySet(aResult, i + 1, aRow);
            hb_itemRelease(aRow);
            layer = dwg_document_next_layer(layer);
            i++;
        }
        hb_itemReturnRelease(aResult);
    }
}

HB_FUNC(DWG_BLOCKS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HBLOCK block;
    unsigned long count, i;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    count = dwg_document_block_count(h->hDwg);
    if (count == 0)
    {
        hb_reta(0);
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(count);
        block = dwg_document_first_block(h->hDwg);
        i = 0;
        while (block != NULL && i < count)
        {
            PHB_ITEM aRow = hb_itemArrayNew(2);
            hb_arraySetC(aRow, 1, dwg_block_get_name(block));
            hb_arraySetNL(aRow, 2, (long)dwg_block_entity_count(block));
            hb_arraySet(aResult, i + 1, aRow);
            hb_itemRelease(aRow);
            block = dwg_document_next_block(block);
            i++;
        }
        hb_itemReturnRelease(aResult);
    }
}

HB_FUNC(DWG_STYLES)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HSTYLE style;
    unsigned long count, i;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    count = dwg_document_style_count(h->hDwg);
    if (count == 0)
    {
        hb_reta(0);
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(count);
        style = dwg_document_first_style(h->hDwg);
        i = 0;
        while (style != NULL && i < count)
        {
            PHB_ITEM aRow = hb_itemArrayNew(4);
            hb_arraySetC(aRow, 1, dwg_style_get_name(style));
            hb_arraySetC(aRow, 2, dwg_style_get_font(style));
            hb_arraySetND(aRow, 3, dwg_style_get_height(style));
            hb_arraySetND(aRow, 4, dwg_style_get_width_factor(style));
            hb_arraySet(aResult, i + 1, aRow);
            hb_itemRelease(aRow);
            style = dwg_document_next_style(style);
            i++;
        }
        hb_itemReturnRelease(aResult);
    }
}

/* ============================================================================
 * ENTITY ITERATION
 * ============================================================================ */

HB_FUNC(DWG_FIRSTENTITY)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY ent;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    ent = dwg_document_first_entity(h->hDwg);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_NEXTENTITY)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY ent = (HENTITY)hb_parptr(2);
    HENTITY next;

    if (h == NULL || !h->open || ent == NULL)
    {
        hb_ret();
        return;
    }

    next = dwg_document_next_entity(ent);
    if (next == NULL)
        hb_ret();
    else
        hb_retptr((void *)next);
}

HB_FUNC(DWG_GETENTITYBYID)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    DWG_ID id = (DWG_ID)hb_parnl(2);
    HENTITY ent;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    ent = dwg_document_get_entity_by_id(h->hDwg, id);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

/* ============================================================================
 * ENTITY PROPERTIES
 * ============================================================================ */

HB_FUNC(DWG_ENTITYTYPE)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retni(0);
    else
        hb_retni((int)dwg_entity_get_type(ent));
}

HB_FUNC(DWG_ENTITYTYPESTR)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retc("UNKNOWN");
    else
        hb_retc(dwg_entity_type_name(dwg_entity_get_type(ent)));
}

HB_FUNC(DWG_ENTITYLAYER)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retc("");
    else
        hb_retc(dwg_entity_get_layer(ent));
}

HB_FUNC(DWG_ENTITYCOLOR)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retni(0);
    else
        hb_retni((int)dwg_entity_get_color(ent));
}

HB_FUNC(DWG_ENTITYSETCOLOR)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    int color = hb_parni(2);
    if (ent != NULL)
        dwg_entity_put_color(ent, (unsigned short)color);
}

HB_FUNC(DWG_ENTITYSETLAYER)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    const char *name = hb_parc(2);
    if (ent != NULL && name != NULL)
        dwg_entity_put_layer(ent, name);
}

HB_FUNC(DWG_ENTITYID)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retnl(0);
    else
        hb_retnl((long)dwg_entity_get_id(ent));
}

/* ============================================================================
 * GEOMETRY ACCESS
 * ============================================================================ */

HB_FUNC(DWG_GETLINEGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_LINE3D *line;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_LINE)
    {
        hb_ret();
        return;
    }
    line = (DWG_LINE3D *)((DWG_ENTITY *)ent)->geometry;
    if (line == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(6);
    hb_arraySetND(aResult, 1, line->start.x);
    hb_arraySetND(aResult, 2, line->start.y);
    hb_arraySetND(aResult, 3, line->start.z);
    hb_arraySetND(aResult, 4, line->end.x);
    hb_arraySetND(aResult, 5, line->end.y);
    hb_arraySetND(aResult, 6, line->end.z);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETCIRCLEGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_CIRCLE3D *circ;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_CIRCLE)
    {
        hb_ret();
        return;
    }
    circ = (DWG_CIRCLE3D *)((DWG_ENTITY *)ent)->geometry;
    if (circ == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(4);
    hb_arraySetND(aResult, 1, circ->center.x);
    hb_arraySetND(aResult, 2, circ->center.y);
    hb_arraySetND(aResult, 3, circ->center.z);
    hb_arraySetND(aResult, 4, circ->radius);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETARCGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_ARC3D *arc;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_ARC)
    {
        hb_ret();
        return;
    }
    arc = (DWG_ARC3D *)((DWG_ENTITY *)ent)->geometry;
    if (arc == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(6);
    hb_arraySetND(aResult, 1, arc->center.x);
    hb_arraySetND(aResult, 2, arc->center.y);
    hb_arraySetND(aResult, 3, arc->center.z);
    hb_arraySetND(aResult, 4, arc->radius);
    hb_arraySetND(aResult, 5, arc->start_angle);
    hb_arraySetND(aResult, 6, arc->end_angle);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETELLIPSEGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_ELLIPSE3D *ell;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_ELLIPSE)
    {
        hb_ret();
        return;
    }
    ell = (DWG_ELLIPSE3D *)((DWG_ENTITY *)ent)->geometry;
    if (ell == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(9);
    hb_arraySetND(aResult, 1, ell->center.x);
    hb_arraySetND(aResult, 2, ell->center.y);
    hb_arraySetND(aResult, 3, ell->center.z);
    hb_arraySetND(aResult, 4, ell->major_axis_endpoint.x);
    hb_arraySetND(aResult, 5, ell->major_axis_endpoint.y);
    hb_arraySetND(aResult, 6, ell->major_axis_endpoint.z);
    hb_arraySetND(aResult, 7, ell->axis_ratio);
    hb_arraySetND(aResult, 8, ell->start_param);
    hb_arraySetND(aResult, 9, ell->end_param);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETPOINTGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_POINT3D *pt;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_POINT)
    {
        hb_ret();
        return;
    }
    pt = (DWG_POINT3D *)((DWG_ENTITY *)ent)->geometry;
    if (pt == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(3);
    hb_arraySetND(aResult, 1, pt->x);
    hb_arraySetND(aResult, 2, pt->y);
    hb_arraySetND(aResult, 3, pt->z);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETTEXTGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_TEXT *txt;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_TEXT)
    {
        hb_ret();
        return;
    }
    txt = (DWG_TEXT *)((DWG_ENTITY *)ent)->geometry;
    if (txt == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(6);
    hb_arraySetND(aResult, 1, txt->point.x);
    hb_arraySetND(aResult, 2, txt->point.y);
    hb_arraySetND(aResult, 3, txt->point.z);
    hb_arraySetC(aResult, 4, txt->text);
    hb_arraySetND(aResult, 5, txt->height);
    hb_arraySetND(aResult, 6, txt->angle);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETMTEXTGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_MTEXT *mt;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_MTEXT)
    {
        hb_ret();
        return;
    }
    mt = (DWG_MTEXT *)((DWG_ENTITY *)ent)->geometry;
    if (mt == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(6);
    hb_arraySetND(aResult, 1, mt->point.x);
    hb_arraySetND(aResult, 2, mt->point.y);
    hb_arraySetND(aResult, 3, mt->point.z);
    hb_arraySetC(aResult, 4, mt->text);
    hb_arraySetND(aResult, 5, mt->height);
    hb_arraySetND(aResult, 6, mt->rect_width);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETINSERTGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_INSERT *ins;
    PHB_ITEM aResult;
    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_INSERT)
    {
        hb_ret();
        return;
    }
    ins = (DWG_INSERT *)((DWG_ENTITY *)ent)->geometry;
    if (ins == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(8);
    hb_arraySetND(aResult, 1, ins->point.x);
    hb_arraySetND(aResult, 2, ins->point.y);
    hb_arraySetND(aResult, 3, ins->point.z);
    hb_arraySetC(aResult, 4, ins->block_name);
    hb_arraySetND(aResult, 5, ins->scale_x);
    hb_arraySetND(aResult, 6, ins->scale_y);
    hb_arraySetND(aResult, 7, ins->scale_z);
    hb_arraySetND(aResult, 8, ins->angle);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETSOLIDGEOMETRY)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    DWG_ENTITY_TYPE t;
    DWG_SOLID3D *sol;
    PHB_ITEM aResult;
    if (ent == NULL)
    {
        hb_ret();
        return;
    }
    t = dwg_entity_get_type(ent);
    if (t != DWG_ENTITY_SOLID && t != DWG_ENTITY_FACE)
    {
        hb_ret();
        return;
    }
    sol = (DWG_SOLID3D *)((DWG_ENTITY *)ent)->geometry;
    if (sol == NULL) { hb_ret(); return; }
    aResult = hb_itemArrayNew(12);
    hb_arraySetND(aResult,  1, sol->p1.x);
    hb_arraySetND(aResult,  2, sol->p1.y);
    hb_arraySetND(aResult,  3, sol->p1.z);
    hb_arraySetND(aResult,  4, sol->p2.x);
    hb_arraySetND(aResult,  5, sol->p2.y);
    hb_arraySetND(aResult,  6, sol->p2.z);
    hb_arraySetND(aResult,  7, sol->p3.x);
    hb_arraySetND(aResult,  8, sol->p3.y);
    hb_arraySetND(aResult,  9, sol->p3.z);
    hb_arraySetND(aResult, 10, sol->p4.x);
    hb_arraySetND(aResult, 11, sol->p4.y);
    hb_arraySetND(aResult, 12, sol->p4.z);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETPOLYLINEVERTICES)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    HPOLYLINE pl;
    HVERTEX v;
    unsigned long count, i;

    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_POLYLINE)
    {
        hb_ret();
        return;
    }

    pl = dwg_polyline_from_entity(ent);
    if (pl == NULL)
    {
        hb_ret();
        return;
    }

    count = dwg_polyline_vertex_count(pl);
    if (count == 0)
    {
        hb_reta(0);
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(count);
        v = dwg_polyline_first_vertex(pl);
        i = 0;
        while (v != NULL && i < count)
        {
            double vx, vy, vz, bulge;
            PHB_ITEM aRow = hb_itemArrayNew(4);
            dwg_vertex_get_point(v, &vx, &vy, &vz);
            bulge = dwg_vertex_get_bulge(v);
            hb_arraySetND(aRow, 1, vx);
            hb_arraySetND(aRow, 2, vy);
            hb_arraySetND(aRow, 3, vz);
            hb_arraySetND(aRow, 4, bulge);
            hb_arraySet(aResult, i + 1, aRow);
            hb_itemRelease(aRow);
            v = dwg_polyline_next_vertex(v);
            i++;
        }
        hb_itemReturnRelease(aResult);
    }
}

HB_FUNC(DWG_POLYLINEISCLOSED)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    HPOLYLINE pl;

    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_POLYLINE)
    {
        hb_retl(0);
        return;
    }

    pl = dwg_polyline_from_entity(ent);
    if (pl == NULL)
        hb_retl(0);
    else
        hb_retl(dwg_polyline_is_closed(pl) ? 1 : 0);
}

/* ============================================================================
 * GEOMETRY CREATION
 * ============================================================================ */

HB_FUNC(DWG_ADDLINE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x1 = hb_parnd(2), y1 = hb_parnd(3), z1 = hb_parnd(4);
    double x2 = hb_parnd(5), y2 = hb_parnd(6), z2 = hb_parnd(7);
    HENTITY ent;

    if (h == NULL || !h->open) { hb_ret(); return; }

    ent = dwg_add_line(h->hDwg, x1, y1, z1, x2, y2, z2);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDCIRCLE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double radius = hb_parnd(5);
    HENTITY ent;

    if (h == NULL || !h->open) { hb_ret(); return; }

    ent = dwg_add_circle(h->hDwg, cx, cy, cz, radius);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDARC)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double radius = hb_parnd(5);
    double start_angle = hb_parnd(6), end_angle = hb_parnd(7);
    HENTITY ent;

    if (h == NULL || !h->open) { hb_ret(); return; }

    ent = dwg_add_arc(h->hDwg, cx, cy, cz, radius, start_angle, end_angle);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDPOINT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x = hb_parnd(2), y = hb_parnd(3), z = hb_parnd(4);
    HENTITY ent;

    if (h == NULL || !h->open) { hb_ret(); return; }

    ent = dwg_add_point(h->hDwg, x, y, z);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDTEXT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x = hb_parnd(2), y = hb_parnd(3), z = hb_parnd(4);
    double height = hb_parnd(5), angle = hb_parnd(6);
    const char *text = hb_parc(7);
    HENTITY ent;

    if (h == NULL || !h->open || text == NULL) { hb_ret(); return; }

    ent = dwg_add_text(h->hDwg, x, y, z, height, angle, text);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDMTEXT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x = hb_parnd(2), y = hb_parnd(3), z = hb_parnd(4);
    double height = hb_parnd(5), rect_width = hb_parnd(6);
    const char *text = hb_parc(7);
    HENTITY ent;

    if (h == NULL || !h->open || text == NULL) { hb_ret(); return; }

    ent = dwg_add_mtext(h->hDwg, x, y, z, height, rect_width, text);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDPOLYLINE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY ent;

    if (h == NULL || !h->open) { hb_ret(); return; }

    ent = dwg_add_polyline(h->hDwg);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_ADDPOLYLINEVERTEX)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    double x = hb_parnd(2), y = hb_parnd(3), z = hb_parnd(4);
    double bulge = hb_parnd(5);
    HPOLYLINE pl;

    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_POLYLINE)
        return;

    pl = dwg_polyline_from_entity(ent);
    if (pl != NULL)
        dwg_polyline_add_vertex2(pl, x, y, z, bulge, 0.0, 0.0);
}

HB_FUNC(DWG_CLOSEPOLYLINE)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    int closed = hb_parni(2);
    HPOLYLINE pl;

    if (ent == NULL || dwg_entity_get_type(ent) != DWG_ENTITY_POLYLINE)
        return;

    pl = dwg_polyline_from_entity(ent);
    if (pl != NULL)
        dwg_polyline_set_closed(pl, closed ? 1 : 0);
}

/* ============================================================================
 * TRANSFORMATIONS
 * ============================================================================ */

HB_FUNC(DWG_MOVE)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    double dx = hb_parnd(2), dy = hb_parnd(3), dz = hb_parnd(4);
    if (ent != NULL)
        dwg_entity_move(ent, dx, dy, dz);
}

HB_FUNC(DWG_ROTATE)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double angle = hb_parnd(5);
    if (ent != NULL)
        dwg_entity_rotate(ent, cx, cy, cz, angle);
}

HB_FUNC(DWG_SCALE)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double factor = hb_parnd(5);
    if (ent != NULL)
        dwg_entity_scale(ent, cx, cy, cz, factor);
}

HB_FUNC(DWG_MIRROR)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    double x1 = hb_parnd(2), y1 = hb_parnd(3);
    double x2 = hb_parnd(4), y2 = hb_parnd(5);
    if (ent != NULL)
        dwg_entity_mirror(ent, x1, y1, x2, y2);
}

HB_FUNC(DWG_COPY)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY ent = (HENTITY)hb_parptr(2);
    HENTITY copy;

    if (h == NULL || !h->open || ent == NULL)
    {
        hb_ret();
        return;
    }

    copy = dwg_entity_copy(h->hDwg, ent);
    if (copy == NULL)
        hb_ret();
    else
        hb_retptr((void *)copy);
}

HB_FUNC(DWG_EXPLODE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY ent = (HENTITY)hb_parptr(2);
    unsigned long count = 0UL;

    if (h != NULL && h->open && ent != NULL)
        count = dwg_entity_explode(h->hDwg, ent);

    hb_retnl((long)count);
}

/* ============================================================================
 * SELECTION
 * ============================================================================ */

HB_FUNC(DWG_SELECTPOINT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double px = hb_parnd(2), py = hb_parnd(3), tol = hb_parnd(4);
    unsigned long count = 0UL;

    if (h != NULL && h->open)
        count = dwg_select_point(h->hDwg, px, py, tol);

    hb_retnl((long)count);
}

HB_FUNC(DWG_SELECTWINDOW)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x1 = hb_parnd(2), y1 = hb_parnd(3);
    double x2 = hb_parnd(4), y2 = hb_parnd(5);
    int crossing = hb_parni(6);
    unsigned long count = 0UL;

    if (h != NULL && h->open)
        count = dwg_select_window(h->hDwg, x1, y1, x2, y2, crossing ? DWG_TRUE : DWG_FALSE);

    hb_retnl((long)count);
}

HB_FUNC(DWG_SELECTLAYER)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    const char *name = hb_parc(2);
    unsigned long count = 0UL;

    if (h != NULL && h->open && name != NULL)
        count = dwg_select_layer(h->hDwg, name);

    hb_retnl((long)count);
}

HB_FUNC(DWG_SELCLEAR)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    if (h != NULL && h->open)
        dwg_document_sel_clear(h->hDwg);
}

HB_FUNC(DWG_SELCOUNT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;

    if (h != NULL && h->open)
        count = dwg_document_sel_count(h->hDwg);

    hb_retnl((long)count);
}

HB_FUNC(DWG_SELGET)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long idx = (unsigned long)hb_parnl(2);
    HENTITY ent;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    ent = dwg_document_sel_get(h->hDwg, idx);
    if (ent == NULL)
        hb_ret();
    else
        hb_retptr((void *)ent);
}

HB_FUNC(DWG_SELMOVE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double dx = hb_parnd(2), dy = hb_parnd(3), dz = hb_parnd(4);
    if (h != NULL && h->open)
        dwg_sel_move(h->hDwg, dx, dy, dz);
}

HB_FUNC(DWG_SELROTATE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double angle = hb_parnd(5);
    if (h != NULL && h->open)
        dwg_sel_rotate(h->hDwg, cx, cy, cz, angle);
}

HB_FUNC(DWG_SELSCALE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double cx = hb_parnd(2), cy = hb_parnd(3), cz = hb_parnd(4);
    double factor = hb_parnd(5);
    if (h != NULL && h->open)
        dwg_sel_scale(h->hDwg, cx, cy, cz, factor);
}

HB_FUNC(DWG_SELMIRROR)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double x1 = hb_parnd(2), y1 = hb_parnd(3);
    double x2 = hb_parnd(4), y2 = hb_parnd(5);
    if (h != NULL && h->open)
        dwg_sel_mirror(h->hDwg, x1, y1, x2, y2);
}

HB_FUNC(DWG_SELERASE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_sel_erase(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_SELCOPY)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_sel_copy(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_SELEXPLODE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_sel_explode(h->hDwg);
    hb_retnl((long)count);
}

HB_FUNC(DWG_SELJOIN)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double tolerance = hb_parnd(2);
    unsigned long count = 0UL;
    if (h != NULL && h->open)
        count = dwg_sel_join(h->hDwg, tolerance);
    hb_retnl((long)count);
}

/* ============================================================================
 * ENTITY SELECTION FLAG (for viewer highlighting)
 * ============================================================================ */

HB_FUNC(DWG_ENTITYSELECTED)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    if (ent == NULL)
        hb_retl(0);
    else
        hb_retl(dwg_entity_is_selected(ent) ? 1 : 0);
}

HB_FUNC(DWG_ENTITYSETSELECTED)
{
    HENTITY ent = (HENTITY)hb_parptr(1);
    int selected = hb_parni(2);
    if (ent != NULL)
        dwg_entity_set_selected(ent, selected);
}

HB_FUNC(DWG_CLEARALLSELECTED)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e;

    if (h == NULL || !h->open)
        return;

    for (e = dwg_document_first_entity(h->hDwg); e != NULL; e = dwg_document_next_entity(e))
    {
        dwg_entity_set_selected(e, 0);
    }
}
