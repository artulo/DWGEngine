@ECHO OFF
REM ============================================================================
REM BuildMSVC.bat - DWGEngine + Harbour/FiveWin, compilado con MSVC en vez de
REM bcc32 -- build PARALELO al Build.bat (bcc32) existente, que sigue siendo
REM el build "conocido bueno". Este script no toca ni un archivo del build
REM bcc32 (usa win32\msvc\ como directorio de trabajo propio, separado de
REM win32\ donde caen los .obj/.exe de Build.bat).
REM
REM Receta de linkeo confirmada empiricamente con un piloto minimo (ver
REM memoria project_dwgengine_msvc_pilot_confirmed) antes de escribir este
REM script -- en particular las libs de compatibilidad de runtime C
REM (legacy_stdio_definitions+oldnames+msvcrt) y la lista de .hcb de Harbour
REM que FiveH32.lib necesita.
REM ============================================================================

cd /d %~dp0\msvc

if "%FWDIR%" == "" set FWDIR=d:\prgsmio\FWH2603
if "%HBDIR%" == "" set HBDIR=d:\prgsmio\Harbour_32_VSC2022
if "%LIBREDWG%" == "" set LIBREDWG=D:\estudio\libredwg-master
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

set hdir=%HBDIR%
set hdirl=%hdir%\lib\win\msvc
set hbin=%hdir%\bin\win\msvc
set fwh=%FWDIR%

set PRG=dwg_demo
set ENGABS=D:\estudio\DWGEngine

call %VCVARS% x86 >nul
if errorlevel 1 (
    ECHO *** No se pudo inicializar el entorno de MSVC ^(vcvarsall.bat^) ***
    GOTO EXIT
)

REM Borrar .obj/.c/.exe viejos -- mismo motivo que Build.bat: mejor un "no
REM existe" clarito que linkear con algo de una vuelta anterior.
del /q *.obj *.c *.ppo *.exe *.log *.res 2>nul

ECHO %FWDIR%
ECHO %hdir%
ECHO %hdirl%

REM --- 1) Motor DWGEngine (todos los .c de src\) ---
ECHO === Compilando motor DWGEngine (MSVC) ===

set CFLAGS_ENG=/c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%ENGABS%\include /I%LIBREDWG%\include /Fo.\

set FAILED=0
for %%f in (%ENGABS%\src\*.c) do (
    cl.exe %CFLAGS_ENG% "%%f" >nul
    if errorlevel 1 (
        ECHO *** ERROR compilando %%f ***
        set FAILED=1
    )
)
if %FAILED%==1 GOTO ENGINEERROR

if not exist dwg_render.obj GOTO ENGINEERROR
if not exist dwg_r2000_reader.obj GOTO ENGINEERROR
if not exist dwg_r2000_writer.obj GOTO ENGINEERROR
if not exist dwg_dwg_reader.obj GOTO ENGINEERROR
if not exist dwg_document.obj GOTO ENGINEERROR
if not exist dwg_libredwg_bridge.obj GOTO ENGINEERROR

REM --- 1b) LibreDWG in_dxf.c, recompilado con dwg_read_dxf renombrado -----
REM
REM LibreDWG's propio in_dxf.c define dwg_read_dxf(Bit_Chain*,Dwg_Data*)
REM (su importador DXF interno) -- choca en el link con la funcion YA
REM EXISTENTE de DWGEngine del mismo nombre en dwg_dxf_reader.c (firma
REM distinta, extern "C" ambas). Se recompila aca ese UN archivo con
REM /Ddwg_read_dxf=libredwg_dwg_read_dxf (renombre a nivel preprocesador,
REM antes de que dwg_api.h declare el simbolo) para que ambos nombres
REM convivan en el mismo .exe -- el resto de LibreDWG (dwg.obj/dwg_api.obj/
REM etc) sigue viendo el nombre real dwg_read_dxf via el .h, pero como
REM nada de lo que se linkea aca LO LLAMA (dxf_read_file referencia
REM dwg_read_dxfb, no dwg_read_dxf), no hace falta renombrar nada mas.
ECHO === Compilando LibreDWG in_dxf.c (renombrado) ===
cl.exe /c /nologo /O2 /D_CRT_SECURE_NO_WARNINGS /DDISABLE_WERROR /Ddwg_read_dxf=libredwg_dwg_read_dxf ^
    /I%LIBREDWG%\include /I%LIBREDWG%\src /I%LIBREDWG%\build_msvc\src ^
    /Fo.\in_dxf_renamed.obj %LIBREDWG%\src\in_dxf.c
if not exist in_dxf_renamed.obj GOTO ENGINEERROR

REM --- 2) Glue Harbour (dwg_hbfunc.c) -------------------------------------
ECHO === Compilando glue Harbour (dwg_hbfunc.c) ===

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /DDWGENGINE_HAVE_LIBREDWG /I%ENGABS%\include /I%hdir%\include /Fo.\ %ENGABS%\harbour\dwg_hbfunc.c
if not exist dwg_hbfunc.obj GOTO ENGINEERROR

REM --- 2b) Recursos Windows (dwg_demo.rc) ---------------------------------
REM Los 4 dialogos (Capas/Propiedades/Texto/Parametros) como DIALOGEX reales
REM -- pedido de Arturo 2026-08-27, ver dwg_demo.rc. dwg_demo_ids.ch se copia
REM aca (antes de cualquier .prg) porque TODOS los .prg de dialogo y el .rc
REM comparten el mismo #include "dwg_demo_ids.ch" -- una sola fuente de
REM verdad para los IDs de control. rc.exe ya esta en el PATH despues de
REM vcvarsall.bat de arriba (mismo -d__FLAT__ que buildh32.bat de FWH2603
REM usa para sus propios .rc).
ECHO === Compilando recursos (dwg_demo.rc) ===
copy %ENGABS%\harbour\dwg_demo_ids.ch . > nul
copy %ENGABS%\harbour\dwg_demo.rc . > nul

rc.exe -r -d__FLAT__ dwg_demo.rc
if not exist dwg_demo.res GOTO ENGINEERROR

REM --- 3) dwg_viewer.prg ---------------------------------------------------
ECHO === Compilando dwg_viewer.prg ===
copy %ENGABS%\harbour\dwg_viewer.prg . > nul

%hbin%\harbour.exe dwg_viewer /n /i%fwh%\include;%hdir%\include /w /p > comp_viewer.log 2> warnings_viewer.log
if errorlevel 1 GOTO COMPILEERRORS_VIEWER
@type comp_viewer.log
@type warnings_viewer.log

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%hdir%\include /I%fwh%\include /Fo.\ dwg_viewer.c
if not exist dwg_viewer.obj GOTO ENGINEERROR

REM --- 3b/3c) dwg_layers_dlg.prg / dwg_props_dlg.prg (dialogos de Capas/
REM Propiedades, ver dwg_hbfunc.c HB_FUNC nuevos + TDwgViewer:SelectPoint/
REM SelectWindow/etc en dwg_viewer.prg) -- mismo patron que dwg_viewer.prg.
ECHO === Compilando dwg_layers_dlg.prg ===
copy %ENGABS%\harbour\dwg_layers_dlg.prg . > nul

%hbin%\harbour.exe dwg_layers_dlg /n /i%fwh%\include;%hdir%\include /w /p > comp_layers_dlg.log 2> warnings_layers_dlg.log
if errorlevel 1 GOTO COMPILEERRORS_LAYERS_DLG
@type comp_layers_dlg.log
@type warnings_layers_dlg.log

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%hdir%\include /I%fwh%\include /Fo.\ dwg_layers_dlg.c
if not exist dwg_layers_dlg.obj GOTO ENGINEERROR

ECHO === Compilando dwg_props_dlg.prg ===
copy %ENGABS%\harbour\dwg_props_dlg.prg . > nul

%hbin%\harbour.exe dwg_props_dlg /n /i%fwh%\include;%hdir%\include /w /p > comp_props_dlg.log 2> warnings_props_dlg.log
if errorlevel 1 GOTO COMPILEERRORS_PROPS_DLG
@type comp_props_dlg.log
@type warnings_props_dlg.log

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%hdir%\include /I%fwh%\include /Fo.\ dwg_props_dlg.c
if not exist dwg_props_dlg.obj GOTO ENGINEERROR

REM --- 3d) dwg_text_dlg.prg (dialogo de Texto, ver DWG_ADDTEXT en
REM dwg_hbfunc.c + TDwgViewer:DrawClick "TEXT" en dwg_viewer.prg) -- mismo
REM patron que dwg_layers_dlg.prg/dwg_props_dlg.prg arriba.
ECHO === Compilando dwg_text_dlg.prg ===
copy %ENGABS%\harbour\dwg_text_dlg.prg . > nul

%hbin%\harbour.exe dwg_text_dlg /n /i%fwh%\include;%hdir%\include /w /p > comp_text_dlg.log 2> warnings_text_dlg.log
if errorlevel 1 GOTO COMPILEERRORS_TEXT_DLG
@type comp_text_dlg.log
@type warnings_text_dlg.log

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%hdir%\include /I%fwh%\include /Fo.\ dwg_text_dlg.c
if not exist dwg_text_dlg.obj GOTO ENGINEERROR

REM --- 4) dwg_demo.prg -----------------------------------------------------
copy %ENGABS%\harbour\%PRG%.prg . > nul

%hbin%\harbour.exe %PRG% /n /i%fwh%\include;%hdir%\include /w /p > comp.log 2> warnings.log
if errorlevel 1 GOTO COMPILEERRORS
@type comp.log
@type warnings.log

cl.exe /c /nologo /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I%hdir%\include /I%fwh%\include /Fo.\ %PRG%.c
if not exist %PRG%.obj GOTO ENGINEERROR

REM --- 5) Link: motor + glue + dwg_viewer.obj + %PRG%.obj + FiveWin + Harbour
REM
REM NOTA LibreDWG: se linkean los .obj INDIVIDUALES de libredwg.dir\Release
REM (generados por el build CMake de LibreDWG, ver comentario arriba de
REM DwgOpen en harbour\dwg_hbfunc.c) en vez de libredwg.lib completo.
REM Motivo: libredwg.lib trae in_dxf.obj/out_dxf.obj (LibreDWG's propio
REM importador/exportador DXF), que exportan un simbolo TAMBIEN llamado
REM dwg_read_dxf/dwg_write_dxf -- choca (LNK2005, "ya se definio") con
REM las funciones YA EXISTENTES de DWGEngine del mismo nombre en
REM dwg_dxf_reader.c/dwg_dxf_writer.c (funciones distintas, mismo nombre,
REM ambas extern "C"). Como el puente (dwg_libredwg_bridge.c) solo llama
REM dwg_read_file (el lector binario DWG real), no hace falta ninguno de
REM los conversores DXF/JSON de LibreDWG -- se listan solo los .obj del
REM nucleo de lectura (bits/decode/dwg/dwg_api/etc), sin encode.obj/
REM encode2.obj (path de escritura, tampoco usado) ni in_dxf/out_dxf/
REM out_dxfb/out_json/out_geojson.
ECHO === Linkeando %PRG%.exe (MSVC) ===

link.exe /nologo /subsystem:windows /out:%PRG%.exe ^
    dwg_bitstream.obj dwg_block.obj dwg_dimension.obj dwg_dimstyle.obj dwg_document.obj ^
    dwg_dwg_reader.obj dwg_dwg_writer.obj dwg_dxf_reader.obj dwg_dxf_writer.obj ^
    dwg_entity.obj dwg_geometry.obj dwg_hatch.obj dwg_insert.obj dwg_layer.obj ^
    dwg_leader.obj dwg_linetype.obj dwg_mlinestyle.obj dwg_mtext.obj dwg_page.obj ^
    dwg_pointstyle.obj dwg_polyline.obj dwg_r2000_entity_reader.obj ^
    dwg_r2000_reader.obj dwg_r2000_writer.obj dwg_r1314_entity_reader.obj ^
    dwg_r2004_decompress.obj dwg_r2004_entity_reader.obj dwg_render.obj ^
    dwg_selection.obj dwg_solid.obj dwg_style.obj dwg_text.obj dwg_transform.obj ^
    dwg_vertex.obj dwg_libredwg_bridge.obj dwg_hbfunc.obj dwg_viewer.obj ^
    dwg_layers_dlg.obj dwg_props_dlg.obj dwg_text_dlg.obj %PRG%.obj ^
    dwg_demo.res ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\bits.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\classes.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\codepages.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\common.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\decode.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\decode2.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\decode_r11.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\decode_r2007.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\dwg.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\dwg_api.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\dxfclasses.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\dynapi.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\free.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\geom.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\hash.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\logging.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\objects.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\print.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\reedsolomon.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\encode.obj" ^
    "%LIBREDWG%\build_msvc\libredwg.dir\Release\encode2.obj" ^
    in_dxf_renamed.obj ^
    /LIBPATH:%fwh%\lib FiveH32.lib FiveHC32.lib ^
    /LIBPATH:%hdirl% hbwin.lib gtgui.lib hbrtl.lib hbvm.lib hblang.lib ^
    hbmacro.lib hbrdd.lib rddntx.lib rddcdx.lib rddfpt.lib hbsix.lib ^
    hbdebug.lib hbcommon.lib hbpp.lib hbcpage.lib hbcplr.lib hbct.lib ^
    hbpcre.lib xhb.lib hbziparc.lib hbmzip.lib hbzlib.lib minizip.lib ^
    hbusrrdd.lib hbtip.lib hbfoxpro.lib ^
    gdiplus.lib ole32.lib OleDlg.lib version.lib comctl32.lib comdlg32.lib ^
    winspool.lib shell32.lib uxtheme.lib msimg32.lib ^
    kernel32.lib user32.lib gdi32.lib advapi32.lib oleaut32.lib uuid.lib ^
    ws2_32.lib winmm.lib mpr.lib iphlpapi.lib odbc32.lib odbccp32.lib ^
    legacy_stdio_definitions.lib oldnames.lib msvcrt.lib
IF ERRORLEVEL 1 GOTO LINKERROR

ECHO * Listo: %PRG%.exe *
GOTO EXIT

:ENGINEERROR
ECHO.
ECHO *** Fallo la compilacion del motor o del glue -- revisar arriba ***
GOTO EXIT

:COMPILEERRORS_VIEWER
@type comp_viewer.log
@type warnings_viewer.log
ECHO * Errores de compilacion Harbour (dwg_viewer.prg) *
GOTO EXIT

:COMPILEERRORS_LAYERS_DLG
@type comp_layers_dlg.log
@type warnings_layers_dlg.log
ECHO * Errores de compilacion Harbour (dwg_layers_dlg.prg) *
GOTO EXIT

:COMPILEERRORS_PROPS_DLG
@type comp_props_dlg.log
@type warnings_props_dlg.log
ECHO * Errores de compilacion Harbour (dwg_props_dlg.prg) *
GOTO EXIT

:COMPILEERRORS_TEXT_DLG
@type comp_text_dlg.log
@type warnings_text_dlg.log
ECHO * Errores de compilacion Harbour (dwg_text_dlg.prg) *
GOTO EXIT

:COMPILEERRORS
@type comp.log
@type warnings.log
ECHO * Errores de compilacion Harbour *
GOTO EXIT

:LINKERROR
ECHO * Errores de link *
GOTO EXIT

:EXIT
