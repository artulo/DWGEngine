#ifndef DWG_BLOCK_H
#define DWG_BLOCK_H

#include "dwg_types.h"
#include "dwg_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DWG_BLOCK_NAME_MAX 64
#define DWG_BLOCK_PATH_MAX 260

typedef struct _DWG_BLOCK DWG_BLOCK;
typedef DWG_BLOCK * HBLOCK;

/*
 * Fields match real vecad.dll's CBlock, recovered in
 * D:\estudio\DWGEngine\reverse\CBlock_notes.md: a base insertion point
 * (3 doubles, confirmed against CadBlockGetBase) and its own embedded
 * entity list (confirmed against CadBlockAddEntity/GetFirstPtr).
 */
struct _DWG_BLOCK
{
    char name[DWG_BLOCK_NAME_MAX];

    DWG_POINT3D base;

    DWG_ENTITY *entity_first;
    DWG_ENTITY *entity_last;
    unsigned long entity_count;
    DWG_ID next_entity_id;

    DWG_BOOL is_xref;
    char xref_path[DWG_BLOCK_PATH_MAX];

    HBLOCK next;
    HBLOCK prev;
};

HBLOCK dwg_block_create(const char *name);
void dwg_block_destroy(HBLOCK block);

const char *dwg_block_get_name(HBLOCK block);
long dwg_block_set_name(HBLOCK block, const char *name);

void dwg_block_get_base(HBLOCK block, double *x, double *y, double *z);
void dwg_block_set_base(HBLOCK block, double x, double y, double z);

HENTITY dwg_block_add_entity(HBLOCK block, DWG_ENTITY_TYPE type);
long dwg_block_remove_entity(HBLOCK block, HENTITY entity);

/*
 * Appends an already-created, currently-unattached entity (e.g. one
 * just detached from a document via dwg_document_detach_entity) to this
 * block's own entity list. Does not allocate or assign a new id.
 */
long dwg_block_attach_entity(HBLOCK block, HENTITY entity);

HENTITY dwg_block_first_entity(HBLOCK block);
HENTITY dwg_block_next_entity(HENTITY entity);

unsigned long dwg_block_entity_count(HBLOCK block);

DWG_BOOL dwg_block_is_xref(HBLOCK block);

const char *dwg_block_get_xref_path(HBLOCK block);
long dwg_block_set_xref_path(HBLOCK block, const char *path);

#ifdef __cplusplus
}
#endif

#endif
