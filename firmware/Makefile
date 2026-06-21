# MPTT Firmware — convenience wrapper (Windows ARM GCC via build.bat)
.PHONY: all clean flash size

all:
	cmd.exe /c build.bat

clean:
	rm -rf build

flash:
	pyocd flash -t stm32wle5cbux build/fw.bin

size:
	arm-none-eabi-size build/fw.elf 2>/dev/null || ls -la build/fw.*
