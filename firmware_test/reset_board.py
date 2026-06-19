"""Halt via under-reset, erase, then disconnect for pyocd flash."""
import time, sys, subprocess
from pyocd.probe.pydapaccess import DAPAccessCMSISDAP
from pyocd.probe.pydapaccess.dap_access_api import DAPAccessIntf

REG = DAPAccessIntf.REG
devices = DAPAccessCMSISDAP.get_connected_devices()
dap = devices[0]
dap.open()
dap.connect()
dap.set_clock(50000)

def write32(addr, value):
    dap.write_reg(REG.AP_0x4, addr)
    dap.write_reg(REG.AP_0xC, value)
    dap.flush()

def read32(addr):
    dap.write_reg(REG.AP_0x4, addr)
    dap.flush()
    return dap.read_reg(REG.AP_0xC)

def swd_init():
    dap.swj_sequence(51, 0x0007FFFFFFFFFFFF)
    dap.swj_sequence(16, 0xE79E)
    dap.swj_sequence(51, 0x0007FFFFFFFFFFFF)
    dap.swj_sequence(8, 0x00)
    dap.flush()

print("[1] Under-reset halt...")
dap.assert_reset(True)
time.sleep(0.1)
swd_init()
dpidr = dap.read_reg(REG.DP_0x0)
print(f"DPIDR=0x{dpidr:08X}")
dap.write_reg(REG.DP_0x4, 0x50000000)
dap.write_reg(REG.DP_0x8, 0x00000000)
dap.flush()
time.sleep(0.05)
write32(0xE000EDF0, 0xA05F0003)
write32(0xE000EDFC, 0x01000001)
dap.assert_reset(False)
time.sleep(0.05)
swd_init()
dap.read_reg(REG.DP_0x0)
dap.write_reg(REG.DP_0x4, 0x50000000)
dap.write_reg(REG.DP_0x8, 0x00000000)
dap.flush()
time.sleep(0.05)
dhcsr = read32(0xE000EDF0)
print(f"DHCSR=0x{dhcsr:08X} Halted={bool(dhcsr&0x20000)}")

print("[2] Mass erase...")
write32(0x58004008, 0x45670123)
write32(0x58004008, 0xCDEF89AB)
write32(0x58004014, 0x00010004)
for i in range(100):
    time.sleep(0.1)
    if not (read32(0x58004010) & 0x00010000):
        print(f"  Erased in {(i+1)*0.1:.1f}s")
        break
write32(0x58004014, read32(0x58004014) | 0x80000000)

dap.disconnect()
dap.close()
print("[3] SWD released. Flashing...")
