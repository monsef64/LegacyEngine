#!/usr/bin/env python3
"""Conservative PE packaging gate, not a Windows 7 runtime certification.

Run under MinGW/MSYS2 after bundling the executable and its runtime DLLs.
No downloaded system DLLs or UCRT shims are accepted by this distribution.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

SYSTEM_DLLS = set("""
advapi32.dll avrt.dll bcrypt.dll cfgmgr32.dll comctl32.dll comdlg32.dll
crypt32.dll d3d9.dll d3d11.dll dbghelp.dll dinput8.dll dsound.dll dwmapi.dll
dxgi.dll gdi32.dll hid.dll imm32.dll iphlpapi.dll kernel32.dll ksuser.dll
msacm32.dll msimg32.dll msvcrt.dll normaliz.dll ntdll.dll ole32.dll oleaut32.dll
opengl32.dll powrprof.dll propsys.dll psapi.dll rpcrt4.dll secur32.dll setupapi.dll
shell32.dll shlwapi.dll user32.dll usp10.dll uxtheme.dll version.dll winhttp.dll
wininet.dll winmm.dll winspool.drv wintrust.dll winusb.dll wldap32.dll ws2_32.dll
wtsapi32.dll urlmon.dll
""".split())
# These are known post-Win7 entry points. SDK target macros alone do not stop
# prebuilt compiler/runtime libraries from importing them.
NEW_APIS = set("""
CreateFile2 DiscardVirtualMemory GetDpiForSystem GetDpiForWindow
GetOverlappedResultEx GetSystemCpuSetInformation GetSystemTimePreciseAsFileTime
GetTempPath2A GetTempPath2W IsWow64Process2 SetProcessDpiAwareness
SetProcessDpiAwarenessContext SetThreadDescription SetThreadDpiAwarenessContext
VirtualAlloc2 VirtualAllocFromApp WaitOnAddress WakeByAddressAll WakeByAddressSingle
""".split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--objdump", default="objdump")
    args = parser.parse_args()
    files = sorted(p for p in args.directory.iterdir() if p.suffix.lower() in {".exe", ".dll"})
    if not any(p.suffix.lower() == ".exe" for p in files):
        parser.error("Package has no executable")
    bundled = {p.name.lower() for p in files}
    errors = []
    for file in files:
        result = subprocess.run([args.objdump, "-p", str(file)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        text = result.stdout
        if "pei-x86-64" not in text:
            errors.append(f"{file.name}: not an x86-64 PE binary")
        major = re.search(r"^MajorSubsystemVersion\s+(\d+)", text, re.M)
        minor = re.search(r"^MinorSubsystemVersion\s+(\d+)", text, re.M)
        if not major or not minor:
            errors.append(f"{file.name}: cannot determine subsystem version")
        elif (int(major[1]), int(minor[1])) > (6, 1):
            errors.append(f"{file.name}: subsystem requires newer than Windows 7")
        for dll in re.findall(r"DLL Name:\s*(\S+)", text):
            name = dll.lower()
            if name == "ucrtbase.dll" or name.startswith("api-ms-win-crt-"):
                errors.append(f"{file.name}: UCRT import {dll}; use MINGW64/MSVCRT")
            elif name not in SYSTEM_DLLS and name not in bundled:
                errors.append(f"{file.name}: unbundled or unreviewed dependency {dll}")
        for symbol in NEW_APIS:
            if re.search(r"^\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+" + re.escape(symbol) + r"\s*$", text, re.M):
                errors.append(f"{file.name}: imports post-Win7 API {symbol}")
    if errors:
        print("Windows 7 package import audit FAILED:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(f"Static import audit passed for {len(files)} packaged binaries.")
    print("Manual Windows 7 SP1/OpenGL-driver validation is still required.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
