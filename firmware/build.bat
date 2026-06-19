@echo off
REM Phase 0 Build — 最小系统验证 (startup + main)
set TOOLDIR=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin
set GCC=%TOOLDIR%\arm-none-eabi-gcc-13.3.1.exe
set OBJCOPY=%TOOLDIR%\arm-none-eabi-objcopy.exe
set SIZE=%TOOLDIR%\arm-none-eabi-size.exe

cd /d D:\projects\mptt\firmware
if not exist build mkdir build

set CFLAGS=-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -g -Wall -nostdlib -nostartfiles -ffreestanding -fno-builtin

echo [1/3] startup.c ...
"%GCC%" %CFLAGS% -c startup.c -o build\startup.o
if errorlevel 1 goto fail

echo [2/3] main.c ...
"%GCC%" %CFLAGS% -c main.c -o build\main.o
if errorlevel 1 goto fail

echo [3/3] Linking ...
"%GCC%" -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -T link.ld -nostdlib -nostartfiles build\startup.o build\main.o -o build\fw.elf
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
