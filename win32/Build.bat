@ECHO OFF
REM ============================================================================
REM Build.bat - DWGEngine + Harbour/FiveWin
REM
REM Clonado de D:\estudio\PDFEngine32\win32\Build.bat (mismo par de
REM toolchains, rutas confirmadas por Arturo: FWH2603 d:\prgsmio\FWH2603,
REM harbour d:\prgsmio\FWH2603\harbour, bcc d:\prgsmio\bcc770). Mismo GT=
REM gtgui, misma estructura de pasos (motor -> glue harbour -> .prg's ->
REM link), solo cambian los nombres de archivo (dwg_* en vez de pdf_*) y
REM la lista de objetos/.c del motor.
REM
REM Los DOS bugs de esta maquina documentados en el Build.bat de
REM PDFEngine32 (rutas relativas/-I resueltas distinto cuando bcc32 se
REM invoca DESDE este .bat, y compilacion stale silenciosa de archivos
REM puntuales pese a imprimir "Listo") son de la MAQUINA, no del
REM proyecto -- aplican aca tambien. Por eso, igual que alla: TODOS los
REM .c del motor usan ruta ABSOLUTA (%ENGABS%) y el -I tambien absoluto,
REM sin mezclar estilos. Si algun .c de DWGEngine muestra el mismo sintoma
REM (cambio en el .c que no se refleja en el .exe final pese a "Listo"),
REM el fix es el mismo: recompilar ese .c a mano con bcc32 directo
REM (mismas flags que CFLAGS_ENG mas abajo) y copiar el .obj resultante
REM encima antes de linkear.
REM ============================================================================

REM BUG REAL ENCONTRADO Y ARREGLADO: invocar este .bat con "cmd /c
REM rutacompleta\Build.bat" (en vez de cd-ear ahi primero y despues
REM llamarlo por nombre suelto) NO cambia el cwd del proceso a la carpeta
REM del .bat -- cmd.exe deja el cwd heredado del llamador. Sin este cd,
REM "-n." (paso 1 de abajo) y los "if not exist X.obj" que vienen
REM despues terminan operando sobre el cwd EQUIVOCADO -- silencioso, sin
REM ningun mensaje de error claro (los .obj se crean bien, solo que en
REM otro lado). %~dp0 es la carpeta de ESTE .bat, siempre correcta sin
REM importar como se lo invoco.
cd /d %~dp0

if "%FWDIR%" == "" set FWDIR=d:\prgsmio\FWH2603
if "%HBDIR%" == "" set HBDIR=%FWDIR%\harbour
set GT=gtgui

set hdir=%HBDIR%
set hdirl=%hdir%\lib\win\bcc
set fwh=%FWDIR%
set bcdir=d:\prgsmio\bcc770

set PRG=dwg_demo
set ENG=..
set ENGABS=D:\estudio\DWGEngine

REM Borrar .obj viejos -- mismo motivo que en PDFEngine32\win32\Build.bat:
REM mejor un "no existe" clarito que linkear con un .obj de una vuelta
REM anterior si algo mas abajo falla a mitad de camino.
del /q *.obj 2>nul
del /q %PRG%.c %PRG%.ppo %PRG%.exe 2>nul
del /q dwg_viewer.c dwg_viewer.ppo 2>nul

ECHO %FWDIR%
ECHO %hdir%
ECHO %hdirl%

REM --- 1) Motor DWGEngine (todos los .c de src\, ya compilan limpio con bcc32) ---
ECHO === Compilando motor DWGEngine ===

REM -n. fuerza el .obj al directorio ACTUAL (win32\) -- sin esto, bcc32
REM pone el .obj al lado del .c fuente (confirmado empiricamente: con
REM fuente absoluta %ENGABS%\src\*.c y sin -n, los .obj aparecieron en
REM src\, no en win32\, y los checks "if not exist X.obj" de abajo
REM fallaban todos aunque la compilacion en si no tuviera errores reales).
REM
REM BUG REAL ENCONTRADO Y ARREGLADO: bcc32.cfg de esta instalacion de
REM bcc770 (d:\prgsmio\bcc770\bin\bcc32.cfg) trae "-w!" (TODO warning
REM cuenta como error fatal, config de toda la maquina, no de este
REM proyecto). 4 de los 29 .c del motor emiten algun warning benigno
REM (comparacion signed/unsigned, variable asignada y no usada) y con
REM -w! eso basta para que bcc32 aborte SIN imprimir ninguna linea de
REM "Error" (solo el warning en si + "*** 1 errors in Compile ***"),
REM asi que el .obj no se genera y no hay pista clara de por que en la
REM salida. -w- (en la linea de comando, DESPUES del cfg) neutraliza el
REM -w! del cfg -- confirmado en runs de prueba que con -w- estos mismos
REM 4 archivos compilan limpio (mismos warnings se siguen imprimiendo,
REM pero ya no son fatales).
set CFLAGS_ENG=-c -O2 -w- -n. -I%ENGABS%\include -I%bcdir%\include

%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_bitstream.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_block.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_dimstyle.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_document.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_dwg_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_dwg_writer.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_dxf_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_dxf_writer.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_entity.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_geometry.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_hatch.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_insert.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_layer.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_leader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_linetype.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_mlinestyle.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_mtext.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_page.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_pointstyle.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_polyline.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r2000_entity_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r2000_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r2000_writer.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r1314_entity_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r2004_decompress.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_r2004_entity_reader.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_render.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_selection.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_solid.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_style.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_text.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_transform.c
%bcdir%\bin\bcc32 %CFLAGS_ENG% %ENGABS%\src\dwg_vertex.c

if not exist dwg_render.obj goto ENGINEERROR
if not exist dwg_r2000_reader.obj goto ENGINEERROR
if not exist dwg_r2000_writer.obj goto ENGINEERROR
if not exist dwg_dwg_reader.obj goto ENGINEERROR
if not exist dwg_document.obj goto ENGINEERROR

REM --- 2) Glue Harbour (usa hbapi.h de Harbour + windows.h de BCC) -------------
ECHO === Compilando glue Harbour (dwg_hbfunc.c) ===

%bcdir%\bin\bcc32 -c -O2 -w- -n. -I%ENGABS%\include -I%hdir%\include -I%bcdir%\include %ENGABS%\harbour\dwg_hbfunc.c

if not exist dwg_hbfunc.obj goto ENGINEERROR

REM --- 3) dwg_viewer.prg (clases TDwgViewer/TDwgBitmap -- ver
REM     harbour\dwg_viewer.prg) -- se compila COMO MODULO APARTE, mismo
REM     patron que pdf_viewer.prg en PDFEngine32. --------------------------

ECHO === Compilando dwg_viewer.prg ===
copy %ENGABS%\harbour\dwg_viewer.prg . > nul

%hdir%\bin\harbour dwg_viewer /n /i%fwh%\include;%hdir%\include /w /p > comp_viewer.log 2> warnings_viewer.log
if errorlevel 1 goto COMPILEERRORS_VIEWER
@type comp_viewer.log
@type warnings_viewer.log

echo -O2 -w- -edwg_viewer.exe -I%hdir%\include -I%bcdir%\include -I%fwh%\include dwg_viewer.c > b32v.bc
%bcdir%\bin\bcc32 -M -c @b32v.bc

if not exist dwg_viewer.obj goto ENGINEERROR

REM --- 4) dwg_demo.prg -----------------------------------------------------
copy %ENGABS%\harbour\%PRG%.prg . > nul

ECHO Compiling...

%hdir%\bin\harbour %PRG% /n /i%fwh%\include;%hdir%\include /w /p > comp.log 2> warnings.log
if errorlevel 1 goto COMPILEERRORS
@type comp.log
@type warnings.log

echo -O2 -w- -e%PRG%.exe -I%hdir%\include -I%bcdir%\include -I%fwh%\include %PRG%.c > b32.bc
%bcdir%\bin\bcc32 -M -c @b32.bc

REM --- 5) Link: objetos del motor + glue + dwg_viewer.obj + %PRG%.obj +
REM     TODAS las libs del build.bat real (sin recortar -- ver la nota en
REM     PDFEngine32\win32\Build.bat sobre por que la version recortada
REM     daba unresolved externals).

echo -L%bcdir%\lib;%bcdir%\lib\psdk + > b32.bc
echo %bcdir%\lib\c0w32.obj + >> b32.bc
echo dwg_bitstream.obj + >> b32.bc
echo dwg_block.obj + >> b32.bc
echo dwg_dimstyle.obj + >> b32.bc
echo dwg_document.obj + >> b32.bc
echo dwg_dwg_reader.obj + >> b32.bc
echo dwg_dwg_writer.obj + >> b32.bc
echo dwg_dxf_reader.obj + >> b32.bc
echo dwg_dxf_writer.obj + >> b32.bc
echo dwg_entity.obj + >> b32.bc
echo dwg_geometry.obj + >> b32.bc
echo dwg_hatch.obj + >> b32.bc
echo dwg_insert.obj + >> b32.bc
echo dwg_layer.obj + >> b32.bc
echo dwg_leader.obj + >> b32.bc
echo dwg_linetype.obj + >> b32.bc
echo dwg_mlinestyle.obj + >> b32.bc
echo dwg_mtext.obj + >> b32.bc
echo dwg_page.obj + >> b32.bc
echo dwg_pointstyle.obj + >> b32.bc
echo dwg_polyline.obj + >> b32.bc
echo dwg_r2000_entity_reader.obj + >> b32.bc
echo dwg_r2000_reader.obj + >> b32.bc
echo dwg_r2000_writer.obj + >> b32.bc
echo dwg_r1314_entity_reader.obj + >> b32.bc
echo dwg_r2004_decompress.obj + >> b32.bc
echo dwg_r2004_entity_reader.obj + >> b32.bc
echo dwg_render.obj + >> b32.bc
echo dwg_selection.obj + >> b32.bc
echo dwg_solid.obj + >> b32.bc
echo dwg_style.obj + >> b32.bc
echo dwg_text.obj + >> b32.bc
echo dwg_transform.obj + >> b32.bc
echo dwg_vertex.obj + >> b32.bc
echo dwg_hbfunc.obj + >> b32.bc
echo dwg_viewer.obj + >> b32.bc
echo %PRG%.obj, + >> b32.bc
echo %PRG%.exe, + >> b32.bc
echo %PRG%.map, + >> b32.bc
echo %fwh%\lib\FiveH.lib %fwh%\lib\FiveHC.lib %fwh%\lib\libmysql.lib + >> b32.bc
echo %fwh%\lib\xlsxlibhbbcc.lib + >> b32.bc
echo %fwh%\lib\hbpgsql.lib %fwh%\lib\libpq.lib + >> b32.bc
echo %fwh%\lib\drxlsx32_bcc.lib + >> b32.bc
echo %hdirl%\hbhpdf.lib + >> b32.bc
echo %hdirl%\libhpdf.lib + >> b32.bc
echo %hdirl%\png.lib + >> b32.bc
echo %hdirl%\hbwin.lib + >> b32.bc
echo %hdirl%\gtgui.lib + >> b32.bc
echo %hdirl%\hbrtl.lib + >> b32.bc
echo %hdirl%\hbvm.lib + >> b32.bc
echo %hdirl%\hblang.lib + >> b32.bc
echo %hdirl%\hbmacro.lib + >> b32.bc
echo %hdirl%\hbrdd.lib + >> b32.bc
echo %hdirl%\rddntx.lib + >> b32.bc
echo %hdirl%\rddcdx.lib + >> b32.bc
echo %hdirl%\rddfpt.lib + >> b32.bc
echo %hdirl%\hbsix.lib + >> b32.bc
echo %hdirl%\hbdebug.lib + >> b32.bc
echo %hdirl%\hbcommon.lib + >> b32.bc
echo %hdirl%\hbpp.lib + >> b32.bc
echo %hdirl%\hbcpage.lib + >> b32.bc
echo %hdirl%\hbcplr.lib + >> b32.bc
echo %hdirl%\hbct.lib + >> b32.bc
echo %hdirl%\hbpcre.lib + >> b32.bc
echo %hdirl%\xhb.lib + >> b32.bc
echo %hdirl%\hbziparc.lib + >> b32.bc
echo %hdirl%\hbmzip.lib + >> b32.bc
echo %hdirl%\hbzlib.lib + >> b32.bc
echo %hdirl%\minizip.lib + >> b32.bc
echo %hdirl%\hbusrrdd.lib + >> b32.bc
echo %hdirl%\hbtip.lib + >> b32.bc
echo %hdirl%\hbzebra.lib + >> b32.bc
echo %hdirl%\hbcurl.lib + >> b32.bc
echo %hdirl%\libcurl.lib + >> b32.bc
echo %fwh%\lib\dolphin.lib + >> b32.bc
echo %bcdir%\lib\cw32.lib + >> b32.bc
echo %bcdir%\lib\psdk\uuid.lib + >> b32.bc
echo %bcdir%\lib\import32.lib + >> b32.bc
echo %bcdir%\lib\psdk\ws2_32.lib + >> b32.bc
echo %bcdir%\lib\psdk\odbc32.lib + >> b32.bc
echo %bcdir%\lib\psdk\nddeapi.lib + >> b32.bc
echo %bcdir%\lib\psdk\iphlpapi.lib + >> b32.bc
echo %bcdir%\lib\psdk\msimg32.lib + >> b32.bc
echo %bcdir%\lib\psdk\psapi.lib + >> b32.bc
echo %bcdir%\lib\psdk\rasapi32.lib + >> b32.bc
echo %bcdir%\lib\psdk\gdiplus.lib + >> b32.bc
echo %bcdir%\lib\psdk\shell32.lib + >> b32.bc
echo %bcdir%\lib\psdk\uxtheme.lib , >> b32.bc

%bcdir%\bin\ilink32 -Gn -aa -Tpe -s @b32.bc
IF ERRORLEVEL 1 GOTO LINKERROR

ECHO * Listo: %PRG%.exe *
start %PRG%
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

:COMPILEERRORS
@type comp.log
@type warnings.log
ECHO * Errores de compilacion Harbour *
GOTO EXIT

:LINKERROR
ECHO * Errores de link *
ECHO   Ya se uso la lista COMPLETA de libs del build.bat real. Si sigue
ECHO   habiendo unresolved externals, es probable que sea un simbolo de
ECHO   nuestro propio codigo (dwg_hbfunc.c/dwg_viewer.prg) mal referenciado.
GOTO EXIT

:EXIT
