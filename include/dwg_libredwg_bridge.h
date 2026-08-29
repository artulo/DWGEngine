#ifndef DWG_LIBREDWG_BRIDGE_H
#define DWG_LIBREDWG_BRIDGE_H

#include "dwg_types.h"
#include "dwg_file_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lee un DWG usando LibreDWG (GPLv3, D:\estudio\libredwg-master) como
 * motor de parseo real, en vez de los lectores propios de DWGEngine
 * (dwg_r2000_reader.c / dwg_r1314_entity_reader.c / dwg_r2004_entity_reader.c).
 * LibreDWG hace la lectura/decodificacion completa del archivo; este
 * puente solo camina su modelo de objetos (Dwg_Data/Dwg_Object) y llena
 * el modelo HDWG/HENTITY de DWGEngine via las mismas dwg_add_line/
 * dwg_add_circle/dwg_entity_put_layer/etc que ya usa el resto del motor
 * -- todo el codigo aguas abajo (render, documento, GUI Harbour/FiveWin)
 * queda sin cambios.
 *
 * Confirmado con Arturo (2026-08-25): integrar el codigo real de
 * LibreDWG implica que el binario combinado de DWGEngine pasa a estar
 * bajo GPLv3 (codigo fuente debe estar disponible, ya no se puede
 * vender/licenciar como propietario/cerrado) -- decision de negocio
 * aceptada explicitamente, revirtiendo dos rechazos previos.
 */
HDWG dwg_read_dwg_libredwg(const char *path, DWG_IO_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif
