"""Check real installer payloads without installing a product on the machine."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def api(dll, name, result, *arguments):
    function = getattr(dll, name)
    function.restype, function.argtypes = result, arguments
    return function


def verify_setup(setup, expected):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    load = api(kernel, "LoadLibraryExW", w.HMODULE, w.LPCWSTR, w.HANDLE, w.DWORD)
    find = api(kernel, "FindResourceW", w.HANDLE, w.HMODULE, c.c_void_p, c.c_void_p)
    size = api(kernel, "SizeofResource", w.DWORD, w.HMODULE, w.HANDLE)
    resource = api(kernel, "LoadResource", w.HANDLE, w.HMODULE, w.HANDLE)
    lock = api(kernel, "LockResource", c.c_void_p, w.HANDLE)
    free = api(kernel, "FreeLibrary", w.BOOL, w.HMODULE)
    module = load(str(setup), None, 2)  # LOAD_LIBRARY_AS_DATAFILE; do not execute it.
    require(module, "Cannot read setup resources")
    try:
        entry = find(module, 101, 10)  # Application RCDATA payload.
        require(entry, "Setup payload is missing")
        pointer = lock(resource(module, entry))
        require(pointer, "Setup payload cannot be loaded")
        require(c.string_at(pointer, size(module, entry)) == expected,
                "Setup payload differs from Archiwa.exe")
    finally:
        free(module)
    print("EXE INSTALLER PAYLOAD: PASS", flush=True)


def verify_msi(msi_path, expected, version, folder):
    msi = c.WinDLL("msi")
    handle = w.UINT
    open_db = api(msi, "MsiOpenDatabaseW", w.UINT, w.LPCWSTR, c.c_void_p, c.POINTER(handle))
    open_view = api(msi, "MsiDatabaseOpenViewW", w.UINT, handle, w.LPCWSTR, c.POINTER(handle))
    execute = api(msi, "MsiViewExecute", w.UINT, handle, handle)
    fetch = api(msi, "MsiViewFetch", w.UINT, handle, c.POINTER(handle))
    get_string = api(msi, "MsiRecordGetStringW", w.UINT, handle, w.UINT, w.LPWSTR, c.POINTER(w.DWORD))
    read_stream = api(msi, "MsiRecordReadStream", w.UINT, handle, w.UINT, c.c_void_p, c.POINTER(w.DWORD))
    close = api(msi, "MsiCloseHandle", w.UINT, handle)

    def check(code):
        if code:
            raise OSError(code, "Reading MSI database")

    database = handle()
    check(open_db(str(msi_path), None, c.byref(database)))
    try:
        def read(sql, stream=False):
            view, record = handle(), handle()
            check(open_view(database, sql, c.byref(view)))
            try:
                check(execute(view, 0))
                check(fetch(view, c.byref(record)))
                if stream:
                    chunks = []
                    while True:
                        buffer, count = c.create_string_buffer(65536), w.DWORD(65536)
                        check(read_stream(record, 1, buffer, c.byref(count)))
                        if not count.value:
                            return b"".join(chunks)
                        chunks.append(buffer.raw[:count.value])
                text, count = c.create_unicode_buffer(1024), w.DWORD(1024)
                check(get_string(record, 1, text, c.byref(count)))
                return text.value
            finally:
                if record.value:
                    close(record)
                close(view)

        require(read("SELECT `Value` FROM `Property` WHERE `Property` = 'ProductVersion'") == version,
                "MSI ProductVersion differs from version.txt")
        cabinet = folder / "payload.cab"
        cabinet.write_bytes(read("SELECT `Data` FROM `_Streams` WHERE `Name` = 'app.cab'", stream=True))
        extracted = folder / "extracted"
        extracted.mkdir()
        subprocess.run(["expand.exe", "-R", str(cabinet), "-F:*", str(extracted)], check=True)
        require((extracted / "ArchiwaBinary").read_bytes() == expected,
                "MSI cabinet payload differs from Archiwa.exe")
    finally:
        close(database)
    print("MSI VERSION AND CABINET PAYLOAD: PASS", flush=True)


def main():
    project = Path(__file__).resolve().parents[1]
    release = Path(sys.argv[1]).resolve()
    version = (project / "version.txt").read_text(encoding="utf-8").strip()
    expected = (release / "Archiwa.exe").read_bytes()
    for name in ("Archiwa.exe", "Archiwa-Setup-x64.exe"):
        data = (release / name).read_bytes()
        offset = struct.unpack_from("<I", data, 0x3C)[0]
        require(data[offset:offset + 4] == b"PE\0\0", f"{name}: invalid PE signature")
        require(struct.unpack_from("<H", data, offset + 4)[0] == 0x8664, f"{name}: expected x64")
    print("APPLICATION AND SETUP X64 PE HEADERS: PASS", flush=True)
    verify_setup(release / "Archiwa-Setup-x64.exe", expected)
    with tempfile.TemporaryDirectory(prefix="package-check-", dir=project / "build") as temporary:
        folder = Path(temporary)
        verify_msi(release / f"Archiwa-{version}-x64.msi", expected, version, folder)
        subprocess.run([str(project / "build" / "Archiwa-Setup-Smoke.exe"), "--smoke-lifecycle"],
                       env=dict(os.environ, LOCALAPPDATA=str(folder)), check=True, timeout=30)
    print("SETUP COPY, SHORTCUT, REINSTALL AND REMOVAL: PASS", flush=True)
    with zipfile.ZipFile(release / f"Archiwa-{version}-Store-Preview-x64.msix") as package:
        require(package.read("Archiwa.exe") == expected, "MSIX payload differs from Archiwa.exe")
        manifest = ET.fromstring(package.read("AppxManifest.xml"))
        identity = manifest.find("{http://schemas.microsoft.com/appx/manifest/foundation/windows10}Identity")
        require(identity is not None and identity.get("Version") == version + ".0", "MSIX version mismatch")
        require(identity.get("ProcessorArchitecture") == "x64", "MSIX architecture mismatch")
    print("MSIX VERSION, ARCHITECTURE AND PAYLOAD: PASS", flush=True)


if __name__ == "__main__":
    main()
