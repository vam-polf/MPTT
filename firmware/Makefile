# MPTT Firmware Makefile
# Target: STM32WLE5CBU6 (Cortex-M4F)
#
# Build runs via cmd.exe /c build.bat (Windows ARM GCC from CubeIDE,
# not runnable directly from WSL due to CreateProcess limitation).
# This Makefile is a convenience wrapper.

.PHONY: all clean flash size

all:
	@rm -rf build
	@mkdir -p build
	@cmd.exe /c build.bat

clean:
	rm -rf build

flash:
	@echo "Flash with: pyocd.exe flash -t stm32wle5cbux build/fw_mptt.bin"

size:
	@ls -la build/fw_mptt.bin build/fw_mptt.elf 2>/dev/null
