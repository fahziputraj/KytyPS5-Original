"""Sample instruction pointers and thread CPU time in one chosen Windows process."""
import argparse
import ctypes as c
import json
import time
from collections import Counter
from ctypes import wintypes as w

p = argparse.ArgumentParser()
p.add_argument("pid", type=int)
p.add_argument("--output", required=True)
args = p.parse_args()
k = c.WinDLL("kernel32", use_last_error=True)
k.CreateToolhelp32Snapshot.restype = w.HANDLE
k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenThread.restype = w.HANDLE
k.SuspendThread.argtypes = [w.HANDLE]
k.ResumeThread.argtypes = [w.HANDLE]
k.GetThreadContext.argtypes = [w.HANDLE, c.c_void_p]
k.CloseHandle.argtypes = [w.HANDLE]
k.GetThreadTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4

class Entry(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                ("pid", w.DWORD), ("base", w.LONG), ("delta", w.LONG), ("flags", w.DWORD)]

k.Thread32First.argtypes = [w.HANDLE, c.POINTER(Entry)]
k.Thread32Next.argtypes = [w.HANDLE, c.POINTER(Entry)]
snapshot = k.CreateToolhelp32Snapshot(4, 0)
entry = Entry(size=c.sizeof(Entry))
handles = {}
try:
    ok = k.Thread32First(snapshot, c.byref(entry))
    while ok:
        if entry.pid == args.pid:
            handle = k.OpenThread(0x2 | 0x8 | 0x40, False, entry.tid)
            if handle:
                handles[entry.tid] = handle
        ok = k.Thread32Next(snapshot, c.byref(entry))
finally:
    k.CloseHandle(snapshot)

def cpu(handle):
    times = [w.FILETIME() for _ in range(4)]
    if not k.GetThreadTimes(handle, *[c.byref(t) for t in times]):
        return 0
    return sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 10_000_000

starts = {tid: cpu(h) for tid, h in handles.items()}
samples = {tid: Counter() for tid in handles}
try:
    for _ in range(15):
        for tid, handle in handles.items():
            storage = c.create_string_buffer(1248)
            address = (c.addressof(storage) + 15) & ~15
            c.c_uint32.from_address(address + 48).value = 0x100001
            if k.SuspendThread(handle) == 0xFFFFFFFF:
                continue
            try:
                if k.GetThreadContext(handle, address):
                    samples[tid][hex(c.c_uint64.from_address(address + 248).value)] += 1
            finally:
                k.ResumeThread(handle)
        time.sleep(0.1)
    result = sorted([{"tid": tid, "cpu_seconds": cpu(h) - starts[tid],
                      "instruction_pointers": samples[tid].most_common(5)}
                     for tid, h in handles.items()], key=lambda x: -x["cpu_seconds"])
finally:
    for handle in handles.values():
        k.CloseHandle(handle)
with open(args.output, "w", encoding="utf-8") as f:
    json.dump(result, f, indent=2)
print(json.dumps(result[:8], indent=2))
