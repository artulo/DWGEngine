#ifndef DWG_FILE_IO_H
#define DWG_FILE_IO_H

#include "dwg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Angulos de arco (start_angle/end_angle) se manejan en GRADOS,
 * igual que el grupo 50/51 del formato DXF, para evitar conversiones
 * innecesarias al leer/escribir.
 */

typedef enum
{
    DWG_IO_OK = 0,
    DWG_IO_ERROR_OPEN,
    DWG_IO_ERROR_FORMAT,
    DWG_IO_ERROR_MEMORY
} DWG_IO_RESULT;

HDWG dwg_read_dxf(const char *path, DWG_IO_RESULT *result);
DWG_IO_RESULT dwg_write_dxf(HDWG hDwg, const char *path);

/*
 * AutoCAD R12 (AC1009) binary format. Writer only for now -- see
 * reverse\DWG_R12_format_reference.md for the validation plan (vecad.dll's
 * CadFileOpen is used as the oracle to confirm this writer is correct
 * before a reader is attempted).
 */
DWG_IO_RESULT dwg_write_dwg_r12(HDWG hDwg, const char *path);
HDWG dwg_read_dwg_r12(const char *path, DWG_IO_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif
