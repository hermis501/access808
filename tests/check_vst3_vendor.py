"""Checks that the built VST3 reports the manufacturer name intact.

The name ("Hermis Kasperavičius") has a non-ASCII letter that several build
steps have mangled before: MSVC without /utf-8, JUCE's moduleinfo helper, and
stock JUCE's PClassInfoW widening (see patches/). This reads the expected name
from ACCESS808_VENDOR in CMakeLists.txt and compares it, byte for byte, with:

- every "Vendor" entry in Contents/Resources/moduleinfo.json (all platforms),
- on Windows, what the plugin itself reports to a host: PFactoryInfo,
  PClassInfo2 (UTF-8) and PClassInfoW (UTF-16) for every class.

Usage: python tests/check_vst3_vendor.py <path to Access808.vst3 bundle>
Exits non-zero on any mismatch.
"""

import os
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
want = re.search(r'set\(ACCESS808_VENDOR "([^"]+)"\)', cmake).group(1)
bundle = Path(sys.argv[1])
failures = 0


def report(label, ok, got):
    global failures
    print(f"{'OK   ' if ok else 'WRONG'} {label}: {got!a}")
    if not ok:
        failures += 1


raw = (bundle / "Contents" / "Resources" / "moduleinfo.json").read_bytes()
vendors = re.findall(rb'"Vendor":\s*"([^"]*)"', raw)
report("moduleinfo.json has Vendor entries", len(vendors) > 0, len(vendors))
for v in vendors:
    report("moduleinfo.json Vendor", v == want.encode("utf-8"), v)

if os.name == "nt":
    import ctypes
    from ctypes import CFUNCTYPE, POINTER, c_char, c_int32, c_void_p

    dll = ctypes.WinDLL(str(bundle / "Contents" / "x86_64-win" / bundle.name))
    if hasattr(dll, "InitDll"):
        dll.InitDll()
    get_factory = dll.GetPluginFactory
    get_factory.restype = c_void_p
    factory = get_factory()
    vtable = ctypes.cast(ctypes.cast(factory, POINTER(c_void_p))[0], POINTER(c_void_p))

    def call(index, *args):
        fn = CFUNCTYPE(c_int32, c_void_p, *[type(a) for a in args])(vtable[index])
        return fn(factory, *args)

    # IPluginFactory: 3 getFactoryInfo, 4 countClasses.
    # IPluginFactory2: 7 getClassInfo2. IPluginFactory3: 8 getClassInfoUnicode.
    info = (c_char * (64 + 256 + 128 + 4))()
    call(3, ctypes.cast(info, c_void_p))
    v = bytes(info[:64]).split(b"\0")[0]
    report("PFactoryInfo vendor", v == want.encode("utf-8"), v)

    for i in range(call(4)):
        info2 = (c_char * (16 + 4 + 32 + 64 + 4 + 128 + 64 * 3))()
        call(7, c_int32(i), ctypes.cast(info2, c_void_p))
        off = 16 + 4 + 32 + 64 + 4 + 128
        v = bytes(info2[off:off + 64]).split(b"\0")[0]
        report(f"PClassInfo2[{i}] vendor", v == want.encode("utf-8"), v)

        infow = (c_char * (16 + 4 + 32 + 128 + 4 + 128 + 128 * 3))()
        call(8, c_int32(i), ctypes.cast(infow, c_void_p))
        off = 16 + 4 + 32 + 128 + 4 + 128
        s = bytes(infow[off:off + 128]).decode("utf-16-le", "replace").split("\0")[0]
        report(f"PClassInfoW[{i}] vendor", s == want, s)

sys.stdout.flush()
# Skip the plugin's static destructors: unloading it here is not what is tested.
os._exit(1 if failures else 0)
