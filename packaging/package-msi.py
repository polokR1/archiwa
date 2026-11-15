import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import tempfile
import uuid


MSI = c.WinDLL("msi")
HANDLE = w.UINT
P_HANDLE = c.POINTER(HANDLE)


def api(name, result, *arguments):
    function = getattr(MSI, name)
    function.restype = result
    function.argtypes = arguments
    return function


open_db = api("MsiOpenDatabaseW", w.UINT, w.LPCWSTR, c.c_void_p, P_HANDLE)
open_view = api("MsiDatabaseOpenViewW", w.UINT, HANDLE, w.LPCWSTR, P_HANDLE)
execute = api("MsiViewExecute", w.UINT, HANDLE, HANDLE)
create_record = api("MsiCreateRecord", HANDLE, w.UINT)
set_string = api("MsiRecordSetStringW", w.UINT, HANDLE, w.UINT, w.LPCWSTR)
set_int = api("MsiRecordSetInteger", w.UINT, HANDLE, w.UINT, c.c_int)
set_stream = api("MsiRecordSetStreamW", w.UINT, HANDLE, w.UINT, w.LPCWSTR)
close = api("MsiCloseHandle", w.UINT, HANDLE)
commit = api("MsiDatabaseCommit", w.UINT, HANDLE)
open_summary = api("MsiGetSummaryInformationW", w.UINT, HANDLE, w.LPCWSTR, w.UINT, P_HANDLE)
summary_set = api("MsiSummaryInfoSetPropertyW", w.UINT, HANDLE, w.UINT,
                  w.UINT, c.c_int, c.c_void_p, w.LPCWSTR)
summary_save = api("MsiSummaryInfoPersist", w.UINT, HANDLE)

UPGRADE_CODE = "{0388D71F-F51C-4EB0-BBB8-4DE9D21F3C84}"
COMPONENT_CODE = "{76C31D23-3142-4B8C-9A15-6F809900687C}"
MENU_COMPONENT_CODE = "{7DD5DA99-093F-467D-8ABB-279149CF788D}"
SHELL_COMPONENT_CODE = "{26E5F32B-7A9E-42E4-AA59-C35269B8EF07}"
ARCHIVE_TYPES = ("zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "zst", "cab", "iso")


def guid(value):
    return "{" + str(value).upper() + "}"


def check(code, operation):
    if code:
        raise OSError(code, operation)


class Database:
    def __init__(self, path):
        handle = HANDLE()
        check(open_db(str(path), 3, c.byref(handle)), "Creating MSI database")
        self.handle = handle

    def run(self, sql, values=()):
        view = HANDLE()
        check(open_view(self.handle, sql, c.byref(view)), sql)
        record = create_record(len(values)) if values else 0
        try:
            if values and not record:
                raise MemoryError("Could not allocate an MSI record")
            for index, value in enumerate(values, start=1):
                if isinstance(value, int):
                    check(set_int(record, index, value), sql)
                elif isinstance(value, Path):
                    check(set_stream(record, index, str(value)), sql)
                elif value is not None:
                    check(set_string(record, index, value), sql)
            check(execute(view, record), sql)
        finally:
            if record:
                close(record)
            close(view)

    def table(self, name, schema, keys, columns, rows):
        self.run(f"CREATE TABLE `{name}` ({schema} PRIMARY KEY {keys})")
        fields = ", ".join(f"`{column}`" for column in columns.split())
        placeholders = ", ".join("?" for _ in columns.split())
        for row in rows:
            self.run(f"INSERT INTO `{name}` ({fields}) VALUES ({placeholders})", row)


def cabinet(folder):
    path = folder / "app.cab"
    definition = folder / "app.ddf"
    definition.write_text(
        ".OPTION EXPLICIT\n"
        ".Set Cabinet=on\n"
        ".Set Compress=on\n"
        ".Set CompressionType=MSZIP\n"
        ".Set CabinetNameTemplate=app.cab\n"
        ".Set DiskDirectoryTemplate=.\n"
        ".Set InfFileName=app.inf\n"
        ".Set RptFileName=app.rpt\n"
        '"Archiwa.exe" ArchiwaBinary\n',
        encoding="ascii",
    )
    subprocess.run(["makecab.exe", "/V0", "/F", str(definition)], cwd=folder, check=True)
    return path


def registry_rows():
    def entry(identifier, subkey, name, value):
        return (identifier, 1, "Software\\Classes\\" + subkey, name, value, "Shell")

    app_key = r"Applications\Archiwa.exe"
    rows = [
        entry("ShellKey", app_key, "FriendlyAppName", "Archiwa"),
        entry("OpenCommand", app_key + r"\shell\open\command", None, '"[#ArchiwaBinary]" "%1"'),
    ]
    for suffix in ARCHIVE_TYPES:
        rows.append(entry("Type_" + suffix, app_key + r"\SupportedTypes", "." + suffix, ""))
        verb = rf"SystemFileAssociations\.{suffix}\shell\ArchiwaExtract"
        rows.extend((
            entry("ExtractVerb_" + suffix, verb, "MUIVerb", "@[#ArchiwaBinary],-201"),
            entry("ExtractIcon_" + suffix, verb, "Icon", "[#ArchiwaBinary],0"),
            entry("ExtractCmd_" + suffix, verb + r"\command", None, '"[#ArchiwaBinary]" --extract-here "%1"'),
        ))
    verb = r"Directory\shell\ArchiwaProfile"
    rows.extend((
        entry("ProfileVerb", verb, "MUIVerb", "@[#ArchiwaBinary],-202"),
        entry("ProfileIcon", verb, "Icon", "[#ArchiwaBinary],0"),
        entry("ProfileCommand", verb + r"\command", None, '"[#ArchiwaBinary]" --profile "%1"'),
    ))
    return rows


def package(database, exe, payload, version):
    product = guid(uuid.uuid5(uuid.UUID(UPGRADE_CODE), version))
    table = database.table
    table("Property", "`Property` CHAR(72) NOT NULL, `Value` LONGCHAR NOT NULL", "`Property`",
          "Property Value", (
              ("ProductName", "Archiwa"), ("Manufacturer", "PoloQP"),
              ("ProductCode", product), ("UpgradeCode", UPGRADE_CODE),
              ("ProductVersion", version), ("ProductLanguage", "1033"),
              ("SecureCustomProperties", "ARCHIWA_OLDER;ARCHIWA_NEWER;INSTALLFOLDER"),
              ("ARPNOMODIFY", "1"), ("ARPPRODUCTICON", "ArchiwaIcon"),
          ))
    table("Directory", "`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE",
          "`Directory`", "Directory Directory_Parent DefaultDir", (
              ("TARGETDIR", None, "SourceDir"),
              ("LocalAppDataFolder", "TARGETDIR", "."),
              ("AppPrograms", "LocalAppDataFolder", "Programs"),
              ("INSTALLFOLDER", "AppPrograms", "Archiwa"),
              ("ProgramMenuFolder", "TARGETDIR", "."),
              ("MenuFolder", "ProgramMenuFolder", "Archiwa"),
          ))
    table("Component", "`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, "
          "`Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72)", "`Component`",
          "Component ComponentId Directory_ Attributes Condition KeyPath", (
              ("Main", COMPONENT_CODE, "INSTALLFOLDER", 256, None, "ArchiwaBinary"),
              ("Menu", MENU_COMPONENT_CODE, "MenuFolder", 260, None, "MenuKey"),
              ("Shell", SHELL_COMPONENT_CODE, "INSTALLFOLDER", 260, None, "ShellKey"),
          ))
    table("Feature", "`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, "
          "`Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, "
          "`Directory_` CHAR(72), `Attributes` SHORT NOT NULL", "`Feature`",
          "Feature Feature_Parent Title Description Display Level Directory_ Attributes",
          (("Archiwa", None, "Archiwa", "Offline archive manager", 1, 1, "INSTALLFOLDER", 0),))
    table("FeatureComponents", "`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL",
          "`Feature_`, `Component_`", "Feature_ Component_",
          tuple(("Archiwa", name) for name in ("Main", "Menu", "Shell")))
    table("File", "`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, "
          "`FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` SHORT NOT NULL",
          "`File`", "File Component_ FileName FileSize Version Language Attributes Sequence",
          (("ArchiwaBinary", "Main", "Archiwa.exe", exe.stat().st_size, version + ".0", "1033", 16384, 1),))
    table("Media", "`DiskId` SHORT NOT NULL, `LastSequence` SHORT NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, "
          "`Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72)", "`DiskId`",
          "DiskId LastSequence DiskPrompt Cabinet VolumeLabel Source", ((1, 1, None, "#app.cab", None, None),))
    table("Registry", "`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, "
          "`Name` CHAR(255) LOCALIZABLE, `Value` LONGCHAR LOCALIZABLE, `Component_` CHAR(72) NOT NULL", "`Registry`",
          "Registry Root Key Name Value Component_",
          [("MenuKey",1,r"Software\Archiwa\Installer","Installed","#1","Menu"), *registry_rows()])
    table("Shortcut", "`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL LOCALIZABLE, "
          "`Component_` CHAR(72) NOT NULL, `Target` CHAR(72) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255) LOCALIZABLE, "
          "`Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72)",
          "`Shortcut`", "Shortcut Directory_ Name Component_ Target Arguments Description Hotkey Icon_ IconIndex ShowCmd WkDir",
          (("MenuShortcut","MenuFolder","Archiwa","Menu","[#ArchiwaBinary]",None,"Archiwa",None,"ArchiwaIcon",0,1,"INSTALLFOLDER"),))
    table("Icon", "`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL", "`Name`", "Name Data",
          (("ArchiwaIcon", Path(__file__).resolve().parent / "assets" / "Archiwa.ico"),))
    table("RemoveFile", "`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) LOCALIZABLE, "
          "`DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL", "`FileKey`",
          "FileKey Component_ FileName DirProperty InstallMode", (
              ("EmptyMenu","Menu",None,"MenuFolder",2),
              ("EmptyApp","Main",None,"INSTALLFOLDER",2),
          ))
    table("Upgrade", "`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), "
          "`Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL",
          "`UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes`",
          "UpgradeCode VersionMin VersionMax Language Attributes Remove ActionProperty", (
              (UPGRADE_CODE, None, version, None, 0, None, "ARCHIWA_OLDER"),
              (UPGRADE_CODE, version, None, None, 2, None, "ARCHIWA_NEWER"),
          ))
    table("LaunchCondition", "`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL LOCALIZABLE",
          "`Condition`", "Condition Description", (
              ("Installed OR (VersionNT64 AND WindowsBuild >= 26100)", "Archiwa requires Windows 11 24H2 or later (x64)."),
              ("NOT ARCHIWA_NEWER", "A newer version of Archiwa is installed."),
          ))
    sequence = (
        ("FindRelatedProducts","NOT Installed",25), ("LaunchConditions",None,100),
        ("CostInitialize",None,800), ("FileCost",None,900), ("CostFinalize",None,1000),
        ("InstallValidate",None,1400), ("InstallInitialize",None,1500),
        ("RemoveExistingProducts",None,1520), ("ProcessComponents",None,1600),
        ("UnpublishFeatures",None,1800), ("RemoveRegistryValues",None,2600),
        ("RemoveShortcuts",None,3200), ("RemoveFiles",None,3500),
        ("InstallFiles",None,4000), ("CreateShortcuts",None,4500),
        ("WriteRegistryValues",None,5000), ("RegisterUser",None,6000),
        ("RegisterProduct",None,6100), ("PublishFeatures",None,6300),
        ("PublishProduct",None,6400), ("InstallFinalize",None,6600),
    )
    table("InstallExecuteSequence", "`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT",
          "`Action`", "Action Condition Sequence", sequence)
    database.run("INSERT INTO `_Streams` (`Name`, `Data`) VALUES (?, ?)", ("app.cab", payload))
    summary = HANDLE()
    check(open_summary(database.handle, None, 15, c.byref(summary)), "Opening package summary")
    try:
        fields = {1:1252, 2:"Installation Database", 3:"Archiwa " + version, 4:"PoloQP",
                  7:"x64;1033", 9:guid(uuid.uuid4()), 14:405, 15:10, 19:2}
        for key, value in fields.items():
            if isinstance(value, int):
                check(summary_set(summary, key, 3 if key != 1 else 2, value, None, None), "Writing MSI summary")
            else:
                check(summary_set(summary, key, 30, 0, None, value), "Writing MSI summary")
        check(summary_save(summary), "Saving MSI summary")
    finally:
        close(summary)
    check(commit(database.handle), "Saving MSI database")
    print(f"Product code: {product}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("version")
    args = parser.parse_args()
    exe, output = args.executable.resolve(strict=True), args.output.resolve()
    with tempfile.TemporaryDirectory(prefix="archiwa-msi-", dir=output.parent) as temp:
        stage = Path(temp)
        (stage / "Archiwa.exe").write_bytes(exe.read_bytes())
        payload = cabinet(stage)
        built = stage / output.name
        database = Database(built)
        try:
            package(database, exe, payload, args.version)
        finally:
            close(database.handle)
        built.replace(output)


if __name__ == "__main__":
    main()
