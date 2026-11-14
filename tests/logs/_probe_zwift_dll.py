import ctypes, ctypes.wintypes as wt, sys

k32 = ctypes.windll.kernel32
k32.LoadLibraryW.argtypes = [wt.LPCWSTR]
k32.LoadLibraryW.restype = wt.HMODULE
CoInitializeEx = ctypes.windll.ole32.CoInitializeEx
CoInitializeEx.argtypes = [ctypes.c_void_p, wt.DWORD]
CoInitializeEx.restype = ctypes.c_long

DLL = {"main": r"D:\app\Zwift\BleWin10Lib.dll",
       "fallback": r"D:\app\Zwift\BleWin10Lib_fallback.dll"}[sys.argv[1]]
apt = {"none": None, "sta": 0, "mta": 2}[sys.argv[2]]

hr = None if apt is None else CoInitializeEx(None, apt)
h = k32.LoadLibraryW(DLL)
err = 0 if h else ctypes.GetLastError()
print(f"dll={sys.argv[1]} mode={sys.argv[2]} CoInit={hr} load={'OK' if h else 'FAIL err=' + str(err)}")
