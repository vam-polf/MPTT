@echo off
set "GCC=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin"
set "PATH=%GCC%;%PATH%"
cd /d "D:\projects\mptt\firmware_test\audio_test"
arm-none-eabi-gcc.exe -mcpu=cortex-m4 -mthumb -Os -Wall -nostdlib -nostartfiles -T link.ld startup.c bypass_test.c -o bypass_test.elf
if errorlevel 1 exit /b 1
arm-none-eabi-objcopy.exe -O binary bypass_test.elf bypass_test.bin
if errorlevel 1 exit /b 1
arm-none-eabi-size.exe bypass_test.elf
echo BUILD_OK
