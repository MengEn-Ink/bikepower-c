import ctypes, ctypes.wintypes as wt, sys

k32 = ctypes.windll.kernel32
k32.LoadLibraryW.argtypes = [wt.LPCWSTR]
k32.LoadLibraryW.restype = wt.HMODULE

INJECT = r"D:\app\DoubaoWork\app\sbox\x64\aiep_sbox.dll"
BLE = r"D:\app\Zwift\BleWin10Lib.dll"

print("step1 加载注入模块 aiep_sbox.dll ...")
h1 = k32.LoadLibraryW(INJECT)
print("  aiep_sbox load =", "OK" if h1 else f"FAIL err={ctypes.GetLastError()}")

print("step2 再加载 Zwift BLE 库 ...")
h2 = k32.LoadLibraryW(BLE)
print("  BleWin10Lib load =", "OK" if h2 else f"FAIL err={ctypes.GetLastError()}")
