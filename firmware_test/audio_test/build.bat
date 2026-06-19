@echo off
REM MPTT Audio Test - Build Script
REM Run from: D:\projects\mptt\firmware_test\audio_test\

set GCC_PATH=D:\Program Files\STM32CubeIDE_2.0.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712\tools\bin
set PATH=%GCC_PATH%;%PATH%

echo [1/3] Compiling...
arm-none-eabi-gcc.exe -mcpu=cortex-m4 -mthumb -Os -Wall -nostdlib -nostartfiles -T link.ld startup.c main.c -o audio_test.elf
if errorlevel 1 (
    echo COMPILE FAILED!
    pause
    exit /b 1
)

echo [2/3] Generating binary...
arm-none-eabi-objcopy.exe -O binary audio_test.elf audio_test.bin
if errorlevel 1 (
    echo OBJCOPY FAILED!
    pause
    exit /b 1
)

echo [3/3] Size info:
arm-none-eabi-size.exe audio_test.elf

echo.
echo === BUILD OK ===
echo Flash with: pyocd flash -t stm32wle5cbux -f 1000000 audio_test.bin --base-address 0x08000000
echo Read result: pyocd cmd -t stm32wle5cbux -f 1000000 -c "reset" -c "go" -c "sleep 3000" -c "read8 0x20000100 8"
echo.
echo Expected: AA 01 1A 01 01 01 55 xx
echo   [0]=AA boot  [1]=01 clk  [2]=1A i2c  [3]=01 wm_cfg
echo   [4]=01 mclk  [5]=01 i2s_ok  [6]=55 playing  [7]=loop counter
pause
