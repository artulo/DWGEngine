// dwg_demo_ids.ch
//
// ============================================================================
// IDs de control compartidos entre dwg_demo.rc (compilado con rc.exe,
// preprocesador compatible C) y cada .prg de dialogo (preprocesador de
// Harbour, tambien entiende #define/#include estilo C) -- pedido de
// Arturo 2026-08-27 ("generar un dwg_demo.rc donde se encuentren todos
// los dialogos y reemplazarlos"). UNA sola fuente de verdad para los
// numeros: nunca hay que sincronizar dos listas a mano entre el .rc y
// el .prg que lo REDEFINE.
//
// Rangos separados por dialogo (100s Capas, 200s Propiedades, 300s
// Texto, 400s Parametros) solo por claridad -- un mismo numero podria
// reusarse entre dialogos DISTINTOS sin problema real (cada DIALOGEX es
// su propio espacio de IDs), pero mantenerlos separados evita cualquier
// confusion al leer el .rc o el .prg.
// ============================================================================

#ifndef DWG_DEMO_IDS_CH
#define DWG_DEMO_IDS_CH

// IDOK/IDCANCEL (1/2, el estandar de Windows) YA vienen definidos
// transitivamente (confirmado: un #define propio aca disparaba
// "redefinition" al compilar) -- NO redefinir, mismo motivo/criterio ya
// documentado para VK_ESCAPE en dwg_viewer.prg.

// ---- Capas (dwg_layers_dlg.prg) -------------------------------------
#define ID_CAPAS_LIST     101
#define ID_CAPAS_ISOLATE  102
#define ID_CAPAS_SHOWALL  103

// ---- Propiedades (dwg_props_dlg.prg) --------------------------------
#define ID_PROPS_CAPA      201
#define ID_PROPS_COLOR     202
#define ID_PROPS_LINETYPE  203
#define ID_PROPS_GEOMLABEL 204   // "Geometria:" / "(sin geometria editable)"
#define ID_PROPS_LBL1      205
#define ID_PROPS_LBL2      206
#define ID_PROPS_LBL3      207
#define ID_PROPS_LBL4      208
#define ID_PROPS_LBL5      209
#define ID_PROPS_LBL6      210
#define ID_PROPS_G1        211
#define ID_PROPS_G2        212
#define ID_PROPS_G3        213
#define ID_PROPS_G4        214
#define ID_PROPS_G5        215
#define ID_PROPS_G6        216
#define ID_PROPS_LBLTXT    217
#define ID_PROPS_TEXTO     218

// ---- Texto (dwg_text_dlg.prg) ---------------------------------------
#define ID_TEXT_TEXTO       301
#define ID_TEXT_FONTSLABEL  302
#define ID_TEXT_FONTS       303
#define ID_TEXT_ALTURA      304

// ---- Parametros genericos (dwg_demo.prg, DwgAskParams) --------------
#define ID_PARAMS_LBL1  401
#define ID_PARAMS_LBL2  402
#define ID_PARAMS_LBL3  403
#define ID_PARAMS_LBL4  404
#define ID_PARAMS_F1    405
#define ID_PARAMS_F2    406
#define ID_PARAMS_F3    407
#define ID_PARAMS_F4    408

#endif
