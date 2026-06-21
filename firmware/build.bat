@echo off
REM MPTT 主固件构建 (startup + audio driver + app)
cd /d %~dp0
if not exist build mkdir build

set TOOLDIR=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin
set GCC=%TOOLDIR%\arm-none-eabi-gcc-13.3.1.exe
set OBJCOPY=%TOOLDIR%\arm-none-eabi-objcopy.exe
set SIZE=%TOOLDIR%\arm-none-eabi-size.exe

set CFLAGS=-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -g -Wall -nostdlib -nostartfiles -ffreestanding -fno-builtin -Iinclude

echo [1/4] startup.c ...
"%GCC%" %CFLAGS% -c startup.c -o build\startup.o
if errorlevel 1 goto fail

echo [2/4] src\audio_wm8960.c ...
"%GCC%" %CFLAGS% -c src\audio_wm8960.c -o build\audio_wm8960.o
if errorlevel 1 goto fail

echo [3/4] src\main.c ...
"%GCC%" %CFLAGS% -c src\main.c -o build\main.o
if errorlevel 1 goto fail

echo [4/4] Linking ...
"%GCC%" -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -T link.ld -nostdlib -nostartfiles build\startup.o build\audio_wm8960.o build\main.o -o build\fw.elf
if errorlevel 1 goto fail

echo Creating binary ...
"%OBJCOPY%" -O binary build\fw.elf build\fw.bin
if errorlevel 1 goto fail

"%SIZE%" build\fw.elf
echo ======== BUILD SUCCESS ========
goto end
:fail
echo ======== BUILD FAILED ========
exit /b 1
:end
