"""
Small diagnostics helper that attempts to create a Direct3D 11 device outside of the
Rive renderer. Use it to verify that the local workstation can initialise D3D11
hardware before debugging the renderer pipeline.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import uuid


_D3D11_SDK_VERSION = 7
_D3D11_CREATE_DEVICE_BGRA_SUPPORT = 0x20

_D3D_DRIVER_TYPE_HARDWARE = 1
_D3D_DRIVER_TYPE_WARP = 5

_FEATURE_LEVELS = (
    0x0000_0b01,  # D3D_FEATURE_LEVEL_11_1
    0x0000_0b00,  # D3D_FEATURE_LEVEL_11_0
)

_DXGI_ERROR_NOT_FOUND = 0x887A0002


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    @classmethod
    def from_uuid(cls, value: str | uuid.UUID) -> "GUID":
        uid = uuid.UUID(str(value))
        data4 = (ctypes.c_ubyte * 8)(*uid.bytes[8:])
        return cls(uid.time_low, uid.time_mid, uid.time_hi_version, data4)


class LUID(ctypes.Structure):
    _fields_ = [("LowPart", wintypes.DWORD), ("HighPart", wintypes.LONG)]


class DXGI_ADAPTER_DESC1(ctypes.Structure):
    _fields_ = [
        ("Description", wintypes.WCHAR * 128),
        ("VendorId", wintypes.UINT),
        ("DeviceId", wintypes.UINT),
        ("SubSysId", wintypes.UINT),
        ("Revision", wintypes.UINT),
        ("DedicatedVideoMemory", ctypes.c_ulonglong),
        ("DedicatedSystemMemory", ctypes.c_ulonglong),
        ("SharedSystemMemory", ctypes.c_ulonglong),
        ("AdapterLuid", LUID),
    ]


def _format_hresult(hr: int) -> str:
    hr &= 0xFFFF_FFFF
    kernel32 = ctypes.windll.kernel32
    buffer = ctypes.create_unicode_buffer(512)
    flags = 0x00001000 | 0x00000200  # FORMAT_MESSAGE_FROM_SYSTEM | IGNORE_INSERTS
    length = kernel32.FormatMessageW(
        flags,
        None,
        wintypes.DWORD(hr),
        0,
        buffer,
        len(buffer),
        None,
    )
    if length:
        return f"0x{hr:08X} ({buffer.value.strip()})"
    return f"0x{hr:08X}"


def _feature_level_name(level: int) -> str:
    mapping = {
        0x0000_0b01: "11_1",
        0x0000_0b00: "11_0",
        0x0000_0a01: "10_1",
        0x0000_0a00: "10_0",
        0x0000_0933: "9_3",
        0x0000_0922: "9_2",
        0x0000_0911: "9_1",
    }
    return mapping.get(level, f"0x{level:08X}")


def _release_com(pointer: ctypes.c_void_p) -> None:
    if not pointer or not pointer.value:
        return

    vtable = ctypes.cast(pointer, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))
    release = ctypes.CFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtable[0][2])
    release(pointer)


def _create_device_raw(adapter: ctypes.c_void_p | None, driver_type: int, creation_flags: int) -> tuple[int, ctypes.c_void_p, ctypes.c_void_p, int]:
    d3d11 = ctypes.windll.d3d11

    d3d11.D3D11CreateDevice.restype = ctypes.c_long
    d3d11.D3D11CreateDevice.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.POINTER(ctypes.c_uint),
        ctypes.c_uint,
        ctypes.c_uint,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_uint),
        ctypes.POINTER(ctypes.c_void_p),
    ]

    feature_array = (ctypes.c_uint * len(_FEATURE_LEVELS))(*_FEATURE_LEVELS)
    device_ptr = ctypes.c_void_p()
    context_ptr = ctypes.c_void_p()
    feature_level = ctypes.c_uint()

    hr = d3d11.D3D11CreateDevice(
        adapter,
        driver_type,
        None,
        creation_flags,
        feature_array,
        len(_FEATURE_LEVELS),
        _D3D11_SDK_VERSION,
        ctypes.byref(device_ptr),
        ctypes.byref(feature_level),
        ctypes.byref(context_ptr),
    )

    return int(hr), device_ptr, context_ptr, int(feature_level.value)


def _create_device(driver_type: int, creation_flags: int) -> tuple[int, ctypes.c_void_p, ctypes.c_void_p, int]:
    return _create_device_raw(None, driver_type, creation_flags)


def _enum_adapters() -> list[tuple[ctypes.c_void_p, DXGI_ADAPTER_DESC1]]:
    IID_IDXGIFactory1 = GUID.from_uuid("{770AAE78-F26F-4DBA-A829-253C83D1B387}")
    factory_ptr = ctypes.c_void_p()
    create_factory = ctypes.windll.dxgi.CreateDXGIFactory1
    create_factory.argtypes = [ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p)]
    create_factory.restype = ctypes.c_long

    hr = create_factory(ctypes.byref(IID_IDXGIFactory1), ctypes.byref(factory_ptr))
    if hr < 0 or not factory_ptr.value:
        print("CreateDXGIFactory1 failed", _format_hresult(hr))
        return []

    adapters: list[tuple[ctypes.c_void_p, DXGI_ADAPTER_DESC1]] = []
    factory_vtbl = ctypes.cast(factory_ptr, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))
    enum_adapters = ctypes.CFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p))(factory_vtbl[0][7])

    idx = 0
    while True:
        adapter_ptr = ctypes.c_void_p()
        hr_enum = enum_adapters(factory_ptr, idx, ctypes.byref(adapter_ptr))
        if hr_enum == _DXGI_ERROR_NOT_FOUND:
            break
        if hr_enum < 0 or not adapter_ptr.value:
            print(f"EnumAdapters1({idx}) failed", _format_hresult(hr_enum))
            break

        adapter_vtbl = ctypes.cast(adapter_ptr, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))
        get_desc1 = ctypes.CFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(DXGI_ADAPTER_DESC1))(adapter_vtbl[0][10])
        desc = DXGI_ADAPTER_DESC1()
        hr_desc = get_desc1(adapter_ptr, ctypes.byref(desc))
        if hr_desc < 0:
            print(f"IDXGIAdapter1::GetDesc1 failed for adapter {idx}", _format_hresult(hr_desc))
            release = ctypes.CFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(adapter_vtbl[0][2])
            release(adapter_ptr)
            idx += 1
            continue

        adapters.append((adapter_ptr, desc))
        idx += 1

    # caller is responsible for releasing adapters; release factory now
    _release_com(factory_ptr)
    return adapters


def _describe_adapter(desc: DXGI_ADAPTER_DESC1) -> str:
    name = desc.Description.rstrip("\x00")
    vram_gib = desc.DedicatedVideoMemory / (1024 ** 3)
    return f"{name} (vendor=0x{desc.VendorId:04X}, device=0x{desc.DeviceId:04X}, VRAM={vram_gib:.2f} GiB)"


def _run_check(force_driver: int | None) -> int:
    ole32 = ctypes.windll.ole32
    ole32.CoInitializeEx(None, 0x0)
    try:
        print("Enumerating adapters via IDXGIFactory1...")
        adapters = _enum_adapters()
        if adapters:
            for index, (adapter_ptr, desc) in enumerate(adapters):
                print(f"Adapter {index}: {_describe_adapter(desc)}")
                for flags, label in ((0, "flags=0"), (_D3D11_CREATE_DEVICE_BGRA_SUPPORT, "flags=BGRA")):
                    hr, device, context, feature_level = _create_device_raw(adapter_ptr, _D3D_DRIVER_TYPE_HARDWARE, flags)
                    if hr >= 0:
                        print(f"  Success ({label}): feature level {_feature_level_name(feature_level)}")
                        _release_com(context)
                        _release_com(device)
                        break
                    else:
                        print(f"  Failure ({label}):", _format_hresult(hr))
                _release_com(adapter_ptr)
        else:
            print("No adapters reported by DXGI.")

        print("\nAttempting driver-type creation...")
        drivers_to_try = []
        if force_driver is not None:
            drivers_to_try.append(force_driver)
        else:
            drivers_to_try.extend((_D3D_DRIVER_TYPE_HARDWARE, _D3D_DRIVER_TYPE_WARP))

        for driver in drivers_to_try:
            hr, device, context, feature_level = _create_device(driver, _D3D11_CREATE_DEVICE_BGRA_SUPPORT)
            if hr >= 0:
                driver_name = "hardware" if driver == _D3D_DRIVER_TYPE_HARDWARE else "WARP"
                print(f"Success: Direct3D11 device initialised using {driver_name} driver")
                print(f"Feature level: {_feature_level_name(feature_level)}")
                _release_com(context)
                _release_com(device)
                return 0

            print(
                "Failure: D3D11CreateDevice returned",
                _format_hresult(hr),
                "with driver",
                "hardware" if driver == _D3D_DRIVER_TYPE_HARDWARE else "WARP",
            )

        return 1
    finally:
        ole32.CoUninitialize()


def main() -> int:
    parser = argparse.ArgumentParser(description="Check whether Direct3D 11 can be initialised on this host.")
    parser.add_argument(
        "--warp",
        action="store_true",
        help="Skip hardware checks and validate the WARP software driver only.",
    )
    args = parser.parse_args()

    driver = _D3D_DRIVER_TYPE_WARP if args.warp else None
    return _run_check(driver)


if __name__ == "__main__":  # pragma: no cover - manual diagnostic entry point
    raise SystemExit(main())
