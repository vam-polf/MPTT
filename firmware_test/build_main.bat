@echo off
set "GCC=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin"
set "PATH=%GCC%;%PATH%"
cd /d "D:\projects\mptt\firmware_test"
arm-none-eabi-gcc.exe -mcpu=cortex-m4 -mthumb -Os -Wall -nostdlib -nostartfiles -T link.ld startup.c main_minimal.c -o main_minimal.elf
if errorlevel 1 exit /b 1
arm-none-eabi-objcopy.exe -O binary main_minimal.elf main_minimal.bin
if errorlevel 1 exit /b 1
arm-none-eabi-size.exe main_minimal.elf
echo BUILD_OK
