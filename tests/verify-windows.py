import ctypes as c
import binascii
import struct
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes as w
import zipfile
from datetime import date

user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)

def native(dll, name, output, *args):
    fn = getattr(dll, name)
    fn.restype, fn.argtypes = output, args
    return fn

callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
windows = native(user, "EnumWindows", w.BOOL, callback, w.LPARAM)
children = native(user, "EnumChildWindows", w.BOOL, w.HWND, callback, w.LPARAM)
pid_of = native(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
is_visible = native(user, "IsWindowVisible", w.BOOL, w.HWND)
get_name = native(user, "GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
get_text = native(user, "GetWindowTextW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
get_id = native(user, "GetDlgCtrlID", c.c_int, w.HWND)
send = native(user, "SendMessageW", c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
post = native(user, "PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
allocate = native(kernel, "GlobalAlloc", w.HANDLE, w.UINT, c.c_size_t)
lock = native(kernel, "GlobalLock", c.c_void_p, w.HANDLE)
unlock = native(kernel, "GlobalUnlock", w.BOOL, w.HANDLE)
release = native(kernel, "GlobalFree", w.HANDLE, w.HANDLE)

class DropFiles(c.Structure):
    _fields_ = [("offset", w.DWORD), ("point", w.POINT), ("client", w.BOOL), ("wide", w.BOOL)]

def drop(hwnd, files):
    blob = ("\0".join(map(str,files))+"\0\0").encode("utf-16le")
    head = DropFiles(c.sizeof(DropFiles), w.POINT(), 0, 1)
    handle = allocate(0x42,c.sizeof(head)+len(blob))
    assert handle
    ptr = lock(handle)
    assert ptr
    c.memmove(ptr,c.byref(head),c.sizeof(head))
    c.memmove(ptr+c.sizeof(head),blob,len(blob))
    unlock(handle)
    if not post(hwnd,0x0233,handle,0):
        release(handle)
        raise c.WinError(c.get_last_error())

def identify(hwnd, getter):
    buf = c.create_unicode_buffer(4096)
    getter(hwnd, buf, len(buf))
    return buf.value

def title(hwnd):
    buf = c.create_unicode_buffer(4096)
    send(hwnd, 0x000d, len(buf), c.addressof(buf))
    return buf.value

def kind(hwnd):
    return identify(hwnd, get_name)

def inspect(process, class_name=None):
    found = []
    @callback
    def pick(hwnd, _):
        pid = w.DWORD()
        pid_of(hwnd, c.byref(pid))
        if pid.value == process.pid and is_visible(hwnd) and (class_name is None or kind(hwnd) == class_name):
            found.append(hwnd)
        return 1
    windows(pick, 0)
    return next(iter(found), 0)

def wait_until(check, label, seconds=5):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = check()
        if value:
            return value
        time.sleep(0.04)
    raise AssertionError(label)

def widget_list(parent):
    found = []
    @callback
    def pick(hwnd, _):
        if is_visible(hwnd):
            found.append(hwnd)
        return 1
    children(parent, pick, 0)
    return found

def find_widget(parent, id):
    for h in widget_list(parent):
        if get_id(h) == id:
            return h
    return 0

def widget(parent, id):
    return wait_until(lambda: find_widget(parent,id), f"Control {id} not visible")

def button(parent, id):
    return post(widget(parent, id), 0xf5, 0, 0)

def choose(hwnd, index):
    assert send(hwnd, 0x14e, index, 0) == index

def set_text(hwnd, text):
    data = c.create_unicode_buffer(text)
    assert send(hwnd, 0x000c, 0, c.addressof(data)) == 1

def dismiss(process, id):
    dlg = wait_until(lambda: inspect(process, "#32770"), "Expected dialog")
    button(dlg, id)

def stop(process, hwnd):
    if process.poll() is None:
        post(hwnd, 0x10, 0, 0)
        try:
            assert process.wait(5) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(5)

def start(executable, args=()):
    return subprocess.Popen([str(executable), *map(str,args)], cwd=area, env=env)


def status_has(window, text):
    return any(text in title(h) for h in widget_list(window) if kind(h) == "Static")


def test_profiles(executable):
    source = area / "Budowa"
    source.mkdir()
    (source / "Zdjęcia").mkdir()
    (source / "Robocze").mkdir()
    (source / "odbiór.PDF").write_bytes(b"report")
    (source / "Zdjęcia" / "etap.JPG").write_bytes(b"photo")
    (source / "Robocze" / "tajne.pdf").write_bytes(b"draft")
    (source / "kosztorys.xlsx").write_bytes(b"sheet")
    output = area / f"Budowa_{date.today().isoformat()}.zip"
    app = start(executable)
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App opens for profiles")
    try:
        button(window,118)
        for control_id,value in ((120,"Paczka dla klienta"),(121,source),(123,".pdf; *.jpg"),
                                 (124,"Robocze"),(125,area),(127,"{folder}_{date}")):
            set_text(widget(window,control_id),str(value))
        button(window,128)
        wait_until(lambda: status_has(window,"Profile saved"),"Save ZIP profile")
        button(window,130)
        wait_until(lambda: status_has(window,"Files to pack: 2"),"Filter ZIP preview")
        assert not output.exists()
        button(window,131)
        wait_until(output.is_file,"Create profile ZIP")
        wait_until(lambda: status_has(window,"Profile ZIP saved"),"Profile ZIP finished")
        with zipfile.ZipFile(output) as archive:
            assert set(archive.namelist()) == {"odbiór.PDF","Zdjęcia/etap.JPG"}
            assert archive.read("Zdjęcia/etap.JPG") == b"photo"
    finally:
        stop(app,window)
    app = start(executable,("--profile",source))
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App opens profile from Explorer arguments")
    try:
        assert title(widget(window,120)) == "Paczka dla klienta"
        assert title(widget(window,121)) == str(source)
        button(window,130)
        wait_until(lambda: status_has(window,"Files to pack: 2"),"Resume saved ZIP profile")
        (source / "odbiór.PDF").write_bytes(b"report updated")
        button(window,131)
        dialog = wait_until(lambda: inspect(app,"#32770"),"Changed source error")
        assert any("Files changed since the preview" in title(h) for h in widget_list(dialog))
        ok = next(h for h in widget_list(dialog) if kind(h)=="Button")
        assert post(ok,0xf5,0,0)
        wait_until(lambda: not inspect(app,"#32770"),"Dismiss changed source error")
        wait_until(lambda: status_has(window,"Files changed since the preview"),"Detect files changed after preview")
        assert not output.with_name(f"{output.stem} (2).zip").exists()
        button(window,130)
        wait_until(lambda: status_has(window,"Files to pack: 2"),"Refresh stale preview")
        button(window,131)
        wait_until(output.with_name(f"{output.stem} (2).zip").is_file,"Avoid overwriting first profile ZIP")
    finally:
        stop(app,window)
    print("PROFILE SAVE, RESTART, FILTERED PREVIEW, STALE INPUT AND ZIP OUTPUT: PASS",flush=True)


def test_queue(executable, dest):
    error = area / "bad.zip"
    error.write_bytes(b"Not an archive")
    nested = area / "incoming"
    nested.mkdir()
    repeated = nested / "sample.zip"
    with zipfile.ZipFile(repeated,"w") as archive:
        archive.writestr("second.txt","unique contents")
    app = start(executable)
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App opens for queue")
    try:
        drop(window,[error,area / "new.zip",repeated,area / "example.rar",area / "new.7z"])
        widget(window,135)
        listing = next(h for h in widget_list(window) if kind(h)=="SysListView32")
        wait_until(lambda: send(listing,0x1004,0,0)==5,"Five archives in the queue")
        button(window,135)
        dismiss(app,6)
        wait_until(lambda: status_has(window,"Extracted: 3   Errors: 2   Waiting: 0"),"Extraction queue summary")
        assert (dest / "sample (2)" / "second.txt").read_text() == "unique contents"
        assert (dest / "example (2)" / "example.txt").read_bytes() == b"RAR test"
        assert (dest / "new" / names[0]).read_text() == "Archiwa ZIP"
        assert not (dest / "bad").exists()
        assert not any(item.name.startswith(".archiwa-") for item in dest.iterdir())
        button(window,135)
        dismiss(app,6)
        wait_until(lambda: status_has(window,"Extracted: 3   Errors: 2   Waiting: 0"),"Retry extraction queue")
        assert not (dest / "sample (3)").exists()
    finally:
        stop(app,window)
    app = start(executable,("--extract-here",repeated))
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App opens for Extract here")
    try:
        extracted = nested / "sample" / "second.txt"
        wait_until(extracted.is_file,"Extract beside the archive")
        assert extracted.read_text() == "unique contents"
    finally:
        stop(app,window)
    print("MIXED QUEUE, CONTINUATION AFTER ERRORS, RETRY, AND EXTRACT HERE: PASS",flush=True)


def test_information_and_settings(executable, version, dest, archive):
    legacy_area = area / "legacy-preferences"
    legacy_area.mkdir()
    path = (str(dest) + "\0").encode("utf-16le").ljust(8192, b"\0")
    prefs = legacy_area / "ArchiwaPreferences.dat"
    prefs.write_bytes(struct.pack("<BB2xii", 0, 0, 0, 1) + path)
    old_local = env["LOCALAPPDATA"]
    env["LOCALAPPDATA"] = str(legacy_area)
    try:
        app = start(executable)
        window = wait_until(lambda: inspect(app, "ArchiwaMain"), "Load older settings")
        try:
            assert title(widget(window, 104)) == "Settings"
            button(window, 138)
            wait_until(lambda: "ZIP profiles and the extraction queue" in title(widget(window, 147)), "English help")
            button(window, 139)
            wait_until(lambda: f"Archiwa {version} (x64)" in title(widget(window, 147)), "Display release version")
            assert "PoloQP" in title(widget(window, 147))
            button(window, 140)
            wait_until(lambda: "When reporting an issue" in title(widget(window, 147)), "English contact")
            assert title(widget(window, 141)) == "Copy support details"
            button(window, 104)
            assert title(widget(window, 113)) == str(dest)
            assert send(widget(window, 144), 0x147, 0, 0) == 0
            assert send(widget(window, 145), 0xf0, 0, 0) == 0
            choose(widget(window, 112), 0)
            choose(widget(window, 144), 2)
            send(widget(window, 145), 0xf1, 1, 0)
            button(window, 114)
            wait_until(lambda: title(widget(window, 104)) == "Ustawienia", "Apply Polish settings")
            wait_until(lambda: status_has(window, "Kontakt z wydawcą"), "Polish contact")
        finally:
            stop(app, window)
        app = start(executable)
        window = wait_until(lambda: inspect(app, "ArchiwaMain"), "Restore startup view")
        try:
            assert title(widget(window, 118)) == "Profile ZIP"
            assert widget(window, 128)
            button(window, 138)
            wait_until(lambda: "Otwieranie i rozpakowywanie" in title(widget(window, 147)), "Polish help")
            button(window, 104)
            assert send(widget(window, 144), 0x147, 0, 0) == 2
            assert send(widget(window, 145), 0xf0, 0, 0) == 1
            button(window, 146)
            wait_until(lambda: title(widget(window, 113)) == "", "Reset extraction folder")
            assert send(widget(window, 144), 0x147, 0, 0) == 0
            assert send(widget(window, 145), 0xf0, 0, 0) == 0
            button(window, 117)
        finally:
            stop(app, window)
        app = start(executable, (archive,))
        window = wait_until(lambda: inspect(app, "ArchiwaMain"), "Open archive from command line")
        try:
            listing = wait_until(lambda: next((h for h in widget_list(window)
                                               if kind(h) == "SysListView32"), 0), "Open archive view")
            wait_until(lambda: send(listing, 0x1004, 0, 0) == 2, "Archive overrides startup view")
            button(window, 102)
            wait_until(lambda: (dest / "sample (3)").is_dir(), "Use saved destination without prompting")
            assert not inspect(app, "#32770")
        finally:
            stop(app, window)
    finally:
        env["LOCALAPPDATA"] = old_local
    print("HELP, ABOUT, CONTACT, SETTINGS MIGRATION, STARTUP AND DEFAULT FOLDER: PASS", flush=True)


project = Path(__file__).resolve().parents[1]
release = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else project.parent
version = (project / "version.txt").read_text(encoding="utf-8").strip()
build = project / "build"
with tempfile.TemporaryDirectory(prefix="validation-",dir=build) as tmp:
    area = Path(tmp)
    env = dict(os.environ, LOCALAPPDATA=str(area))
    setup = start(release / "Archiwa-Setup-x64.exe")
    window = wait_until(lambda: inspect(setup, "ArchiwaSetup"), "Setup window")
    try:
        print("SETUP WINDOW",ascii(title(window)),flush=True)
        lang = widget(window,3)
        choose(lang,1)
        send(window,0x111,3|(1<<16),lang)
        assert title(window) == "Archiwa Setup" and title(widget(window,1)) == "Install"
        button(window,1)
        installed=area / "Archiwa" / "Archiwa.exe"
        wait_until(installed.is_file,"Installer did not create Archiwa.exe")
        dlg=wait_until(lambda: inspect(setup,"#32770"),"Setup result")
        lines=[title(h) for h in widget_list(dlg)]
        assert any("installed" in line for line in lines), lines
        assert button(dlg,7)
        assert setup.wait(5)==0
        assert installed.read_bytes() == (release / "Archiwa.exe").read_bytes()
        print("INSTALL BUTTON AND INSTALLED EXECUTABLE: PASS",flush=True)
    finally:
        stop(setup,window)
    lifecycle_root=area / "lifecycle"
    lifecycle_root.mkdir()
    env["LOCALAPPDATA"]=str(lifecycle_root)
    lifecycle = start(build / "Archiwa-Setup-Smoke.exe",("--smoke-lifecycle",))
    assert lifecycle.wait(8) == 0, "Installer payload or shell shortcut failed"
    print("SETUP PAYLOAD, SHORTCUT, REINSTALL AND REMOVAL: PASS",flush=True)
    env["LOCALAPPDATA"]=str(area)
    dest = area / "Rozpakowane"
    dest.mkdir()
    archive = area / "sample.zip"
    names = ("\u017c\u00f3\u0142\u0107 [1].txt","\u4e2d\u6587.txt")
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
        z.writestr(names[0],"Archiwa ZIP")
        z.writestr(names[1],"Unicode")
    app = start(installed,(archive,))
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App window")
    try:
        print("APP WINDOW",ascii(title(window)),flush=True)
        listings=[h for h in widget_list(window) if kind(h) == "SysListView32"]
        assert len(listings)==1
        wait_until(lambda: send(listings[0],0x1004,0,0)==2,"List must show both archive entries")
        print("ZIP LISTING: 2 FILES",flush=True)
        button(window,104)
        lang = wait_until(lambda: widget(window,112), "Settings")
        choose(lang,1)
        set_text(widget(window,113),str(dest))
        button(window,114)
        wait_until(lambda: status_has(window,"Settings saved."),"Save preferences completed")
        wait_until(lambda: title(widget(window,104))=="Settings","English after saving")
        assert (area / "ArchiwaPreferences.dat").is_file(), "Preferences file missing"
        button(window,103)
        wait_until(lambda: any("Archive data checked" in title(h) for h in widget_list(window)), "Archive data test")
        button(window,102)
        dismiss(app,6)
        out=dest / "sample"
        wait_until(out.is_dir,"Extraction failed")
        assert (out / names[0]).read_text() == "Archiwa ZIP"
        assert (out / names[1]).read_text() == "Unicode"
        print("ZIP TEST AND EXTRACTION: PASS",flush=True)
        button(window,105)
        wait_until(lambda: title(widget(window,106)).startswith("Add files"),"Create interface")
        assert title(widget(window,109)).startswith("Create archive")
        print("NEW ARCHIVE UI: PASS",flush=True)
        drop(window,[out / names[0],out / names[1]])
        sources = next(h for h in widget_list(window) if kind(h)=="ListBox")
        wait_until(lambda: send(sources,0x18b,0,0)==2,"Dropped files not added")
        for index, extension in enumerate((".zip",".7z",".tar.gz")):
            choose(widget(window,110),index)
            send(window,0x111,110|(1<<16),widget(window,110))
            if index == 0:
                set_text(widget(window,111),"test-password")
            target = area / ("new"+extension)
            button(window,109)
            dialog = wait_until(lambda: inspect(app,"#32770"),"Save archive dialog")
            filename_box = next((h for h in widget_list(dialog) if kind(h)=="Edit" and get_id(h)==1001),None)
            if filename_box is None:
                print("DIALOG CONTROLS",[(get_id(h),kind(h),ascii(title(h))) for h in widget_list(dialog)],flush=True)
                raise AssertionError("File name control was not found")
            set_text(filename_box,str(target))
            button(dialog,1)
            try:
                wait_until(target.is_file,f"Creation failed for {extension}")
            except AssertionError:
                current = inspect(app,"#32770")
                print("DIALOG", ascii(title(current)) if current else "-", [(get_id(h),kind(h),ascii(title(h))) for h in widget_list(current)] if current else [],flush=True)
                print("ROOT", [(get_id(h),kind(h),ascii(title(h))) for h in widget_list(window)],flush=True)
                raise
            dismiss(app,7)
            assert target.stat().st_size > 20
        with zipfile.ZipFile(area / "new.zip") as z:
            assert set(z.namelist()) == set(names)
            assert all(file.flag_bits & 1 and file.compress_type==99 for file in z.infolist())
        button(window,100)
        drop(window,[area / "new.zip"])
        wait_until(lambda: any(title(h).endswith("new.zip") for h in widget_list(window)),"Open protected ZIP")
        button(window,103)
        password_window=wait_until(lambda: inspect(app,"ArchiwaPassword"),"Password prompt")
        set_text(widget(password_window,3),"test-password")
        button(password_window,1)
        wait_until(lambda: any("Archive data checked" in title(h) for h in widget_list(window)),"Verify encrypted ZIP")
        print("DRAG & DROP, CREATE ZIP / 7z / TAR.GZ, AND AES ZIP: PASS",flush=True)
    finally:
        stop(app,window)
    app = start(installed)
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App restarts")
    try:
        assert title(widget(window,104))=="Settings"
        button(window,104)
        assert wait_until(lambda: widget(window,113),"Settings loaded")
        print("ENGLISH AND EXTRACTION PREFERENCES PERSIST: PASS",flush=True)
    finally:
        stop(app,window)
    def rar_header(raw):
        return struct.pack("<H", binascii.crc32(raw)&0xffff) + raw
    rar = area / "example.rar"
    content=b"RAR test"; name=b"example.txt"
    archive_header=rar_header(struct.pack("<BHHHI",0x73,0,13,0,0))
    file_header=rar_header(struct.pack("<BHHIIBIIBBHI",0x74,0x8000,32+len(name),len(content),len(content),3,
        binascii.crc32(content),0x5a210000,20,0x30,len(name),0o100644)+name)
    end_header=rar_header(struct.pack("<BHH",0x7b,0,7))
    rar.write_bytes(b"Rar!\x1a\x07\x00"+archive_header+file_header+content+end_header)
    app = start(installed,(rar,))
    window = wait_until(lambda: inspect(app,"ArchiwaMain"),"App opens RAR")
    try:
        listing = next(h for h in widget_list(window) if kind(h)=="SysListView32")
        wait_until(lambda: send(listing,0x1004,0,0)==1,"RAR listing")
        button(window,102)
        dismiss(app,6)
        unpacked=dest / "example" / "example.txt"
        wait_until(unpacked.is_file,"RAR extraction")
        assert unpacked.read_bytes()==content
        print("RAR LISTING AND EXTRACTION: PASS",flush=True)
    finally:
        stop(app,window)
    test_profiles(installed)
    test_queue(installed,dest)
    test_information_and_settings(installed,version,dest,archive)
