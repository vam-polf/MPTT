@echo off
set TOOLSDIR=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin
set GCC=%TOOLSDIR%\arm-none-eabi-gcc-13.3.1.exe
set OBJCOPY=%TOOLSDIR%\arm-none-eabi-objcopy.exe
cd /d D:\projects\mptt\firmware_test
"%GCC%" -mcpu=cortex-m4 -mthumb -Os -nostdlib -nostartfiles -T link.ld startup.c main_minimal.c -o fw_minimal.elf
if errorlevel 1 goto :fail
"%OBJCOPY%" -O binary fw_minimal.elf fw_minimal.bin
echo BUILD_OK
goto :eof
:fail
echo BUILD_FAILED
