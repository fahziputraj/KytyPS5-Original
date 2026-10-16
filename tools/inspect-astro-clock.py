"""Read the clock of our isolated Astro QA process; never write game memory."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('launch', type=Path)
args = p.parse_args()
launch = json.loads(args.launch.read_text())
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [w.HANDLE]
handle = k.OpenProcess(0x1000 | 0x10, False, launch['pid'])
if not handle:
    raise SystemExit('Test process is no longer available')
try:
    code = w.DWORD()
    assert k.GetExitCodeProcess(handle, c.byref(code)) and code.value == 259, 'Test process exited'
    name, length = c.create_unicode_buffer(32768), w.DWORD(32768)
    assert k.QueryFullProcessImageNameW(handle, 0, name, c.byref(length))
    assert Path(name.value).resolve() == Path(launch['exe']).resolve(), 'PID belongs to a different process'

    def read(address, size):
        data, received = c.create_string_buffer(size), c.c_size_t()
        assert k.ReadProcessMemory(handle, address, data, size, c.byref(received)), c.get_last_error()
        assert received.value == size
        return data.raw

    rate, seconds, micros = struct.unpack('<df4xQ', read(0x908fd4040, 24))
    print(json.dumps({'pid': launch['pid'], 'rate': rate, 'seconds_per_step': seconds,
                      'microseconds_per_step': micros,
                      'setter_prefix': read(0x9000034c0, 8).hex()}, indent=2))
finally:
    k.CloseHandle(handle)
