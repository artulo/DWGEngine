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
 * Seleccion/edicion/capas/propiedades (pedido de Arturo 2026-08-26,
 * ver plan "Seleccion, capas y propiedades interactivas en TDwgViewer")
 * -- wrappers mecanicos de backend YA EXISTENTE en dwg_selection.h/
 * dwg_layer.h/dwg_document.h, nunca antes expuesto a Harbour:
 *
 *   DWG_SELECTPOINT( pDoc, nPx, nPy, nTol )                  -> nAgregados
 *   DWG_SELECTWINDOW( pDoc, nX1, nY1, nX2, nY2, lCrossing )  -> nAgregados
 *   DWG_SELECTLAYER( pDoc, cCapa )                            -> nAgregados
 *   DWG_SELCLEAR( pDoc )                                      -> NIL
 *   DWG_SELCOUNT( pDoc )                                      -> nCantidad
 *   DWG_SELGET( pDoc, nIndex )                    -> pEntidad o NIL (1-based)
 *   DWG_SELMOVE/SELROTATE/SELSCALE/SELMIRROR( pDoc, ... )     -> NIL
 *   DWG_SELERASE/SELCOPY/SELEXPLODE( pDoc )                   -> nCantidad
 *   DWG_SELJOIN( pDoc, nTol )                                 -> nCreados
 *   DWG_LAYERLIST( pDoc )                                     -> aNombres
 *   DWG_LAYERGETFLAGS( pDoc, cCapa ) -> { lOff,lFrozen,lLocked,lPlottable } o NIL
 *   DWG_LAYERSETOFF/SETFROZEN( pDoc, cCapa, lValor )          -> lOk
 *   DWG_LAYERISOLATE( pDoc, aCapasVisibles )                  -> NIL
 *   DWG_ENTITYGETPROPS( pDoc, pEntidad )   -> aProps (ver layout abajo) o NIL
 *   DWG_ENTITYSETPROPS( pDoc, pEntidad, cCapa, nColor, cLinetype,
 *                        nG1..nG6, cTexto, lSetGeom )          -> lOk
 *
 * aProps/ENTITYSETPROPS: { nTipo, cCapa, nColor, cLinetype, nG1, nG2,
 * nG3, nG4, nG5, nG6, cTexto } -- nTipo es el ordinal DWG_ENTITY_TYPE
 * (dwg_types.h: UNKNOWN=0,POINT=1,LINE=2,CIRCLE=3,ARC=4,POLYLINE=5,
 * VERTEX=6,TEXT=7,MTEXT=8,...). nG1..nG6/cTexto solo tienen sentido
 * segun nTipo (alcance de esta vuelta, POLYLINE/HATCH/INSERT/MTEXT/
 * SOLID/FACE solo exponen capa/color/linetype comunes, no geometria):
 *   POINT:  G1,G2,G3 = x,y,z
 *   LINE:   G1..G6   = x1,y1,z1,x2,y2,z2
 *   CIRCLE: G1..G4   = cx,cy,cz,radio
 *   ARC:    G1..G6   = cx,cy,cz,radio,angIni,angFin (grados)
 *   TEXT:   G1..G5   = x,y,z,altura,angulo (grados); cTexto = texto
 *   otros:  G1..G6 = 0, cTexto = ""
 * En ENTITYSETPROPS, cCapa/nColor/cLinetype vacios ("" / -1) dejan ese
 * campo sin tocar; los campos geometricos solo se escriben si nTipo de
 * la entidad coincide con el layout de arriba (mismo tipo, se ignora
 * si no).
 *
 * Creacion de entidades (pedido de Arturo 2026-08-26, "crear lineas
 * circulo rectangulos y otros elementos", ver plan "Crear entidades por
 * click en TDwgViewer") -- wrappers mecanicos de dwg_geometry.h/
 * dwg_polyline.h, funciones de creacion ya existentes desde el reverse
 * original y nunca antes expuestas a Harbour:
 *
 *   DWG_ADDLINE( pDoc, x1,y1,z1, x2,y2,z2 )                   -> pEntidad
 *   DWG_ADDCIRCLE( pDoc, cx,cy,cz, radio )                    -> pEntidad
 *   DWG_ADDARC( pDoc, cx,cy,cz, radio, angIni, angFin )       -> pEntidad
 *   DWG_ADDPOLYLINE( pDoc )                                   -> pEntidad
 *   DWG_ADDVERTEX( pEntidadPolyline, x, y, z )                -> lOk
 *   DWG_POLYSETCLOSED( pEntidadPolyline, lCerrada )           -> lOk
 *   DWG_SELADD( pDoc, pEntidad )                              -> lOk
 *
 * El .prg (ver TDwgViewer:DrawClick/FinishPolyline en dwg_viewer.prg)
 * arma un rectangulo como una POLYLINE cerrada de 4 vertices (no hay
 * entidad DWG "RECTANGLE" real) y clasifica toda entidad recien creada
 * en capa "0" + la deja auto-seleccionada (Dwg_SelAdd) para que sea
 * facil reclasificarla/editarla desde el dialogo de Propiedades ya
 * existente.
 *
 * Nuevo/Guardar (pedido de Arturo 2026-08-26) -- wrappers mecanicos de
 * los 3 escritores de dwg_file_io.h/dwg_r2000_writer.h:
 *
 *   DWG_NEW()                                                 -> pDoc
 *   DWG_WRITEDXF( pDoc, cPath )                               -> lOk
 *   DWG_WRITEDWGR12( pDoc, cPath )                            -> lOk
 *   DWG_WRITEDWGR2000( pDoc, cTemplatePath, cOutPath )        -> lOk
 *
 * dwg_write_dxf/dwg_write_dwg_r12 escriben SIEMPRE desde cero (sirven
 * para un documento "Nuevo" sin archivo original). dwg_write_dwg_r2000
 * (el formato mas compatible) NO escribe desde cero -- carga
 * cTemplatePath como plantilla de bytes y le reemplaza el Model Space,
 * asi que solo funciona si existe un archivo real para usar de
 * plantilla (normalmente el mismo que se abrio). La logica de "probar
 * R2000, si no hay plantilla o falla caer a R12" vive del lado .prg
 * (TDwgViewer:WriteTo en dwg_viewer.prg), no aca -- estos wrappers solo
 * llaman cada escritor tal cual.
 *
 * Cotas (pedido de Arturo 2026-08-26, "permitir colocar cotas") --
 * DIMENSION lineal alineada REAL (creation+render+transform propios en
 * dwg_dimension.h/.c, dwg_render.c, dwg_transform.c -- a diferencia de
 * casi todo lo demas de este archivo, este backend NO existia antes,
 * ver el plan "Cotas (DIMENSION) reales por click en TDwgViewer"):
 *
 *   DWG_ADDDIMENSIONLINEAR( pDoc, x1,y1,z1, x2,y2,z2, defx,defy,defz )
 *                                                             -> pEntidad
 *
 * x1,y1,z1/x2,y2,z2 son los dos puntos medidos; defx,defy,defz ubica la
 * linea de cota (offset perpendicular a la direccion entre los dos
 * primeros puntos -- la cota es siempre "alineada", ver dwg_dimension.h).
 *
 * Texto con tipo de letra + tamaño (pedido de Arturo 2026-08-26,
 * "agregar funcion para escribir texto definiendo el tipo de letra y
 * tamaño") -- de paso, corrige un bug real: dwg_render.c dibujaba TODO
 * TEXT en "Arial" fijo, sin importar su estilo real (ver
 * resolve_text_font en dwg_render.c):
 *
 *   DWG_ADDTEXT( pDoc, x,y,z, altura, angulo, cTexto, cFuente )
 *                                                             -> pEntidad
 *
 * cFuente resuelve-o-crea un DWG_STYLE con ESE nombre (dobla como
 * nombre de estilo, evita pedir uno aparte) y lo asigna al TEXT; vacio
 * o NIL deja el TEXT sin estilo propio (cae al "Arial" de siempre).
 *
 * Edicion in situ de texto (pedido de Arturo 2026-08-27, "editar textos
 * in situ") -- doble-click en un TEXT ya existente (ver
 * TDwgBitmap:LDblClick en dwg_viewer.prg) reabre el mismo dialogo de
 * dwg_text_dlg.prg, ahora precargado con el contenido/fuente/tamaño
 * ACTUALES de la entidad en vez de los valores por defecto de "Texto
 * nuevo". El contenido/altura ya se guardaban con DWG_ENTITYSETPROPS
 * (arriba) -- faltaba solo leer/escribir la FUENTE de una entidad ya
 * existente (DWG_ADDTEXT arriba solo la asigna en la creacion):
 *
 *   DWG_TEXTGETSTYLE( pDoc, pEntidad )                -> cFuente ("" si no tiene)
 *   DWG_TEXTSETSTYLE( pDoc, pEntidad, cFuente )        -> lOk
 *
 * DWG_TEXTSETSTYLE resuelve-o-crea el DWG_STYLE tal cual DWG_ADDTEXT
 * (misma logica, factorizada en dwg_hb_apply_text_style de abajo).
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
#include <string.h>

#include "dwg_types.h"
#include "dwg_document.h"
#include "dwg_file_io.h"
#include "dwg_r2000_reader.h"
#include "dwg_r1314_reader.h"
#include "dwg_r2004_reader.h"
#include "dwg_render.h"
#include "dwg_selection.h"
#include "dwg_layer.h"
#include "dwg_entity.h"
#include "dwg_geometry.h"
#include "dwg_text.h"
#include "dwg_polyline.h"
#include "dwg_r2000_writer.h"
#include "dwg_dimension.h"
#include "dwg_style.h"

/* DWGENGINE_HAVE_LIBREDWG: definido SOLO por win32\BuildMSVC.bat -- el
 * build bcc32 (win32\Build.bat) no compila dwg_libredwg_bridge.c (LibreDWG
 * necesita C99/C11, impractico en bcc32) asi que este .c debe seguir
 * linkeando limpio SIN dwg_read_dwg_libredwg cuando se compila con bcc32.
 */
#ifdef DWGENGINE_HAVE_LIBREDWG
#include "dwg_libredwg_bridge.h"
#endif

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

    hDwg = NULL;
#ifdef DWGENGINE_HAVE_LIBREDWG
    hDwg = dwg_read_dwg_libredwg(file, &result); /* LibreDWG (GPLv3) real motor -- cubre R13 a R2018+, ver dwg_libredwg_bridge.h */
#endif
    if (hDwg == NULL)
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

/*
 * Compartido por DWG_RENDERTOHBITMAP (2D) y DWG_RENDERTOHBITMAP3D --
 * crea el DIB section top-down de 24bpp, selecciona su propio HDC de
 * memoria, llama dwg_render_to_hdc (camera=NULL para 2D, o una
 * DWG_CAMERA3D real para navegacion 3D -- ver dwg_render.h), y
 * devuelve el HBITMAP resultante. Factorizado aca para no duplicar la
 * creacion de DIB/HDC entre ambas funciones Harbour.
 */
static HBITMAP dwg_render_to_new_hbitmap(HDWG hDwg, int width, int height,
                                          double scale, double origin_x, double origin_y,
                                          const DWG_CAMERA3D *camera)
{
    BITMAPINFO bmi;
    HDC screen_dc, mem_dc;
    HBITMAP hBmp, old_bmp;
    void *bits;

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
        return NULL;

    mem_dc = CreateCompatibleDC(NULL);
    if (mem_dc == NULL)
    {
        DeleteObject(hBmp);
        return NULL;
    }
    old_bmp = (HBITMAP)SelectObject(mem_dc, hBmp);

    dwg_render_to_hdc(hDwg, (void *)mem_dc, (long)width, (long)height,
                      scale, origin_x, origin_y, camera);

    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);

    return hBmp;
}

static void dwg_hbitmap_return(HBITMAP hBmp, int width, int height)
{
    PHB_ITEM aResult = hb_itemArrayNew(3);
    hb_arraySetNL(aResult, 1, (long)(HB_PTRUINT)hBmp);
    hb_arraySetNL(aResult, 2, (long)width);
    hb_arraySetNL(aResult, 3, (long)height);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_GETEXTENTS3D)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    double min_x, min_y, min_z, max_x, max_y, max_z;

    if (h == NULL || !h->open ||
        !dwg_render_get_extents_3d(h->hDwg, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z))
    {
        hb_ret();
        return;
    }

    {
        PHB_ITEM aResult = hb_itemArrayNew(6);
        hb_arraySetND(aResult, 1, min_x);
        hb_arraySetND(aResult, 2, min_y);
        hb_arraySetND(aResult, 3, min_z);
        hb_arraySetND(aResult, 4, max_x);
        hb_arraySetND(aResult, 5, max_y);
        hb_arraySetND(aResult, 6, max_z);
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
    HBITMAP hBmp;

    if (h == NULL || !h->open || width <= 0 || height <= 0 || scale <= 0.0)
    {
        hb_ret();
        return;
    }

    hBmp = dwg_render_to_new_hbitmap(h->hDwg, width, height, scale, origin_x, origin_y, NULL);
    if (hBmp == NULL)
    {
        hb_ret();
        return;
    }

    dwg_hbitmap_return(hBmp, width, height);
}

/*
 * DWG_RENDERTOHBITMAP3D( pDoc, nWidth, nHeight, nScale, nAzimuth,
 *                        nElevation, nTargetX, nTargetY, nTargetZ )
 *   -> { hBitmap, nWidth, nHeight } o NIL
 *
 * Navegacion 3D real (camara orbital de proyeccion paralela, ver
 * DWG_CAMERA3D en include/dwg_render.h): nAzimuth/nElevation en
 * RADIANES, nTargetX/Y/Z el punto del mundo alrededor del cual orbita
 * la camara. nScale sigue siendo unidades-mundo por pixel (mismo
 * contrato que el 2D) aplicado DESPUES de proyectar con la camara --
 * hace de "zoom"/dolly bajo proyeccion paralela.
 */
HB_FUNC(DWG_RENDERTOHBITMAP3D)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    int width = hb_parni(2);
    int height = hb_parni(3);
    double scale = hb_parnd(4);
    DWG_CAMERA3D camera;
    HBITMAP hBmp;

    camera.azimuth = hb_parnd(5);
    camera.elevation = hb_parnd(6);
    camera.target_x = hb_parnd(7);
    camera.target_y = hb_parnd(8);
    camera.target_z = hb_parnd(9);

    if (h == NULL || !h->open || width <= 0 || height <= 0 || scale <= 0.0)
    {
        hb_ret();
        return;
    }

    /* origin_x/origin_y (unidades de VISTA, ya proyectadas -- no mundo)
     * centran la vista proyectada en el bitmap: la camara siempre mira
     * a target, que se proyecta a vista (0,0), asi que centrar el
     * bitmap en eso es restar medio ancho/alto en unidades-mundo. */
    hBmp = dwg_render_to_new_hbitmap(h->hDwg, width, height, scale,
                                     -((double)width * scale) / 2.0,
                                     ((double)height * scale) / 2.0,
                                     &camera);
    if (hBmp == NULL)
    {
        hb_ret();
        return;
    }

    dwg_hbitmap_return(hBmp, width, height);
}

/* ------------------------------------------------------------------
 * Seleccion / edicion / capas / propiedades -- ver doc-comment del
 * inicio del archivo para el contrato de cada funcion. Todas mecanicas:
 * parsear parametros Harbour, llamar la funcion C ya existente, devolver
 * el resultado. Ninguna logica de seleccion/edicion/capas nueva aca.
 * ------------------------------------------------------------------ */

HB_FUNC(DWG_SELECTPOINT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long added = 0UL;

    if (h != NULL && h->open)
        added = dwg_select_point(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4));

    hb_retnl((long)added);
}

HB_FUNC(DWG_SELECTWINDOW)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long added = 0UL;

    if (h != NULL && h->open)
        added = dwg_select_window(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4), hb_parnd(5),
                                  (DWG_BOOL)(hb_parl(6) ? DWG_TRUE : DWG_FALSE));

    hb_retnl((long)added);
}

HB_FUNC(DWG_SELECTLAYER)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long added = 0UL;

    if (h != NULL && h->open)
        added = dwg_select_layer(h->hDwg, hb_parc(2));

    hb_retnl((long)added);
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

/* DWG_SELGET( pDoc, nIndex ) -> pEntidad o NIL -- nIndex es 1-based
   (convencion Harbour, como cualquier array), dwg_document_sel_get de
   dwg_document.h es 0-based en C, se resta 1 aca. Usada por
   dwg_props_dlg.prg para recorrer la seleccion actual (Dwg_SelCount
   entidades, Dwg_SelGet(pDoc,1)..Dwg_SelGet(pDoc,nCount)). */
HB_FUNC(DWG_SELGET)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    long idx = hb_parnl(2);
    HENTITY e = NULL;

    if (h != NULL && h->open && idx >= 1L)
        e = dwg_document_sel_get(h->hDwg, (unsigned long)(idx - 1L));

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

HB_FUNC(DWG_SELMOVE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);

    if (h != NULL && h->open)
        dwg_sel_move(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4));
}

HB_FUNC(DWG_SELROTATE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);

    if (h != NULL && h->open)
        dwg_sel_rotate(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4), hb_parnd(5));
}

HB_FUNC(DWG_SELSCALE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);

    if (h != NULL && h->open)
        dwg_sel_scale(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4), hb_parnd(5));
}

HB_FUNC(DWG_SELMIRROR)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);

    if (h != NULL && h->open)
        dwg_sel_mirror(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4), hb_parnd(5));
}

HB_FUNC(DWG_SELERASE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long n = 0UL;

    if (h != NULL && h->open)
        n = dwg_sel_erase(h->hDwg);

    hb_retnl((long)n);
}

HB_FUNC(DWG_SELCOPY)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long n = 0UL;

    if (h != NULL && h->open)
        n = dwg_sel_copy(h->hDwg);

    hb_retnl((long)n);
}

HB_FUNC(DWG_SELEXPLODE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long n = 0UL;

    if (h != NULL && h->open)
        n = dwg_sel_explode(h->hDwg);

    hb_retnl((long)n);
}

HB_FUNC(DWG_SELJOIN)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    unsigned long n = 0UL;

    if (h != NULL && h->open)
        n = dwg_sel_join(h->hDwg, hb_parnd(2));

    hb_retnl((long)n);
}

HB_FUNC(DWG_LAYERLIST)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    PHB_ITEM aResult;
    HLAYER lay;
    HB_SIZE i;

    if (h == NULL || !h->open)
    {
        hb_reta(0);
        return;
    }

    aResult = hb_itemArrayNew((HB_SIZE)dwg_document_layer_count(h->hDwg));
    i = 1;
    for (lay = dwg_document_first_layer(h->hDwg); lay != NULL; lay = dwg_document_next_layer(lay))
    {
        hb_arraySetC(aResult, i, dwg_layer_get_name(lay));
        i++;
    }
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_LAYERGETFLAGS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HLAYER lay;
    PHB_ITEM aResult;

    if (h == NULL || !h->open)
    {
        hb_ret();
        return;
    }

    lay = dwg_document_get_layer(h->hDwg, hb_parc(2));
    if (lay == NULL)
    {
        hb_ret();
        return;
    }

    aResult = hb_itemArrayNew(4);
    hb_arraySetL(aResult, 1, dwg_layer_is_off(lay) ? HB_TRUE : HB_FALSE);
    hb_arraySetL(aResult, 2, dwg_layer_is_frozen(lay) ? HB_TRUE : HB_FALSE);
    hb_arraySetL(aResult, 3, dwg_layer_is_locked(lay) ? HB_TRUE : HB_FALSE);
    hb_arraySetL(aResult, 4, dwg_layer_is_plottable(lay) ? HB_TRUE : HB_FALSE);
    hb_itemReturnRelease(aResult);
}

HB_FUNC(DWG_LAYERSETOFF)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HLAYER lay;

    if (h == NULL || !h->open)
    {
        hb_retl(HB_FALSE);
        return;
    }

    lay = dwg_document_get_layer(h->hDwg, hb_parc(2));
    if (lay == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    dwg_layer_set_off(lay, hb_parl(3) ? DWG_TRUE : DWG_FALSE);
    hb_retl(HB_TRUE);
}

HB_FUNC(DWG_LAYERSETFROZEN)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HLAYER lay;

    if (h == NULL || !h->open)
    {
        hb_retl(HB_FALSE);
        return;
    }

    lay = dwg_document_get_layer(h->hDwg, hb_parc(2));
    if (lay == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    dwg_layer_set_frozen(lay, hb_parl(3) ? DWG_TRUE : DWG_FALSE);
    hb_retl(HB_TRUE);
}

/* Apaga (dwg_layer_set_off) toda capa cuyo nombre no este en aCapasVisibles
   (parametro 2, array de strings), y la prende si esta -- "aislar" en el
   sentido CAD estandar (mostrar solo estas capas, sin tocar frozen/locked). */
HB_FUNC(DWG_LAYERISOLATE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    PHB_ITEM aVisible = hb_param(2, HB_IT_ARRAY);
    HLAYER lay;
    HB_SIZE nVisible, i;

    if (h == NULL || !h->open || aVisible == NULL)
        return;

    nVisible = hb_arrayLen(aVisible);

    for (lay = dwg_document_first_layer(h->hDwg); lay != NULL; lay = dwg_document_next_layer(lay))
    {
        const char *name = dwg_layer_get_name(lay);
        DWG_BOOL visible = DWG_FALSE;

        for (i = 1; i <= nVisible; i++)
        {
            const char *candidate = hb_arrayGetCPtr(aVisible, i);
            if (candidate != NULL && name != NULL && strcmp(candidate, name) == 0)
            {
                visible = DWG_TRUE;
                break;
            }
        }

        dwg_layer_set_off(lay, visible ? DWG_FALSE : DWG_TRUE);
    }
}

/* Layout de aProps: ver doc-comment del inicio del archivo. */
static void dwg_hb_entity_props_fill(HENTITY e, PHB_ITEM aResult)
{
    double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0, g5 = 0.0, g6 = 0.0;
    const char *text = "";

    switch (dwg_entity_get_type(e))
    {
    case DWG_ENTITY_POINT:
        dwg_point_get_xyz(e, &g1, &g2, &g3);
        break;
    case DWG_ENTITY_LINE:
        dwg_line_get_start(e, &g1, &g2, &g3);
        dwg_line_get_end(e, &g4, &g5, &g6);
        break;
    case DWG_ENTITY_CIRCLE:
        dwg_circle_get_center(e, &g1, &g2, &g3);
        g4 = dwg_circle_get_radius(e);
        break;
    case DWG_ENTITY_ARC:
        dwg_arc_get_center(e, &g1, &g2, &g3);
        g4 = dwg_arc_get_radius(e);
        dwg_arc_get_angles(e, &g5, &g6);
        break;
    case DWG_ENTITY_TEXT:
        dwg_text_get_point(e, &g1, &g2, &g3);
        g4 = dwg_text_get_height(e);
        g5 = dwg_text_get_angle(e);
        text = dwg_text_get_text(e);
        if (text == NULL) text = "";
        break;
    default:
        break; /* solo capa/color/linetype comunes -- ver alcance en el plan */
    }

    hb_arraySetNI(aResult, 1, (int)dwg_entity_get_type(e));
    hb_arraySetC(aResult, 2, dwg_entity_get_layer(e));
    hb_arraySetNI(aResult, 3, (int)dwg_entity_get_color(e));
    hb_arraySetC(aResult, 4, dwg_entity_get_linetype(e));
    hb_arraySetND(aResult, 5, g1);
    hb_arraySetND(aResult, 6, g2);
    hb_arraySetND(aResult, 7, g3);
    hb_arraySetND(aResult, 8, g4);
    hb_arraySetND(aResult, 9, g5);
    hb_arraySetND(aResult, 10, g6);
    hb_arraySetC(aResult, 11, text);
}

HB_FUNC(DWG_ENTITYGETPROPS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = (HENTITY)hb_parptr(2);
    PHB_ITEM aResult;

    if (h == NULL || !h->open || e == NULL)
    {
        hb_ret();
        return;
    }

    aResult = hb_itemArrayNew(11);
    dwg_hb_entity_props_fill(e, aResult);
    hb_itemReturnRelease(aResult);
}

/* DWG_ENTITYSETPROPS( pDoc, pEntidad, cCapa, nColor, cLinetype,
                        nG1, nG2, nG3, nG4, nG5, nG6, cTexto, lSetGeom ) -> lOk
   cCapa/cLinetype vacios ("") y nColor negativo dejan ese campo comun
   sin tocar. lSetGeom en .F. salta TODA escritura de geometria (usado
   por el panel de propiedades en edicion multi-seleccion, donde solo
   capa/color/linetype tienen sentido aplicar a la vez -- sin esto,
   pasar g1..g6=0 en una edicion multiple destruiria la geometria real
   de cualquier LINE/CIRCLE/ARC/POINT/TEXT del grupo). Con lSetGeom en
   .T., los campos geometricos se escriben siempre que la entidad sea
   del tipo correspondiente (ver dwg_hb_entity_props_fill/doc-comment de
   arriba para el layout) -- si es de otro tipo, se ignoran, mismo
   alcance que ENTITYGETPROPS. */
HB_FUNC(DWG_ENTITYSETPROPS)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = (HENTITY)hb_parptr(2);
    const char *layer = hb_parc(3);
    int color = hb_parni(4);
    const char *linetype = hb_parc(5);
    double g1 = hb_parnd(6), g2 = hb_parnd(7), g3 = hb_parnd(8);
    double g4 = hb_parnd(9), g5 = hb_parnd(10), g6 = hb_parnd(11);
    const char *text = hb_parc(12);
    int setGeom = hb_parl(13);

    if (h == NULL || !h->open || e == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    if (layer != NULL && layer[0] != '\0')
        dwg_entity_put_layer(e, layer);
    if (color >= 0)
        dwg_entity_put_color(e, (unsigned short)color);
    if (linetype != NULL && linetype[0] != '\0')
        dwg_entity_put_linetype(e, linetype);

    if (!setGeom)
    {
        hb_retl(HB_TRUE);
        return;
    }

    switch (dwg_entity_get_type(e))
    {
    case DWG_ENTITY_POINT:
        dwg_point_set_xyz(e, g1, g2, g3);
        break;
    case DWG_ENTITY_LINE:
        dwg_line_set_start(e, g1, g2, g3);
        dwg_line_set_end(e, g4, g5, g6);
        break;
    case DWG_ENTITY_CIRCLE:
        dwg_circle_set_center(e, g1, g2, g3);
        dwg_circle_set_radius(e, g4);
        break;
    case DWG_ENTITY_ARC:
        dwg_arc_set_center(e, g1, g2, g3);
        dwg_arc_set_radius(e, g4);
        dwg_arc_set_angles(e, g5, g6);
        break;
    case DWG_ENTITY_TEXT:
        dwg_text_set_point(e, g1, g2, g3);
        dwg_text_set_height(e, g4);
        dwg_text_set_angle(e, g5);
        if (text != NULL)
            dwg_text_set_text(e, text);
        break;
    default:
        break;
    }

    hb_retl(HB_TRUE);
}

/* ------------------------------------------------------------------
 * Creacion de entidades -- ver doc-comment del inicio del archivo.
 * Wrappers mecanicos de dwg_add_X y dwg_polyline_set_closed (ya
 * existentes en dwg_geometry.c/dwg_polyline.c desde el reverse
 * original) + dwg_document_sel_add (ya existente en dwg_document.c).
 * ------------------------------------------------------------------ */

HB_FUNC(DWG_ADDLINE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = NULL;

    if (h != NULL && h->open)
        e = dwg_add_line(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4),
                         hb_parnd(5), hb_parnd(6), hb_parnd(7));

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

HB_FUNC(DWG_ADDCIRCLE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = NULL;

    if (h != NULL && h->open)
        e = dwg_add_circle(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4), hb_parnd(5));

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

HB_FUNC(DWG_ADDARC)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = NULL;

    if (h != NULL && h->open)
        e = dwg_add_arc(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4),
                        hb_parnd(5), hb_parnd(6), hb_parnd(7));

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

HB_FUNC(DWG_ADDPOLYLINE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = NULL;

    if (h != NULL && h->open)
        e = dwg_add_polyline(h->hDwg);

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

HB_FUNC(DWG_ADDVERTEX)
{
    HENTITY e = (HENTITY)hb_parptr(1);
    HVERTEX v = NULL;

    if (e != NULL)
        v = dwg_add_vertex(e, hb_parnd(2), hb_parnd(3), hb_parnd(4));

    hb_retl(v != NULL ? HB_TRUE : HB_FALSE);
}

HB_FUNC(DWG_POLYSETCLOSED)
{
    HENTITY e = (HENTITY)hb_parptr(1);
    HPOLYLINE pl;

    if (e == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    pl = dwg_polyline_from_entity(e);
    if (pl == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    hb_retl(dwg_polyline_set_closed(pl, hb_parl(2) ? DWG_TRUE : DWG_FALSE) ? HB_TRUE : HB_FALSE);
}

HB_FUNC(DWG_SELADD)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = (HENTITY)hb_parptr(2);

    if (h == NULL || !h->open || e == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    hb_retl(dwg_document_sel_add(h->hDwg, e) ? HB_TRUE : HB_FALSE);
}

/* ------------------------------------------------------------------
 * Nuevo/Guardar (pedido de Arturo 2026-08-26, "deberia haber uno crear
 * nuevo y guardar") -- ver doc-comment del inicio del archivo para el
 * porque de los 3 escritores distintos (dwg_write_dxf/dwg_write_dwg_r12
 * sin plantilla, dwg_write_dwg_r2000 con plantilla). Wrappers mecanicos,
 * misma logica de fallback R2000->R12 vive del lado .prg
 * (TDwgViewer:WriteTo en dwg_viewer.prg).
 * ------------------------------------------------------------------ */

HB_FUNC(DWG_NEW)
{
    HDWG hDwg;
    dwg_hb_doc *h;

    hDwg = dwg_document_create();
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

HB_FUNC(DWG_WRITEDXF)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    const char *path = hb_parc(2);

    if (h == NULL || !h->open || path == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    hb_retl(dwg_write_dxf(h->hDwg, path) == DWG_IO_OK ? HB_TRUE : HB_FALSE);
}

HB_FUNC(DWG_WRITEDWGR12)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    const char *path = hb_parc(2);

    if (h == NULL || !h->open || path == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    hb_retl(dwg_write_dwg_r12(h->hDwg, path) == DWG_IO_OK ? HB_TRUE : HB_FALSE);
}

HB_FUNC(DWG_WRITEDWGR2000)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    const char *templatePath = hb_parc(2);
    const char *outPath = hb_parc(3);

    if (h == NULL || !h->open || templatePath == NULL || outPath == NULL)
    {
        hb_retl(HB_FALSE);
        return;
    }

    hb_retl(dwg_write_dwg_r2000(h->hDwg, templatePath, outPath) == DWG_IO_OK ? HB_TRUE : HB_FALSE);
}

/* Cotas (pedido de Arturo 2026-08-26, "permitir colocar cotas") -- ver
   dwg_dimension.h para el porque de la entidad DIMENSION real (en vez
   de sintetizar lineas+texto sueltas al colocar, como el lector ya
   hace al abrir un archivo). Wrapper mecanico. */
HB_FUNC(DWG_ADDDIMENSIONLINEAR)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = NULL;

    if (h != NULL && h->open)
        e = dwg_add_dimension_linear(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4),
                                     hb_parnd(5), hb_parnd(6), hb_parnd(7),
                                     hb_parnd(8), hb_parnd(9), hb_parnd(10));

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

/* Resuelve-o-crea un DWG_STYLE llamado "font" y lo asigna a e -- logica
   compartida entre DWG_ADDTEXT (asignacion en la creacion) y
   DWG_TEXTSETSTYLE (edicion in situ de una entidad ya existente, ver
   doc-comment de arriba). font vacio/NULL no hace nada (deja el TEXT
   sin estilo propio, cae al "Arial" de siempre en resolve_text_font,
   dwg_render.c). */
static void dwg_hb_apply_text_style(HDWG hDwg, HENTITY e, const char *font)
{
    HSTYLE style;

    if (e == NULL || font == NULL || font[0] == '\0')
        return;

    style = dwg_document_get_style(hDwg, font);
    if (style == NULL)
        style = dwg_document_add_style(hDwg, font);
    if (style != NULL)
    {
        dwg_style_set_font(style, font);
        dwg_text_set_style_name(e, font);
    }
}

/* Texto con tipo de letra + tamaño (pedido de Arturo 2026-08-26,
   "agregar funcion para escribir texto definiendo el tipo de letra y
   tamaño") -- dwg_add_text (ya existia) + dwg_hb_apply_text_style de
   arriba -- ver resolve_text_font en dwg_render.c para el fix del lado
   del render que hace que esto realmente se vea reflejado (antes, todo
   TEXT se dibujaba siempre en Arial fijo). cFuente vacio o NIL deja el
   TEXT sin estilo propio (mismo "Arial" de siempre). */
HB_FUNC(DWG_ADDTEXT)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    const char *text = hb_parc(7);
    const char *font = hb_parc(8);
    HENTITY e = NULL;

    if (h != NULL && h->open)
    {
        e = dwg_add_text(h->hDwg, hb_parnd(2), hb_parnd(3), hb_parnd(4),
                         hb_parnd(5), hb_parnd(6), text);

        dwg_hb_apply_text_style(h->hDwg, e, font);
    }

    if (e == NULL)
        hb_ret();
    else
        hb_retptr((void *)e);
}

/* DWG_TEXTGETSTYLE( pDoc, pEntidad ) -> cFuente ("" si la entidad no es
   TEXT, no tiene estilo propio, o los parametros son invalidos) -- ver
   doc-comment de arriba, "Edicion in situ de texto". */
HB_FUNC(DWG_TEXTGETSTYLE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = (HENTITY)hb_parptr(2);
    const char *style;

    if (h == NULL || !h->open || e == NULL || dwg_entity_get_type(e) != DWG_ENTITY_TEXT)
    {
        hb_retc("");
        return;
    }

    style = dwg_text_get_style_name(e);
    hb_retc(style != NULL ? style : "");
}

/* DWG_TEXTSETSTYLE( pDoc, pEntidad, cFuente ) -> lOk -- ver doc-comment
   de arriba, "Edicion in situ de texto". Sin efecto (devuelve .F.) si
   la entidad no es TEXT. */
HB_FUNC(DWG_TEXTSETSTYLE)
{
    dwg_hb_doc *h = (dwg_hb_doc *)hb_parptr(1);
    HENTITY e = (HENTITY)hb_parptr(2);
    const char *font = hb_parc(3);

    if (h == NULL || !h->open || e == NULL || dwg_entity_get_type(e) != DWG_ENTITY_TEXT)
    {
        hb_retl(HB_FALSE);
        return;
    }

    dwg_hb_apply_text_style(h->hDwg, e, font);
    hb_retl(HB_TRUE);
}
