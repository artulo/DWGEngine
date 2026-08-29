/* dwg_libredwg_bridge.c
 *
 * Puente entre LibreDWG (motor de lectura real, GPLv3, fuente local en
 * D:\estudio\libredwg-master) y el modelo de objetos propio de
 * DWGEngine (HDWG/HENTITY). LibreDWG hace toda la lectura/decodificacion
 * del archivo DWG (dwg_read_file camina el header, las secciones
 * paginadas/comprimidas, el bitstream de cada objeto, etc.); esta
 * funcion solo recorre su arreglo plano dwg.object[] y llena el modelo
 * de DWGEngine via las mismas dwg_add_line/dwg_add_circle/
 * dwg_entity_put_layer/etc que ya usan los lectores propios -- todo el
 * codigo aguas abajo (dwg_render.c, dwg_document.c, el .prg de Harbour/
 * FiveWin) queda exactamente igual.
 *
 * Reemplaza, para las versiones que LibreDWG soporta, a los tres
 * lectores propios (dwg_r2000_reader.c / dwg_r1314_entity_reader.c /
 * dwg_r2004_entity_reader.c) -- ver Dwg_Open en harbour\dwg_hbfunc.c
 * para el orden de intento.
 *
 * Angulos: DWGEngine los maneja en GRADOS (dwg_file_io.h), LibreDWG los
 * guarda en RADIANES (BITCODE_BD) -- toda lectura de angulo se convierte
 * aca mismo, igual que ya hacia dwg_r2004_entity_reader.c.
 *
 * Color: BITCODE_CMC.index ya viene resuelto por LibreDWG (incluyendo
 * el caso TrueColor-empaquetado 0xC3, ver bit_upconvert_CMC en
 * src/bits.c) -- 0 es BYBLOCK, 256 es BYLAYER, negativo es "capa
 * apagada". BYBLOCK/BYLAYER se resuelven aca al color propio de la capa
 * (misma convencion que los lectores propios: nunca se guarda 0/256 en
 * el modelo de DWGEngine, siempre el indice ACI concreto).
 */

#include "dwg_libredwg_bridge.h"
#include "dwg_document.h"
#include "dwg_entity.h"
#include "dwg_geometry.h"
#include "dwg_text.h"
#include "dwg_mtext.h"
#include "dwg_solid.h"
#include "dwg_polyline.h"
#include "dwg_hatch.h"
#include "dwg_vertex.h"
#include "dwg_style.h"

#include <dwg.h>
#include <dwg_api.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RAD2DEG(r) ((r) * 180.0 / M_PI)

/* Dwg_Color.index NO siempre trae el ACI resuelto: para varios archivos
 * reales (confirmado en 02_Planta 1 Baja_A3ver18.dwg via
 * tests\test_libredwg_bridge_check.c) el color de LAYER llega con
 * method==0xC3 ("TrueColor-empaquetado", ver bit_upconvert_CMC en
 * src/bits.c de LibreDWG) e index se queda en el valor crudo 256 --
 * el indice ACI real vive en el byte bajo de rgb, no en index. Mismo
 * caso que ya tuvo que resolver a mano dwg_r2004_entity_reader.c's
 * propio read_r2004_table_color antes de que este puente existiera.
 */
static long resolve_aci(const Dwg_Color *c)
{
    if (c->method == 0xC3 && (c->rgb & 0x00FFFFFFUL) <= 255UL)
        return (long)(c->rgb & 0xFFUL);
    return (long)c->index;
}

static unsigned short resolve_color(Dwg_Object_Entity *ent)
{
    long idx = resolve_aci(&ent->color);

    if (idx == 0L || idx == 256L)
    {
        if (ent->layer != NULL && ent->layer->obj != NULL &&
            ent->layer->obj->supertype == DWG_SUPERTYPE_OBJECT &&
            ent->layer->obj->fixedtype == DWG_TYPE_LAYER &&
            ent->layer->obj->tio.object != NULL)
        {
            Dwg_Object_LAYER *lay = ent->layer->obj->tio.object->tio.LAYER;
            if (lay != NULL)
                idx = resolve_aci(&lay->color);
        }
    }
    if (idx < 0L)
        idx = -idx;
    if (idx == 0L || idx == 256L)
        idx = 7L; /* aun sin resolver (capa BYLAYER/BYBLOCK tambien) -- blanco/negro por defecto */

    return (unsigned short)idx;
}

/* Campos de texto (BITCODE_T) leidos por acceso directo al struct --
 * como t->text_value o st->font_file -- vienen CRUDOS: en archivos
 * R2007+ (confirmado en 02_Planta 1 Baja_A3ver18.dwg via un diagnostico
 * que bypasea este puente del todo) el campo es en realidad UTF-16
 * (BITCODE_TU) sin convertir, asi que leerlo como char* normal corta en
 * el primer byte alto 0x00 -- exactamente el sintoma que reporto Arturo
 * ("no estan los fonts definidos"): TEXT.text_value truncado a 1
 * caracter, STYLE.font_file='S' en vez del nombre real. La API publica
 * SI resuelve esto correctamente para nombres de tabla (dwg_obj_table_
 * get_name ya hace bit_convert_TU internamente) pero NO para campos de
 * texto sueltos leidos por puntero directo -- dwg_dynapi_entity_utf8text
 * es la funcion generica de LibreDWG que hace la misma conversion para
 * CUALQUIER campo de texto, dado el nombre dxf del tipo (obj->name,
 * p.ej. "TEXT"/"STYLE") y el nombre del campo. Devuelve una copia nueva
 * SOLO si is_new sale 1 (r2007+) -- solo esa hay que liberar despues.
 */
static char *dyn_utf8(void *entity_struct, const char *dxfname, const char *fieldname, int *is_new)
{
    char *textp = NULL;
    int isnew = 0;

    if (dwg_dynapi_entity_utf8text(entity_struct, dxfname, fieldname, &textp, &isnew, NULL) &&
        textp != NULL)
    {
        *is_new = isnew;
        return textp;
    }
    *is_new = 0;
    return NULL;
}

static const char *resolve_layer_name(Dwg_Object_Entity *ent)
{
    int error = 0;
    char *name = dwg_ent_get_layer_name(ent, &error);

    if (name == NULL || error)
        return "0";
    return name;
}

/* off/frozen: el archivo puede tener capas APAGADAS/CONGELADAS a
 * proposito (confirmado real en CASA_3D.DWG: la capa REF_CARP -- 18
 * entidades, probablemente marcadores de referencia de puertas/
 * ventanas -- viene con frozen=1) -- el puente las ignoraba del todo
 * hasta ahora, mostrando SIEMPRE cada entidad sin importar el estado
 * de su capa (el sintoma que reporto Arturo: "una serie de circulos
 * que no corresponde"). frozen_in_new/locked no afectan visibilidad
 * (frozen_in_new es para bloques nuevos, locked solo bloquea edicion),
 * por eso no se chequean aca.
 */
static int entity_layer_hidden(Dwg_Object_Entity *ent)
{
    Dwg_Object *layer_obj;
    Dwg_Object_LAYER *lay;

    if (ent->layer == NULL || ent->layer->obj == NULL)
        return 0;
    layer_obj = ent->layer->obj;
    if (layer_obj->supertype != DWG_SUPERTYPE_OBJECT || layer_obj->fixedtype != DWG_TYPE_LAYER ||
        layer_obj->tio.object == NULL)
        return 0;
    lay = layer_obj->tio.object->tio.LAYER;
    if (lay == NULL)
        return 0;
    return (lay->off != 0) || (lay->frozen != 0);
}

/* Override deliberado pedido por Arturo (2026-08-26, confirmado
 * explicitamente via pregunta -- NO es una correccion de lectura: el
 * ACI real de la capa "0" en CASA_3D.DWG es 7/blanco BYLAYER puro, sin
 * override en el archivo, confirmado tanto en el LAYER de LibreDWG
 * como en el DXF crudo). La capa "0" (la reservada/default de todo
 * DWG) se usa en este archivo para el grid de referencia -- Arturo la
 * quiere ver cyan en vez del blanco literal del archivo, y
 * explicitamente pidio que el alcance sea SOLO la capa "0" (no todo
 * ACI 7 en general, que paredes/texto siguen usando y debe seguir
 * blanco). */
#define DWG_BRIDGE_LAYER0_OVERRIDE_ACI 4U /* cyan */

/* Convencion real de AutoCAD (confirmada real en CASA_3D2.DWG via un
 * diagnostico comparando el lector DXF nativo -- que SI mostraba
 * PUERTAS_01/VENTANAS_01/SANITARIOS_01/COCINA_01/SILLONES/CAMAS con
 * contenido real -- contra el puente, que los mostraba todos en cero):
 * la geometria DENTRO de una definicion de bloque casi siempre se
 * dibuja en la capa "0" a proposito, para que HEREDE la capa/color de
 * donde sea que el bloque termine insertado (asi una puerta se pinta
 * en el color de PUERTAS_01, una ventana en el de VENTANAS_01, etc,
 * sin que el autor del bloque tenga que duplicar un simbolo por capa).
 * El puente nunca implemento esta herencia -- cada entidad explotada
 * quedaba con su capa/color LITERAL ("0"), que ademas el override de
 * arriba fuerza a cyan, tapando la identidad real de TODO ese
 * contenido (no solo el grid que el override si queria afectar).
 * inherit!=NULL lleva la capa/color de la INSERT (o el DIMENSION)
 * que esta explotando el bloque actual -- se aplica SOLO cuando la
 * entidad hija esta literalmente en capa "0"; si no, se resuelve como
 * siempre (su propia capa real, o el override de capa "0" si por algun
 * motivo NO hay contexto de herencia, ej. una entidad de capa "0" que
 * vive directo en modelspace -- ese es el caso real que el override
 * si debe seguir afectando: el grid).
 */
typedef struct
{
    const char *layer_name;
    unsigned short color;
} DWG_INHERIT_CTX;

static void apply_common_ex(HENTITY e, Dwg_Object_Entity *ent, const DWG_INHERIT_CTX *inherit)
{
    const char *layer_name;
    unsigned short color;

    if (e == NULL)
        return;

    layer_name = resolve_layer_name(ent);
    if (strcmp(layer_name, "0") == 0)
    {
        if (inherit != NULL)
        {
            layer_name = inherit->layer_name;
            color = inherit->color;
        }
        else
        {
            color = DWG_BRIDGE_LAYER0_OVERRIDE_ACI;
        }
    }
    else
    {
        color = resolve_color(ent);
    }

    dwg_entity_put_color(e, color);
    dwg_entity_put_layer(e, layer_name);
}

static void apply_common(HENTITY e, Dwg_Object_Entity *ent)
{
    apply_common_ex(e, ent, NULL);
}

/* `style` es el handle propio de cada entidad de texto (BITCODE_H ==
 * Dwg_Object_Ref* ya resuelto -- TEXT/MTEXT lo traen cada una en su
 * propio struct, no es un campo comun de Dwg_Object_Entity). Se resuelve
 * via dwg_obj_table_get_name (STYLE SI es una "tabla" para
 * dwg_obj_is_table, confirmado en dwg.c) ya que no existe un
 * dwg_ent_get_style_name propio en la API publica de LibreDWG. El
 * nombre de vuelta se registra en la tabla STYLE propia de DWGEngine via
 * populate_styles() antes de este punto -- ver esa funcion para el
 * porque de la fuente real (font_file/bigfont_file) tambien resuelta ahi.
 */
static const char *resolve_style_name(BITCODE_H style)
{
    int error = 0;
    char *name;

    if (style == NULL || style->obj == NULL)
        return "STANDARD";
    name = dwg_obj_table_get_name(style->obj, &error);
    if (name == NULL || error || name[0] == '\0')
        return "STANDARD";
    return name;
}

/* Recorre las tablas STYLE reales de LibreDWG y las registra en el
 * modelo de DWGEngine (dwg_document_add_style + font_file/bigfont_file/
 * altura/factor de ancho/oblicuo) -- sin esto, TEXT/MTEXT quedaban con
 * dwg_text_set_style_name() apuntando a un nombre de estilo que nunca
 * existia en la tabla STYLE del documento, asi que el render no tenia
 * de donde sacar la fuente real (reporte de Arturo: "no estan los fonts
 * definidos"). Misma convencion de campos que ya usa dwg_dxf_reader.c
 * para su propio STYLE (grupo DXF 3 -> font, grupo 4 -> ttf_name).
 */
static void populate_styles(HDWG hDwg, Dwg_Data *dwg)
{
    BITCODE_BL i;

    for (i = 0UL; i < dwg->num_objects; i++)
    {
        Dwg_Object *obj = &dwg->object[i];
        Dwg_Object_STYLE *st;
        HSTYLE hst;
        int error = 0;
        char *name;

        if (obj->supertype != DWG_SUPERTYPE_OBJECT || obj->fixedtype != DWG_TYPE_STYLE ||
            obj->tio.object == NULL)
            continue;
        st = obj->tio.object->tio.STYLE;
        if (st == NULL)
            continue;

        name = dwg_obj_table_get_name(obj, &error);
        if (name == NULL || error || name[0] == '\0')
            continue;

        hst = dwg_document_add_style(hDwg, name);
        if (hst == NULL)
            continue;

        {
            int is_new = 0;
            char *font_u8 = dyn_utf8(st, obj->name, "font_file", &is_new);
            dwg_style_set_font(hst, (font_u8 != NULL) ? font_u8 :
                               ((st->font_file != NULL) ? st->font_file : ""));
            if (is_new) free(font_u8);
        }
        {
            int is_new = 0;
            char *bigfont_u8 = dyn_utf8(st, obj->name, "bigfont_file", &is_new);
            dwg_style_set_ttf_name(hst, (bigfont_u8 != NULL) ? bigfont_u8 :
                                   ((st->bigfont_file != NULL) ? st->bigfont_file : ""));
            if (is_new) free(bigfont_u8);
        }
        if (st->text_size > 0.0)
            dwg_style_set_height(hst, st->text_size);
        if (st->width_factor > 0.0)
            dwg_style_set_width_factor(hst, st->width_factor);
        dwg_style_set_oblique(hst, RAD2DEG(st->oblique_angle));
        /* DXF 71 text generation flags: 2=backward (mirror X), 4=upside-down (mirror Y) */
        dwg_style_set_backward(hst, ((int)st->generation & 2) ? DWG_TRUE : DWG_FALSE);
        dwg_style_set_upside_down(hst, ((int)st->generation & 4) ? DWG_TRUE : DWG_FALSE);
    }
}

/* El puente nunca registraba las capas del archivo en la tabla propia
 * de DWGEngine (dwg_document_add_layer) -- entity_layer_hidden() ya
 * filtraba entidades de capas apagadas/congeladas AL LEER (comparando
 * contra el LAYER crudo de LibreDWG), asi que la visibilidad inicial
 * siempre fue correcta, pero sin una tabla de capas propia no hay nada
 * que listar/alternar despues (el bug real que reporto Arturo: el
 * dialogo de Capas, pedido 2026-08-26, sale vacio en cualquier DWG real
 * -- ver dwg_render.c/dwg_layers_dlg.prg). Mismo patron que
 * populate_styles de arriba: recorre dwg->object[] una vez, ANTES del
 * loop principal de entidades, para que dwg_document_get_layer ya
 * funcione para cualquier entidad que se agregue despues. */
static void populate_layers(HDWG hDwg, Dwg_Data *dwg)
{
    BITCODE_BL i;

    for (i = 0UL; i < dwg->num_objects; i++)
    {
        Dwg_Object *obj = &dwg->object[i];
        Dwg_Object_LAYER *lay;
        HLAYER hlay;
        int error = 0;
        char *name;

        if (obj->supertype != DWG_SUPERTYPE_OBJECT || obj->fixedtype != DWG_TYPE_LAYER ||
            obj->tio.object == NULL)
            continue;
        lay = obj->tio.object->tio.LAYER;
        if (lay == NULL)
            continue;

        name = dwg_obj_table_get_name(obj, &error);
        if (name == NULL || error || name[0] == '\0')
            continue;

        hlay = dwg_document_add_layer(hDwg, name);
        if (hlay == NULL)
            continue;

        dwg_layer_set_color(hlay, (unsigned short)resolve_aci(&lay->color));
        dwg_layer_set_off(hlay, (lay->off != 0) ? DWG_TRUE : DWG_FALSE);
        dwg_layer_set_frozen(hlay, (lay->frozen != 0) ? DWG_TRUE : DWG_FALSE);
        dwg_layer_set_locked(hlay, (lay->locked != 0) ? DWG_TRUE : DWG_FALSE);
    }
}

static void bridge_line(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_LINE *l = obj->tio.entity->tio.LINE;
    HENTITY e = dwg_add_line(hDwg, l->start.x, l->start.y, l->start.z,
                             l->end.x, l->end.y, l->end.z);
    apply_common(e, obj->tio.entity);
}

static void bridge_circle(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_CIRCLE *c = obj->tio.entity->tio.CIRCLE;
    HENTITY e = dwg_add_circle(hDwg, c->center.x, c->center.y, c->center.z, c->radius);
    apply_common(e, obj->tio.entity);
}

static void bridge_arc(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_ARC *a = obj->tio.entity->tio.ARC;
    HENTITY e = dwg_add_arc(hDwg, a->center.x, a->center.y, a->center.z, a->radius,
                            RAD2DEG(a->start_angle), RAD2DEG(a->end_angle));
    apply_common(e, obj->tio.entity);
}

static void bridge_point(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_POINT *p = obj->tio.entity->tio.POINT;
    HENTITY e = dwg_add_point(hDwg, p->x, p->y, p->z);
    apply_common(e, obj->tio.entity);
}

static void bridge_solid(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_SOLID *s = obj->tio.entity->tio.SOLID;
    double elev = s->elevation;
    HENTITY e = dwg_add_solid(hDwg,
                              s->corner1.x, s->corner1.y, elev,
                              s->corner2.x, s->corner2.y, elev,
                              s->corner3.x, s->corner3.y, elev,
                              s->corner4.x, s->corner4.y, elev);
    apply_common(e, obj->tio.entity);
}

/* 3DFACE: a diferencia de SOLID, sus 4 corners traen Z real -- no hay un
 * campo "elevation" comun, cada corner es un BITCODE_3BD independiente.
 * invis_flags (DXF 70: 1/2/4/8 por arista) se copia directo a DWGEngine's
 * propio edge_flags -- mismo bit-a-bit, ver dwg_face_set_edge_flags. */
static void bridge_face(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity__3DFACE *f = obj->tio.entity->tio._3DFACE;
    HENTITY e = dwg_add_face(hDwg,
                             f->corner1.x, f->corner1.y, f->corner1.z,
                             f->corner2.x, f->corner2.y, f->corner2.z,
                             f->corner3.x, f->corner3.y, f->corner3.z,
                             f->corner4.x, f->corner4.y, f->corner4.z);
    if (e != NULL)
        dwg_face_set_edge_flags(e, (unsigned short)f->invis_flags);
    apply_common(e, obj->tio.entity);
}

static void bridge_text(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_TEXT *t = obj->tio.entity->tio.TEXT;
    int is_new = 0;
    char *text_u8 = dyn_utf8(t, obj->name, "text_value", &is_new);
    const char *text = (text_u8 != NULL) ? text_u8 : ((t->text_value != NULL) ? t->text_value : "");
    HENTITY e = dwg_add_text(hDwg, t->ins_pt.x, t->ins_pt.y, t->elevation,
                             t->height, RAD2DEG(t->rotation), text);
    apply_common(e, obj->tio.entity);
    if (e != NULL)
        dwg_text_set_style_name(e, resolve_style_name(t->style));
    if (is_new) free(text_u8);
}

static void bridge_mtext(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_MTEXT *m = obj->tio.entity->tio.MTEXT;
    int is_new = 0;
    char *text_u8 = dyn_utf8(m, obj->name, "text", &is_new);
    const char *text = (text_u8 != NULL) ? text_u8 : ((m->text != NULL) ? m->text : "");
    HENTITY e = dwg_add_mtext(hDwg, m->ins_pt.x, m->ins_pt.y, m->ins_pt.z,
                              m->text_height, m->rect_width, text);
    apply_common(e, obj->tio.entity);
    if (e != NULL)
        dwg_mtext_set_style_name(e, resolve_style_name(m->style));
    if (is_new) free(text_u8);
}

static void bridge_lwpolyline(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_LWPOLYLINE *pl = obj->tio.entity->tio.LWPOLYLINE;
    HENTITY e;
    HPOLYLINE hpl;
    unsigned long i;

    if (pl->num_points < 2UL || pl->points == NULL)
        return;

    e = dwg_add_polyline(hDwg);
    hpl = dwg_polyline_from_entity(e);
    if (hpl == NULL)
        return;

    for (i = 0UL; i < pl->num_points; i++)
    {
        double bulge = (pl->bulges != NULL && i < pl->num_bulges) ? pl->bulges[i] : 0.0;
        dwg_polyline_add_vertex2(hpl, pl->points[i].x, pl->points[i].y, pl->elevation,
                                 bulge, 0.0, 0.0);
    }
    dwg_polyline_set_closed(hpl, (pl->flag & 512) ? DWG_TRUE : DWG_FALSE);
    dwg_polyline_set_elevation(hpl, pl->elevation);
    apply_common(e, obj->tio.entity);
}

static double hatch_arc_bulge(const Dwg_HATCH_PathSeg *seg)
{
    double sweep;
    if (seg->is_ccw)
    {
        sweep = seg->end_angle - seg->start_angle;
        if (sweep < 0.0) sweep += 2.0 * M_PI;
    }
    else
    {
        sweep = seg->start_angle - seg->end_angle;
        if (sweep < 0.0) sweep += 2.0 * M_PI;
        sweep = -sweep;
    }
    return tan(sweep / 4.0);
}

/* HATCH.paths[]: cada loop puede venir como "polyline path" (point+bulge,
 * igual que LWPOLYLINE) o como "segment path" (lista de LINE/ARC/ELLIPSE/
 * SPLINE) -- ver Dwg_HATCH_Path en dwg.h. DWGEngine solo modela UN loop
 * de boundary por HENTITY (dwg_hatch.h), asi que cada path[] de LibreDWG
 * se vuelca a su propio dwg_add_hatch, mismo patron que ya usaba
 * dwg_r2004_entity_reader.c's decode_hatch para multiples loops.
 */
static void bridge_hatch(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_HATCH *h = obj->tio.entity->tio.HATCH;
    unsigned long p;

    int is_new = 0;
    char *name_u8;
    const char *pattern_name;

    if (h->num_paths == 0UL || h->paths == NULL)
        return;

    name_u8 = dyn_utf8(h, obj->name, "name", &is_new);
    pattern_name = (name_u8 != NULL) ? name_u8 : ((h->name != NULL) ? h->name : "SOLID");

    for (p = 0UL; p < h->num_paths; p++)
    {
        Dwg_HATCH_Path *path = &h->paths[p];
        HENTITY e;
        unsigned long added = 0UL;
        unsigned long n = (unsigned long)path->num_segs_or_paths;
        unsigned long k;

        if (n == 0UL)
            continue;
        /* is_textbox (flag 0x8): un rectangulo que marca una zona a EXCLUIR
         * del patron/relleno (donde va una etiqueta de texto dentro del
         * hatch), no un loop de relleno real -- confirmado en un archivo
         * real (ROTATORIO.dwg) via diagnostico crudo: justo este path
         * tenia una forma irregular de 4 lineas totalmente distinta de
         * los demas loops rectangulares del mismo HATCH. Crearlo como su
         * propio dwg_add_hatch relleno produce un cuadrilatero solido
         * extra que no deberia estar ahi (el defecto de HATCH que
         * reporto Arturo). */
        if (path->flag & 8UL)
            continue;

        e = dwg_add_hatch(hDwg, pattern_name,
                          h->angle, h->scale_spacing, h->is_solid_fill ? DWG_TRUE : DWG_FALSE);
        if (e == NULL)
            continue;

        if ((path->flag & 2UL) && path->polyline_paths != NULL)
        {
            for (k = 0UL; k < n; k++)
            {
                HVERTEX v = dwg_hatch_add_boundary_point(e, path->polyline_paths[k].point.x,
                                                          path->polyline_paths[k].point.y, 0.0);
                if (v != NULL && path->polyline_paths[k].bulge != 0.0)
                    dwg_vertex_set_bulge(v, path->polyline_paths[k].bulge);
                added++;
            }
        }
        else if (path->segs != NULL)
        {
            for (k = 0UL; k < n; k++)
            {
                Dwg_HATCH_PathSeg *seg = &path->segs[k];
                double px, py, bulge = 0.0;
                HVERTEX v;

                switch (seg->curve_type)
                {
                case 1: /* LINE */
                    px = seg->first_endpoint.x;
                    py = seg->first_endpoint.y;
                    break;
                case 2: /* CIRCULAR ARC */
                    px = seg->center.x + seg->radius * cos(seg->start_angle);
                    py = seg->center.y + seg->radius * sin(seg->start_angle);
                    bulge = hatch_arc_bulge(seg);
                    break;
                case 3: /* ELLIPTICAL ARC -- aproximada, sin angulos propios en el struct */
                    px = seg->center.x;
                    py = seg->center.y;
                    break;
                default: /* SPLINE u otro -- mejor esfuerzo con el primer punto de control */
                    if (seg->num_control_points > 0UL && seg->control_points != NULL)
                    {
                        px = seg->control_points[0].point.x;
                        py = seg->control_points[0].point.y;
                    }
                    else
                    {
                        continue;
                    }
                    break;
                }
                v = dwg_hatch_add_boundary_point(e, px, py, 0.0);
                if (v != NULL && bulge != 0.0)
                    dwg_vertex_set_bulge(v, bulge);
                added++;
            }
        }

        if (added > 0UL)
            apply_common(e, obj->tio.entity);
    }

    if (is_new) free(name_u8);
}

static void bridge_polyline_2d(HDWG hDwg, Dwg_Object *obj)
{
    int error = 0;
    BITCODE_BL n = dwg_object_polyline_2d_get_numpoints(obj, &error);
    dwg_point_2d *pts;
    Dwg_Entity_POLYLINE_2D *pl = obj->tio.entity->tio.POLYLINE_2D;
    HENTITY e;
    HPOLYLINE hpl;
    BITCODE_BL i;

    if (error || n < 2UL)
        return;
    pts = dwg_object_polyline_2d_get_points(obj, &error);
    if (error || pts == NULL)
        return;

    e = dwg_add_polyline(hDwg);
    hpl = dwg_polyline_from_entity(e);
    if (hpl == NULL)
    {
        free(pts);
        return;
    }

    for (i = 0UL; i < n; i++)
        dwg_polyline_add_vertex2(hpl, pts[i].x, pts[i].y, pl->elevation, 0.0, 0.0, 0.0);
    free(pts);

    dwg_polyline_set_closed(hpl, (pl->flag & 1) ? DWG_TRUE : DWG_FALSE);
    dwg_polyline_set_elevation(hpl, pl->elevation);
    apply_common(e, obj->tio.entity);
}

static void bridge_polyline_3d(HDWG hDwg, Dwg_Object *obj)
{
    int error = 0;
    BITCODE_BL n = dwg_object_polyline_3d_get_numpoints(obj, &error);
    dwg_point_3d *pts;
    Dwg_Entity_POLYLINE_3D *pl = obj->tio.entity->tio.POLYLINE_3D;
    HENTITY e;
    HPOLYLINE hpl;
    BITCODE_BL i;

    if (error || n < 2UL)
        return;
    pts = dwg_object_polyline_3d_get_points(obj, &error);
    if (error || pts == NULL)
        return;

    e = dwg_add_polyline(hDwg);
    hpl = dwg_polyline_from_entity(e);
    if (hpl == NULL)
    {
        free(pts);
        return;
    }

    for (i = 0UL; i < n; i++)
        dwg_polyline_add_vertex2(hpl, pts[i].x, pts[i].y, pts[i].z, 0.0, 0.0, 0.0);
    free(pts);

    dwg_polyline_set_closed(hpl, (pl->flag & 1) ? DWG_TRUE : DWG_FALSE);
    apply_common(e, obj->tio.entity);
}

/* POLYLINE_MESH: malla M x N de vertices 3D reales (AcDbPolygonMesh) --
 * confirmado real en CASA_3D.DWG (84 entidades, casi seguro las cerchas
 * del techo y el detalle curvo de sanitarios/muebles que se ven en la
 * imagen de referencia de Arturo -- capa CUBIERTA declarada pero sin
 * contenido, asi que esa geometria vive en otra capa dentro de esta
 * malla). DWGEngine no tiene un tipo de entidad "malla" propio -- se
 * sintetiza como wireframe: una LINE por cada segmento adyacente en
 * direccion M y otra en direccion N, formando la reticula completa
 * (sin relleno, mismo criterio wireframe-only que 3DFACE/SOLID en modo
 * 3D, confirmado por Arturo como la forma correcta de verse). Ni cierre
 * M/N (flag de malla cerrada) esta modelado.
 *
 * Dos caminos para leer los vertices: el arreglo `vertex[]` (handles ya
 * resueltos, esperado en R2004+) es el camino directo -- pero
 * confirmado real que CASA_3D.DWG (AC1032/R2018, deberia ser R2004+)
 * de todos modos trae `num_owned=0`/`vertex=NULL` para sus MESH, con
 * los datos reales solo alcanzables por la cadena vieja
 * first_vertex->next_entity->...->last_vertex (el enlazado de
 * entidades R13-R2000). Se intenta el arreglo primero y se cae a la
 * cadena si no esta disponible, en vez de asumir la version determina
 * cual camino usar.
 */
/* dwg_next_object() (API publica de LibreDWG) es el camino REAL para
 * caminar R13-R2000's cadena de vertices -- confirmado leyendo
 * dwg_object_polyline_2d_get_points/numpoints, que usan exactamente
 * este mismo patron para su propio camino pre-R2004. Un primer intento
 * propio caminando next_entity a mano se cortaba a los 2 pasos (de 18
 * vertices esperados) -- next_entity no es la cadena real que hay que
 * seguir aca, dwg_next_object si. */
static unsigned long walk_mesh_vertex_chain(Dwg_Object_Ref *first, Dwg_Object_Ref *last,
                                             DWG_POINT3D *pts, unsigned long max)
{
    Dwg_Object *vobj = (first != NULL) ? first->obj : NULL;
    Dwg_Object *last_obj = (last != NULL) ? last->obj : NULL;
    unsigned long count = 0UL;

    while (vobj != NULL && count < max)
    {
        if (vobj->supertype == DWG_SUPERTYPE_ENTITY && vobj->tio.entity != NULL &&
            (vobj->fixedtype == DWG_TYPE_VERTEX_MESH || vobj->fixedtype == DWG_TYPE_VERTEX_3D) &&
            vobj->tio.entity->tio.VERTEX_MESH != NULL)
        {
            Dwg_Entity_VERTEX_MESH *v = vobj->tio.entity->tio.VERTEX_MESH;
            pts[count].x = v->point.x; pts[count].y = v->point.y; pts[count].z = v->point.z;
            count++;
        }
        if (vobj == last_obj)
            break;
        vobj = dwg_next_object(vobj);
    }

    return count;
}

static void bridge_polyline_mesh(HDWG hDwg, Dwg_Object *obj)
{
    Dwg_Entity_POLYLINE_MESH *pm = obj->tio.entity->tio.POLYLINE_MESH;
    unsigned long m = (unsigned long)pm->num_m_verts;
    unsigned long n = (unsigned long)pm->num_n_verts;
    unsigned long total = m * n;
    unsigned long i, j, count;
    DWG_POINT3D *pts;

    if (m < 1UL || n < 1UL || total == 0UL)
        return;

    pts = (DWG_POINT3D *)malloc(total * sizeof(DWG_POINT3D));
    if (pts == NULL)
        return;

    if (pm->vertex != NULL && (unsigned long)pm->num_owned >= total)
    {
        for (i = 0UL; i < total; i++)
        {
            Dwg_Object *vobj = (pm->vertex[i] != NULL) ? pm->vertex[i]->obj : NULL;
            if (vobj != NULL && vobj->supertype == DWG_SUPERTYPE_ENTITY && vobj->tio.entity != NULL &&
                vobj->tio.entity->tio.VERTEX_MESH != NULL)
            {
                Dwg_Entity_VERTEX_MESH *v = vobj->tio.entity->tio.VERTEX_MESH;
                pts[i].x = v->point.x; pts[i].y = v->point.y; pts[i].z = v->point.z;
            }
            else
            {
                pts[i].x = pts[i].y = pts[i].z = 0.0;
            }
        }
        count = total;
    }
    else
    {
        count = walk_mesh_vertex_chain(pm->first_vertex, pm->last_vertex, pts, total);
    }

    if (count < total)
    {
        free(pts); /* malla incompleta -- mejor nada que una reticula rota */
        return;
    }

    for (i = 0UL; i < m; i++)
    {
        for (j = 0UL; j + 1UL < n; j++)
        {
            DWG_POINT3D *a = &pts[i * n + j], *b = &pts[i * n + j + 1];
            HENTITY e = dwg_add_line(hDwg, a->x, a->y, a->z, b->x, b->y, b->z);
            apply_common(e, obj->tio.entity);
        }
    }
    for (j = 0UL; j < n; j++)
    {
        for (i = 0UL; i + 1UL < m; i++)
        {
            DWG_POINT3D *a = &pts[i * n + j], *b = &pts[(i + 1) * n + j];
            HENTITY e = dwg_add_line(hDwg, a->x, a->y, a->z, b->x, b->y, b->z);
            apply_common(e, obj->tio.entity);
        }
    }

    free(pts);
}

/* --------------------------------------------------------------------
 * Contenido de BLOCK_HEADER (bloques nombrados via INSERT, y el bloque
 * anonimo de flechas/texto que cada DIMENSION trae): dwg_render.c NO
 * camina INSERT en tiempo de render (no existe ese caso en su switch),
 * asi que este puente lo "explota" en tiempo de carga -- transforma
 * cada entidad del bloque (traslacion por el punto de insercion,
 * escala, rotacion) y la agrega directo al modelspace ya en su
 * posicion final, mismo enfoque ("explode_block_ref_r2004") que ya
 * usaba dwg_r2004_entity_reader.c's propio decode_dimension.
 * -------------------------------------------------------------------- */
typedef struct
{
    double base_x, base_y;
    double ins_x, ins_y, ins_z;
    double sx, sy;
    double cr, sr;
    double rot;
} BlockXform;

static void xform_make(BlockXform *tf, double base_x, double base_y,
                       double ins_x, double ins_y, double ins_z,
                       double sx, double sy, double rot)
{
    tf->base_x = base_x;
    tf->base_y = base_y;
    tf->ins_x = ins_x;
    tf->ins_y = ins_y;
    tf->ins_z = ins_z;
    tf->sx = (sx != 0.0) ? sx : 1.0;
    tf->sy = (sy != 0.0) ? sy : 1.0;
    tf->rot = rot;
    tf->cr = cos(rot);
    tf->sr = sin(rot);
}

static void xform_pt(const BlockXform *tf, double x, double y, double *ox, double *oy)
{
    double dx = (x - tf->base_x) * tf->sx;
    double dy = (y - tf->base_y) * tf->sy;
    *ox = tf->ins_x + dx * tf->cr - dy * tf->sr;
    *oy = tf->ins_y + dx * tf->sr + dy * tf->cr;
}

/* factor isotropico aproximado para radios/alturas -- exacto cuando
 * sx==sy (el caso normal para bloques de flecha de cota y la gran
 * mayoria de bloques simbolo reales); una escala no-uniforme real solo
 * deja el tamano un poco distorsionado, nunca la posicion. */
static double xform_scalar(const BlockXform *tf)
{
    return sqrt(fabs(tf->sx * tf->sy));
}

/* adelantadas: explode_entity necesita recursion (INSERT anidado, ver
 * su propio case DWG_TYPE_INSERT mas abajo) hacia funciones definidas
 * despues de ella en este archivo. */
static void explode_block(HDWG hDwg, Dwg_Data *dwg, Dwg_Object *block_hdr_obj, const BlockXform *tf,
                          int depth, const DWG_INHERIT_CTX *inherit);
static void block_base_point(Dwg_Object *block_hdr_obj, double *base_x, double *base_y);

/* limite defensivo contra una referencia de bloque circular (A inserta
 * B inserta A) -- un archivo real nunca necesita mas de un par de
 * niveles de anidamiento (ej. MESA-C -> SILLA), 16 da margen de sobra
 * sin arriesgar un desborde de pila con un archivo corrupto/adversario. */
#define DWG_BRIDGE_MAX_NESTED_INSERT_DEPTH 16

static void explode_entity(HDWG hDwg, Dwg_Data *dwg, Dwg_Object *obj, const BlockXform *tf, int depth,
                           const DWG_INHERIT_CTX *inherit)
{
    Dwg_Object_Entity *ent = obj->tio.entity;
    HENTITY e = NULL;

    if (entity_layer_hidden(ent))
        return;

    switch (obj->fixedtype)
    {
    case DWG_TYPE_INSERT:
    {
        /* INSERT anidado (un bloque referenciando otro -- ej. confirmado
         * real en CASA_3D.DWG: el bloque MESA-C trae 2 INSERT propios,
         * presumiblemente las sillas). Sin este caso, ese contenido
         * jamas llegaba a dwg_render.c -- default: rompe el switch sin
         * crear nada, dejando solo lo que sobraba del bloque contenedor
         * (el sintoma real que reporto Arturo: muebles redondos
         * reducidos a un simple circulo). El punto de insercion del
         * INSERT anidado vive en el sistema LOCAL del bloque contenedor,
         * asi que se transforma con el tf de AFUERA (xform_pt ya hace
         * resta-de-base+escala+rotacion+traslacion); rotacion/escala se
         * componen sumando/multiplicando -- exacto sin distorsion de
         * shear, misma aproximacion que xform_scalar ya usa. No crea
         * entidad propia: su contenido se agrega recursivamente. */
        Dwg_Entity_INSERT *ins = ent->tio.INSERT;
        Dwg_Object *nested_blk;
        double base_x, base_y, nx, ny;
        BlockXform nested_tf;
        DWG_INHERIT_CTX nested_inherit_storage;
        const DWG_INHERIT_CTX *nested_inherit;
        const char *ins_layer;

        if (dwg == NULL || ins->block_header == NULL || ins->block_header->obj == NULL ||
            depth >= DWG_BRIDGE_MAX_NESTED_INSERT_DEPTH)
            break;
        nested_blk = ins->block_header->obj;
        xform_pt(tf, ins->ins_pt.x, ins->ins_pt.y, &nx, &ny);
        block_base_point(nested_blk, &base_x, &base_y);
        xform_make(&nested_tf, base_x, base_y, nx, ny, tf->ins_z,
                  tf->sx * ins->scale.x, tf->sy * ins->scale.y, tf->rot + ins->rotation);

        /* misma herencia de capa "0" que apply_common_ex: si ESTE INSERT
         * anidado esta a su vez en capa "0", sigue heredando de mas
         * afuera (propaga inherit); si no, su propia capa/color se
         * vuelve el contexto para SU contenido. */
        ins_layer = resolve_layer_name(ent);
        if (strcmp(ins_layer, "0") == 0)
        {
            nested_inherit = inherit;
        }
        else
        {
            nested_inherit_storage.layer_name = ins_layer;
            nested_inherit_storage.color = resolve_color(ent);
            nested_inherit = &nested_inherit_storage;
        }

        explode_block(hDwg, dwg, nested_blk, &nested_tf, depth + 1, nested_inherit);
        break;
    }
    case DWG_TYPE_LINE:
    {
        Dwg_Entity_LINE *l = ent->tio.LINE;
        double x1, y1, x2, y2;
        xform_pt(tf, l->start.x, l->start.y, &x1, &y1);
        xform_pt(tf, l->end.x, l->end.y, &x2, &y2);
        e = dwg_add_line(hDwg, x1, y1, tf->ins_z, x2, y2, tf->ins_z);
        break;
    }
    case DWG_TYPE_SOLID:
    {
        /* SOLID.corner1..4 son 2RD (solo x,y) -- a diferencia de FACE,
         * ACA si hay un solo plano Z real para todo el quad (su propio
         * campo elevation), no perdida de detalle por achatar. Se suma
         * tf->ins_z + s->elevation en vez de solo tf->ins_z -- el
         * elevation propio se estaba descartando antes, aunque el
         * impacto practico sea chico (SOLID casi siempre en elevation
         * 0 para rellenos/hatching). */
        Dwg_Entity_SOLID *s = ent->tio.SOLID;
        double x1, y1, x2, y2, x3, y3, x4, y4;
        double sz = tf->ins_z + s->elevation;
        xform_pt(tf, s->corner1.x, s->corner1.y, &x1, &y1);
        xform_pt(tf, s->corner2.x, s->corner2.y, &x2, &y2);
        xform_pt(tf, s->corner3.x, s->corner3.y, &x3, &y3);
        xform_pt(tf, s->corner4.x, s->corner4.y, &x4, &y4);
        e = dwg_add_solid(hDwg, x1, y1, sz, x2, y2, sz, x3, y3, sz, x4, y4, sz);
        break;
    }
    case DWG_TYPE__3DFACE:
    {
        /* NO se achata a tf->ins_z (a diferencia de SOLID abajo) -- real,
         * confirmado en CASA_3D.DWG: los sanitarios (SBAN1703/SLAVICT3,
         * capa SANITARIOS_01) se arman de DECENAS de 3DFACE chicas con
         * Z real por esquina, no de POLYLINE_MESH -- son exactamente lo
         * que le da forma curva/redondeada al inodoro/lavabo. Achatar
         * su Z (como se hacia antes) los deja perfectamente PLANOS --
         * el reporte real de Arturo ("falta el 3d de sanitarios"). Se
         * suma tf->ins_z como offset y se preserva la Z propia de cada
         * esquina, mismo criterio ya aplicado a POLYLINE_MESH arriba. */
        Dwg_Entity__3DFACE *f = ent->tio._3DFACE;
        double x1, y1, x2, y2, x3, y3, x4, y4;
        xform_pt(tf, f->corner1.x, f->corner1.y, &x1, &y1);
        xform_pt(tf, f->corner2.x, f->corner2.y, &x2, &y2);
        xform_pt(tf, f->corner3.x, f->corner3.y, &x3, &y3);
        xform_pt(tf, f->corner4.x, f->corner4.y, &x4, &y4);
        e = dwg_add_face(hDwg, x1, y1, tf->ins_z + f->corner1.z, x2, y2, tf->ins_z + f->corner2.z,
                         x3, y3, tf->ins_z + f->corner3.z, x4, y4, tf->ins_z + f->corner4.z);
        if (e != NULL)
            dwg_face_set_edge_flags(e, (unsigned short)f->invis_flags);
        break;
    }
    case DWG_TYPE_POLYLINE_MESH:
    {
        /* A diferencia de SOLID/3DFACE arriba, aca NO se achata a
         * tf->ins_z -- el sentido entero de una malla es su Z propia
         * por vertice (el detalle curvo real de sanitarios/muebles,
         * confirmado real: casi todas las 84 mallas de CASA_3D.DWG
         * viven DENTRO de bloques de sanitarios/muebles, no sueltas en
         * modelspace -- sin este caso, `default: return;` las
         * descartaba en silencio, dejando solo el contorno 2D plano
         * del simbolo visible -- exactamente lo que reporto Arturo:
         * "los muebles y sanitarios estan en 2D y son 3D"). Se suma
         * tf->ins_z como offset (BlockXform no tiene escala de Z,
         * misma simplificacion 2D-en-XY-pero-Z-real de siempre). */
        Dwg_Entity_POLYLINE_MESH *pm = ent->tio.POLYLINE_MESH;
        unsigned long mm = (unsigned long)pm->num_m_verts;
        unsigned long nn = (unsigned long)pm->num_n_verts;
        unsigned long mtotal = mm * nn;
        unsigned long mi, mj, mcount;
        DWG_POINT3D *mpts;

        if (mm < 1UL || nn < 1UL || mtotal == 0UL)
            break;
        mpts = (DWG_POINT3D *)malloc(mtotal * sizeof(DWG_POINT3D));
        if (mpts == NULL)
            break;

        if (pm->vertex != NULL && (unsigned long)pm->num_owned >= mtotal)
        {
            for (mi = 0UL; mi < mtotal; mi++)
            {
                Dwg_Object *vobj = (pm->vertex[mi] != NULL) ? pm->vertex[mi]->obj : NULL;
                if (vobj != NULL && vobj->supertype == DWG_SUPERTYPE_ENTITY && vobj->tio.entity != NULL &&
                    vobj->tio.entity->tio.VERTEX_MESH != NULL)
                {
                    Dwg_Entity_VERTEX_MESH *v = vobj->tio.entity->tio.VERTEX_MESH;
                    mpts[mi].x = v->point.x; mpts[mi].y = v->point.y; mpts[mi].z = v->point.z;
                }
                else
                {
                    mpts[mi].x = mpts[mi].y = mpts[mi].z = 0.0;
                }
            }
            mcount = mtotal;
        }
        else
        {
            mcount = walk_mesh_vertex_chain(pm->first_vertex, pm->last_vertex, mpts, mtotal);
        }

        if (mcount < mtotal)
        {
            free(mpts);
            break;
        }

        for (mi = 0UL; mi < mm; mi++)
        {
            for (mj = 0UL; mj + 1UL < nn; mj++)
            {
                double ax, ay, bx, by;
                HENTITY me;
                xform_pt(tf, mpts[mi * nn + mj].x, mpts[mi * nn + mj].y, &ax, &ay);
                xform_pt(tf, mpts[mi * nn + mj + 1].x, mpts[mi * nn + mj + 1].y, &bx, &by);
                me = dwg_add_line(hDwg, ax, ay, tf->ins_z + mpts[mi * nn + mj].z,
                                  bx, by, tf->ins_z + mpts[mi * nn + mj + 1].z);
                apply_common_ex(me, ent, inherit);
            }
        }
        for (mj = 0UL; mj < nn; mj++)
        {
            for (mi = 0UL; mi + 1UL < mm; mi++)
            {
                double ax, ay, bx, by;
                HENTITY me;
                xform_pt(tf, mpts[mi * nn + mj].x, mpts[mi * nn + mj].y, &ax, &ay);
                xform_pt(tf, mpts[(mi + 1) * nn + mj].x, mpts[(mi + 1) * nn + mj].y, &bx, &by);
                me = dwg_add_line(hDwg, ax, ay, tf->ins_z + mpts[mi * nn + mj].z,
                                  bx, by, tf->ins_z + mpts[(mi + 1) * nn + mj].z);
                apply_common_ex(me, ent, inherit);
            }
        }

        free(mpts);
        break;
    }
    case DWG_TYPE_CIRCLE:
    {
        Dwg_Entity_CIRCLE *c = ent->tio.CIRCLE;
        double cx, cy;
        xform_pt(tf, c->center.x, c->center.y, &cx, &cy);
        e = dwg_add_circle(hDwg, cx, cy, tf->ins_z, c->radius * xform_scalar(tf));
        break;
    }
    case DWG_TYPE_ARC:
    {
        Dwg_Entity_ARC *a = ent->tio.ARC;
        double cx, cy;
        xform_pt(tf, a->center.x, a->center.y, &cx, &cy);
        e = dwg_add_arc(hDwg, cx, cy, tf->ins_z, a->radius * xform_scalar(tf),
                        RAD2DEG(a->start_angle + tf->rot), RAD2DEG(a->end_angle + tf->rot));
        break;
    }
    case DWG_TYPE_TEXT:
    {
        Dwg_Entity_TEXT *t = ent->tio.TEXT;
        int is_new = 0;
        char *text_u8 = dyn_utf8(t, obj->name, "text_value", &is_new);
        const char *text = (text_u8 != NULL) ? text_u8 : ((t->text_value != NULL) ? t->text_value : "");
        double px, py;
        xform_pt(tf, t->ins_pt.x, t->ins_pt.y, &px, &py);
        e = dwg_add_text(hDwg, px, py, tf->ins_z, t->height * xform_scalar(tf),
                         RAD2DEG(t->rotation + tf->rot), text);
        if (e != NULL)
            dwg_text_set_style_name(e, resolve_style_name(t->style));
        if (is_new) free(text_u8);
        break;
    }
    case DWG_TYPE_MTEXT:
    {
        Dwg_Entity_MTEXT *m = ent->tio.MTEXT;
        int is_new = 0;
        char *text_u8 = dyn_utf8(m, obj->name, "text", &is_new);
        const char *text = (text_u8 != NULL) ? text_u8 : ((m->text != NULL) ? m->text : "");
        double px, py;
        xform_pt(tf, m->ins_pt.x, m->ins_pt.y, &px, &py);
        e = dwg_add_mtext(hDwg, px, py, tf->ins_z, m->text_height * xform_scalar(tf),
                          m->rect_width * xform_scalar(tf), text);
        if (e != NULL)
            dwg_mtext_set_style_name(e, resolve_style_name(m->style));
        if (is_new) free(text_u8);
        break;
    }
    case DWG_TYPE_LWPOLYLINE:
    {
        Dwg_Entity_LWPOLYLINE *pl = ent->tio.LWPOLYLINE;
        HPOLYLINE hpl;
        unsigned long i;
        if (pl->num_points < 2UL || pl->points == NULL)
            return;
        e = dwg_add_polyline(hDwg);
        hpl = dwg_polyline_from_entity(e);
        if (hpl == NULL)
            return;
        for (i = 0UL; i < pl->num_points; i++)
        {
            double px, py;
            double bulge = (pl->bulges != NULL && i < pl->num_bulges) ? pl->bulges[i] : 0.0;
            xform_pt(tf, pl->points[i].x, pl->points[i].y, &px, &py);
            dwg_polyline_add_vertex2(hpl, px, py, tf->ins_z, bulge, 0.0, 0.0);
        }
        dwg_polyline_set_closed(hpl, (pl->flag & 512) ? DWG_TRUE : DWG_FALSE);
        break;
    }
    default:
        return; /* tipos raros dentro de bloques (HATCH/POLYLINE vieja/etc) -- no modelados aca todavia */
    }

    apply_common_ex(e, ent, inherit);
}

static void explode_block(HDWG hDwg, Dwg_Data *dwg, Dwg_Object *block_hdr_obj, const BlockXform *tf,
                          int depth, const DWG_INHERIT_CTX *inherit)
{
    BITCODE_BL i;

    if (block_hdr_obj == NULL || block_hdr_obj->tio.object == NULL)
        return;

    for (i = 0UL; i < dwg->num_objects; i++)
    {
        Dwg_Object *obj = &dwg->object[i];
        if (obj->supertype != DWG_SUPERTYPE_ENTITY || obj->tio.entity == NULL)
            continue;
        if (obj->tio.entity->ownerhandle == NULL || obj->tio.entity->ownerhandle->obj != block_hdr_obj)
            continue;
        explode_entity(hDwg, dwg, obj, tf, depth, inherit);
    }
}

static void block_base_point(Dwg_Object *block_hdr_obj, double *base_x, double *base_y)
{
    *base_x = 0.0;
    *base_y = 0.0;
    if (block_hdr_obj != NULL && block_hdr_obj->tio.object != NULL &&
        block_hdr_obj->tio.object->tio.BLOCK_HEADER != NULL)
    {
        *base_x = block_hdr_obj->tio.object->tio.BLOCK_HEADER->base_pt.x;
        *base_y = block_hdr_obj->tio.object->tio.BLOCK_HEADER->base_pt.y;
    }
}

/* INSERT real (bloques nombrados -- muebles, simbolos, etc). Se explota
 * directo a modelspace en vez de crear un DWG_ENTITY_INSERT propio,
 * porque dwg_render.c no sabe caminar ese tipo de entidad todavia. */
static void bridge_insert(HDWG hDwg, Dwg_Data *dwg, Dwg_Object *obj)
{
    Dwg_Entity_INSERT *ins = obj->tio.entity->tio.INSERT;
    Dwg_Object *blk;
    double base_x, base_y;
    BlockXform tf;
    DWG_INHERIT_CTX inherit_storage;
    const DWG_INHERIT_CTX *inherit;
    const char *ins_layer;

    if (ins->block_header == NULL || ins->block_header->obj == NULL)
        return;
    blk = ins->block_header->obj;
    block_base_point(blk, &base_x, &base_y);
    xform_make(&tf, base_x, base_y, ins->ins_pt.x, ins->ins_pt.y, ins->ins_pt.z,
              ins->scale.x, ins->scale.y, ins->rotation);

    /* contexto de herencia para contenido en capa "0" DENTRO del bloque
     * -- ver el comentario grande junto a apply_common_ex/DWG_INHERIT_CTX.
     * Si este INSERT mismo esta en capa "0" (raro para un mueble/simbolo
     * de nivel superior, pero posible), no hay contexto real de donde
     * heredar -- inherit=NULL deja que cada hijo caiga al override de
     * capa "0" de siempre. */
    ins_layer = resolve_layer_name(obj->tio.entity);
    if (strcmp(ins_layer, "0") == 0)
    {
        inherit = NULL;
    }
    else
    {
        inherit_storage.layer_name = ins_layer;
        inherit_storage.color = resolve_color(obj->tio.entity);
        inherit = &inherit_storage;
    }

    explode_block(hDwg, dwg, blk, &tf, 0, inherit);
}

/* DIMENSION: las lineas de cota/extension NUNCA se guardan como
 * geometria literal (ni en el bloque explotado, que solo trae las
 * flechas/tick y el texto del valor, ni como objetos DWG separados) --
 * AutoCAD las reconstruye en render a partir de estos mismos campos, asi
 * que este puente hace lo mismo. Formulas y orden de puntos verificados
 * contra decode_dimension() en dwg_r2004_entity_reader.c (confirmado
 * visualmente por Arturo en su momento) -- LINEAR/ALIGNED (extension +
 * linea de cota perpendicular por def_pt) y RADIUS/DIAMETER (linea
 * centro/arco + dogleg de leader_len). ANG2LN/ANG3PT/ORDINATE quedan
 * sin sintetizar, mismo alcance que el lector legacy.
 * El bloque anonimo de flechas + MTEXT del valor (ya con el numero
 * formateado por AutoCAD, no hace falta recalcularlo aca) se explota
 * por separado via ins_scale/ins_rotation/def_pt.
 */
static void bridge_dimension(HDWG hDwg, Dwg_Data *dwg, Dwg_Object *obj)
{
    Dwg_Object_Entity *ent = obj->tio.entity;
    Dwg_DIMENSION_common *dc = ent->tio.DIMENSION_common;
    HENTITY line_e;

    if (obj->fixedtype == DWG_TYPE_DIMENSION_LINEAR || obj->fixedtype == DWG_TYPE_DIMENSION_ALIGNED)
    {
        BITCODE_3BD xline1, xline2;
        double dim_rotation;

        if (obj->fixedtype == DWG_TYPE_DIMENSION_LINEAR)
        {
            Dwg_Entity_DIMENSION_LINEAR *d = ent->tio.DIMENSION_LINEAR;
            xline1 = d->xline1_pt;
            xline2 = d->xline2_pt;
            dim_rotation = d->dim_rotation;
        }
        else
        {
            Dwg_Entity_DIMENSION_ALIGNED *d = ent->tio.DIMENSION_ALIGNED;
            double dx, dy;
            xline1 = d->xline1_pt;
            xline2 = d->xline2_pt;
            dx = xline2.x - xline1.x;
            dy = xline2.y - xline1.y;
            dim_rotation = (dx == 0.0 && dy == 0.0) ? 0.0 : atan2(dy, dx);
        }

        {
            double dirx = cos(dim_rotation), diry = sin(dim_rotation);
            double perpx = -diry, perpy = dirx;
            double def_perp = dc->def_pt.x * perpx + dc->def_pt.y * perpy;
            double xl1_perp = xline1.x * perpx + xline1.y * perpy;
            double xl2_perp = xline2.x * perpx + xline2.y * perpy;
            double delta1 = def_perp - xl1_perp;
            double delta2 = def_perp - xl2_perp;
            double dl1x = xline1.x + delta1 * perpx, dl1y = xline1.y + delta1 * perpy;
            double dl2x = xline2.x + delta2 * perpx, dl2y = xline2.y + delta2 * perpy;

            line_e = dwg_add_line(hDwg, xline1.x, xline1.y, xline1.z, dl1x, dl1y, xline1.z);
            apply_common(line_e, ent);
            line_e = dwg_add_line(hDwg, xline2.x, xline2.y, xline2.z, dl2x, dl2y, xline2.z);
            apply_common(line_e, ent);
            line_e = dwg_add_line(hDwg, dl1x, dl1y, xline1.z, dl2x, dl2y, xline2.z);
            apply_common(line_e, ent);
        }
    }
    else if (obj->fixedtype == DWG_TYPE_DIMENSION_RADIUS || obj->fixedtype == DWG_TYPE_DIMENSION_DIAMETER)
    {
        BITCODE_3BD first_arc;
        double leader_len;

        if (obj->fixedtype == DWG_TYPE_DIMENSION_RADIUS)
        {
            Dwg_Entity_DIMENSION_RADIUS *d = ent->tio.DIMENSION_RADIUS;
            first_arc = d->first_arc_pt;
            leader_len = d->leader_len;
        }
        else
        {
            Dwg_Entity_DIMENSION_DIAMETER *d = ent->tio.DIMENSION_DIAMETER;
            first_arc = d->first_arc_pt;
            leader_len = d->leader_len;
        }

        {
            double ndx = dc->def_pt.x - first_arc.x, ndy = dc->def_pt.y - first_arc.y;
            double dist = sqrt(ndx * ndx + ndy * ndy);
            if (dist > 1.0e-9)
            {
                double ux = ndx / dist, uy = ndy / dist;
                double lead_x, lead_y;

                if (obj->fixedtype == DWG_TYPE_DIMENSION_RADIUS)
                {
                    line_e = dwg_add_line(hDwg, dc->def_pt.x, dc->def_pt.y, dc->def_pt.z,
                                          first_arc.x, first_arc.y, first_arc.z);
                    apply_common(line_e, ent);
                    if (leader_len > 1.0e-9)
                    {
                        lead_x = first_arc.x - ux * leader_len;
                        lead_y = first_arc.y - uy * leader_len;
                        line_e = dwg_add_line(hDwg, first_arc.x, first_arc.y, first_arc.z,
                                              lead_x, lead_y, first_arc.z);
                        apply_common(line_e, ent);
                    }
                }
                else
                {
                    line_e = dwg_add_line(hDwg, first_arc.x, first_arc.y, first_arc.z,
                                          dc->def_pt.x, dc->def_pt.y, dc->def_pt.z);
                    apply_common(line_e, ent);
                    if (leader_len > 1.0e-9)
                    {
                        lead_x = dc->def_pt.x + ux * leader_len;
                        lead_y = dc->def_pt.y + uy * leader_len;
                        line_e = dwg_add_line(hDwg, dc->def_pt.x, dc->def_pt.y, dc->def_pt.z,
                                              lead_x, lead_y, dc->def_pt.z);
                        apply_common(line_e, ent);
                    }
                }
            }
        }
    }
    else
    {
        return; /* ANG2LN/ANG3PT/ORDINATE -- no sintetizados, mismo alcance que el lector legacy */
    }

    /* El bloque anonimo de una DIMENSION NO es un bloque reusable comun --
     * AutoCAD lo genera de nuevo para CADA cota individual con su
     * geometria (flechas/lineas/MTEXT del valor) ya horneada en
     * coordenadas de MUNDO finales, no relativas a un origen local que
     * necesite reubicarse via def_pt/ins_rotation/ins_scale. Confirmado
     * con un diagnostico crudo: la LINE interna del bloque coincide
     * EXACTO con def_pt->first_arc_pt en coordenadas de mundo. Aplicar
     * el transform def_pt-como-insercion (como si fuera un INSERT
     * comun) duplicaba el desplazamiento -- la geometria terminaba en
     * (mundo + def_pt), la nube de "cotas desplazadas" que reporto
     * Arturo. Por eso aca se copia el contenido del bloque TAL CUAL
     * (transform identidad), a diferencia de bridge_insert que si
     * necesita el transform real para bloques nombrados/reusables. */
    if (dc->block != NULL && dc->block->obj != NULL)
    {
        BlockXform tf;
        DWG_INHERIT_CTX inherit_storage;
        const DWG_INHERIT_CTX *inherit;
        const char *dim_layer;

        xform_make(&tf, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);

        /* mismo contexto de herencia de capa "0" que bridge_insert -- el
         * bloque de flechas/texto de la cota tambien puede traer su
         * geometria en capa "0" para heredar la capa de la propia
         * DIMENSION. */
        dim_layer = resolve_layer_name(ent);
        if (strcmp(dim_layer, "0") == 0)
        {
            inherit = NULL;
        }
        else
        {
            inherit_storage.layer_name = dim_layer;
            inherit_storage.color = resolve_color(ent);
            inherit = &inherit_storage;
        }

        explode_block(hDwg, dwg, dc->block->obj, &tf, 0, inherit);
    }
}

static int entity_in_model_or_paper_space(Dwg_Data *dwg, Dwg_Object_Entity *ent)
{
    Dwg_Object *owner_obj = (ent->ownerhandle != NULL) ? ent->ownerhandle->obj : NULL;

    if (owner_obj == NULL)
        return 1; /* sin owner resuelto -- mejor mostrarlo que perderlo silenciosamente */
    return owner_obj == dwg_model_space_object(dwg) || owner_obj == dwg_paper_space_object(dwg);
}

HDWG dwg_read_dwg_libredwg(const char *path, DWG_IO_RESULT *result)
{
    Dwg_Data dwg;
    int err;
    HDWG hDoc;
    BITCODE_BL i;

    if (path == NULL)
    {
        if (result != NULL) *result = DWG_IO_ERROR_OPEN;
        return NULL;
    }

    memset(&dwg, 0, sizeof(Dwg_Data));
    err = dwg_read_file(path, &dwg);
    if (err >= DWG_ERR_CRITICAL)
    {
        dwg_free(&dwg);
        if (result != NULL) *result = DWG_IO_ERROR_FORMAT;
        return NULL;
    }

    hDoc = dwg_document_create();
    if (hDoc == NULL)
    {
        dwg_free(&dwg);
        if (result != NULL) *result = DWG_IO_ERROR_MEMORY;
        return NULL;
    }

    populate_styles(hDoc, &dwg);
    populate_layers(hDoc, &dwg);

    for (i = 0UL; i < dwg.num_objects; i++)
    {
        Dwg_Object *obj = &dwg.object[i];

        if (obj->supertype != DWG_SUPERTYPE_ENTITY || obj->tio.entity == NULL)
            continue;

        /* Entidades que viven dentro de una definicion de bloque (nombrado
           o anonimo, p.ej. el bloque de flechas de una DIMENSION) NO se
           agregan aca directo -- aparecerian en su posicion LOCAL AL
           BLOQUE, no en su posicion real en el dibujo (el bug que
           reporto Arturo como "cotas desplazadas"). Se agregan, ya
           transformadas, cuando se procesa el INSERT/DIMENSION que las
           referencia (bridge_insert/bridge_dimension mas abajo). */
        if (!entity_in_model_or_paper_space(&dwg, obj->tio.entity))
            continue;

        /* capa apagada/congelada -- el archivo la oculta a proposito,
           ver entity_layer_hidden(). */
        if (entity_layer_hidden(obj->tio.entity))
            continue;

        switch (obj->fixedtype)
        {
        case DWG_TYPE_LINE:         bridge_line(hDoc, obj); break;
        case DWG_TYPE_CIRCLE:       bridge_circle(hDoc, obj); break;
        case DWG_TYPE_ARC:          bridge_arc(hDoc, obj); break;
        case DWG_TYPE_POINT:        bridge_point(hDoc, obj); break;
        case DWG_TYPE_SOLID:        bridge_solid(hDoc, obj); break;
        case DWG_TYPE__3DFACE:      bridge_face(hDoc, obj); break;
        case DWG_TYPE_TEXT:         bridge_text(hDoc, obj); break;
        case DWG_TYPE_MTEXT:        bridge_mtext(hDoc, obj); break;
        case DWG_TYPE_LWPOLYLINE:   bridge_lwpolyline(hDoc, obj); break;
        case DWG_TYPE_HATCH:        bridge_hatch(hDoc, obj); break;
        case DWG_TYPE_POLYLINE_2D:  bridge_polyline_2d(hDoc, obj); break;
        case DWG_TYPE_POLYLINE_3D:  bridge_polyline_3d(hDoc, obj); break;
        case DWG_TYPE_POLYLINE_MESH: bridge_polyline_mesh(hDoc, obj); break;
        case DWG_TYPE_INSERT:       bridge_insert(hDoc, &dwg, obj); break;
        case DWG_TYPE_DIMENSION_LINEAR:
        case DWG_TYPE_DIMENSION_ALIGNED:
        case DWG_TYPE_DIMENSION_RADIUS:
        case DWG_TYPE_DIMENSION_DIAMETER:
            bridge_dimension(hDoc, &dwg, obj);
            break;
        default:
            break;
        }
    }

    dwg_free(&dwg);

    if (result != NULL) *result = DWG_IO_OK;
    return hDoc;
}
