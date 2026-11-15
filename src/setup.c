#include "win.h"
#include "archiwa-build.h"

#include <shobjidl.h>

#define APP_PAYLOAD 101
static Handle window, title, description, location, checkbox, install, language, cancel;
static bool polish, uninstall;
static W folder[PATH_CAP], executable[PATH_CAP], menu[PATH_CAP], desktop[PATH_CAP];
static const W keyName[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Archiwa";
#define T(pl,en) (polish ? L##pl : L##en)

static bool shortcut(const W *path) {
    IShellLinkW *link=NULL;
    HRESULT result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void **)&link);
    if(FAILED(result)) return false;
    result=link->lpVtbl->SetPath(link,executable);
    if(SUCCEEDED(result)) result=link->lpVtbl->SetWorkingDirectory(link,folder);
    if(SUCCEEDED(result)) {
        IPersistFile *file=NULL;
        result=link->lpVtbl->QueryInterface(link,&IID_IPersistFile,(void **)&file);
        if(SUCCEEDED(result)) {
            result=file->lpVtbl->Save(file,path,TRUE);
            file->lpVtbl->Release(file);
        }
    }
    link->lpVtbl->Release(link);
    return SUCCEEDED(result);
}
static bool locations(void) {
    W base[PATH_CAP];
    if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_PROGRAMS,NULL,0,base))) join(menu,PATH_CAP,base,L"Archiwa.lnk");
    if(SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_DESKTOPDIRECTORY,NULL,0,base))) join(desktop,PATH_CAP,base,L"Archiwa.lnk");
    return user_path(folder,L"Archiwa") && join(executable,PATH_CAP,folder,L"Archiwa.exe");
}
static bool put_file(const W *target,const unsigned char *bytes,size_t size) {
    W pending[PATH_CAP], suffix[24];
    number(suffix,GetCurrentProcessId());
    if(size>0xffffffffu || !copy(pending,ARRAY(pending),target) || !append(pending,ARRAY(pending),L".") ||
       !append(pending,ARRAY(pending),suffix)) return false;
    Handle file=CreateFileW(pending,WRITE_ACCESS,0,0,CREATE_NEW,NORMAL_FILE,0);
    if(file==BAD_HANDLE) return false;
    bool ok=write_all(file,bytes,(DWORD)size);
    CloseHandle(file);
    if(ok) ok=MoveFileExW(pending,target,1|8)!=0;
    if(!ok) DeleteFileW(pending);
    return ok;
}
static bool put_installer(const W *target) {
    W source[PATH_CAP], pending[PATH_CAP], suffix[24];
    DWORD size=GetModuleFileNameW(NULL,source,ARRAY(source));
    if(!size || size>=ARRAY(source)) return false;
    if(equal(source,target)) return true;
    number(suffix,GetCurrentProcessId());
    if(!copy(pending,ARRAY(pending),target) || !append(pending,ARRAY(pending),L".") ||
       !append(pending,ARRAY(pending),suffix) || !CopyFileW(source,pending,1)) return false;
    bool ok=MoveFileExW(pending,target,1|8)!=0;
    if(!ok) DeleteFileW(pending);
    return ok;
}
static bool reg_text(HKEY key,const W *name,const W *value) {
    return RegSetValueExW(key,name,0,REG_SZ,(const BYTE *)value,(DWORD)((length(value)+1)*sizeof(W)))==0;
}
static bool register_install(const W *uninstaller) {
    W command[PATH_CAP], icon[PATH_CAP];
    if(!copy(command,ARRAY(command),L"\"") || !append(command,ARRAY(command),uninstaller) ||
       !append(command,ARRAY(command),L"\" --uninstall") || !copy(icon,ARRAY(icon),executable)) return false;
    HKEY key=NULL;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,keyName,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)!=0) return false;
    DWORD enabled=1;
    bool ok=reg_text(key,L"DisplayName",L"Archiwa") && reg_text(key,L"DisplayVersion",ARCHIWA_VERSION_WIDE) &&
        reg_text(key,L"InstallLocation",folder) && reg_text(key,L"DisplayIcon",icon) &&
        reg_text(key,L"UninstallString",command) &&
        RegSetValueExW(key,L"NoModify",0,4,(const BYTE *)&enabled,sizeof(enabled))==0 &&
        RegSetValueExW(key,L"NoRepair",0,4,(const BYTE *)&enabled,sizeof(enabled))==0;
    RegCloseKey(key); return ok;
}
static bool install_payload(void) {
    DWORD attrs=GetFileAttributesW(folder);
    if(attrs==INVALID_ATTRS) {
        if(!CreateDirectoryW(folder,0)) return false;
    } else if(!(attrs&DIR) || (attrs&REPARSE)) return false;
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(APP_PAYLOAD),RT_RCDATA);
    HGLOBAL image=resource ? LoadResource(instance,resource) : NULL;
    const unsigned char *payload=image ? LockResource(image) : NULL;
    DWORD size=resource ? SizeofResource(instance,resource) : 0;
    if(!payload || !size) return false;
    W maintenance[PATH_CAP];
    return join(maintenance,ARRAY(maintenance),folder,L"Archiwa-Maintenance.exe") &&
        put_file(executable,payload,size) && put_installer(maintenance);
}
static bool install_app(bool *integrationComplete) {
    if(!install_payload()) return false;
    W uninstaller[PATH_CAP];
    bool startShortcut=*menu && shortcut(menu);
    bool desktopShortcut=SendMessageW(checkbox,CHECK_GET,0,0)!=BST_CHECKED || (*desktop && shortcut(desktop));
    bool registered=join(uninstaller,ARRAY(uninstaller),folder,L"Archiwa-Maintenance.exe") && register_install(uninstaller);
    *integrationComplete=startShortcut && desktopShortcut && registered;
    return true;
}
static bool remove_file(const W *path) {
    return !*path || !exists(path) || DeleteFileW(path)!=0;
}
static bool remove_payload(void) {
    DWORD attrs=GetFileAttributesW(folder);
    return attrs==INVALID_ATTRS || ((attrs&DIR) && !(attrs&REPARSE) && remove_file(executable));
}
static bool uninstall_app(void) {
    if(!remove_payload() || !remove_file(menu) || !remove_file(desktop)) return false;
    int result=RegDeleteTreeW(HKEY_CURRENT_USER,keyName);
    return result==0 || result==ERROR_FILE_NOT_FOUND;
}
static void cleanup_after_exit(void) {
    W system[PATH_CAP], powershell[PATH_CAP], args[768], pid[24];
    unsigned size=GetSystemDirectoryW(system,ARRAY(system));
    if(!size || size>=ARRAY(system) || !join(powershell,ARRAY(powershell),system,
        L"WindowsPowerShell\\v1.0\\powershell.exe")) return;
    number(pid,GetCurrentProcessId());
    copy(args,ARRAY(args),L"-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Wait-Process -ErrorAction SilentlyContinue -Id ");
    append(args,ARRAY(args),pid);
    append(args,ARRAY(args),L"; $folder=Join-Path $env:LOCALAPPDATA 'Archiwa'; "
        L"Remove-Item -ErrorAction SilentlyContinue -LiteralPath (Join-Path $folder 'Archiwa-Maintenance.exe'); "
        L"if ((Test-Path -LiteralPath $folder) -and !(Get-ChildItem -LiteralPath $folder -Force)) "
        L"{ [IO.Directory]::Delete($folder) }\"");
    ShellExecuteW(window,L"open",powershell,args,0,0);
}
#ifdef ARCHIWA_SMOKE
static bool test_lifecycle(void) {
    W link[PATH_CAP], maintenance[PATH_CAP];
    if(!join(link,ARRAY(link),folder,L"Archiwa.lnk") ||
       !join(maintenance,ARRAY(maintenance),folder,L"Archiwa-Maintenance.exe")) return false;
    return install_payload() && shortcut(link) && exists(executable) && exists(link) &&
        install_payload() && remove_payload() && remove_file(link) && remove_file(maintenance) &&
        RemoveDirectoryW(folder);
}
#endif

static void translate(void) {
    caption(window,uninstall ? T("Odinstaluj Archiwa","Uninstall Archiwa") : T("Instalator Archiwa","Archiwa Setup"));
    caption(title,uninstall ? T("Usuń Archiwa","Uninstall Archiwa") : T("Zainstaluj Archiwa","Install Archiwa"));
    caption(description,uninstall ? T("Usuń aplikację z tego konta Windows.","Remove the app from this Windows account.") :
        T("Zainstaluj aplikację dla bieżącego użytkownika.","Install the app for the current Windows user."));
    caption(checkbox,T("Utwórz skrót na pulpicie","Create a desktop shortcut"));
    caption(install,uninstall ? T("Odinstaluj","Uninstall") : T("Zainstaluj","Install"));
    caption(cancel,T("Anuluj","Cancel"));
}
static LRESULT CALLBACK setup_proc(HWND hwnd,unsigned msg,WP w,LPARAM l) {
    if(msg==CTLCOLORSTATIC) {
        SetTextColor((Handle)w,COLOR(33,53,78)); SetBkColor((Handle)w,COLOR(244,247,251)); return (intptr_t)background;
    }
    if(msg==COMMAND) {
        if(LOW(w)==2) { DestroyWindow(hwnd); return 0; }
        if(LOW(w)==3 && HIGH(w)==1) { polish=SendMessageW(language,CB_GET,0,0)==0; translate(); return 0; }
        if(LOW(w)==1) {
            if(!uninstall && !LoadLibraryExW(L"archiveint.dll",0,0x800)) {
                MessageBoxW(hwnd,T("Ta wersja aplikacji wymaga aktualnego Windows 11.",
                    "This version of the app requires an up-to-date Windows 11."),L"Archiwa",OK_ERROR);
                return 0;
            }
            bool integrationComplete=true;
            bool ok=uninstall ? uninstall_app() : install_app(&integrationComplete);
            if(!ok) {
                MessageBoxW(hwnd,T("Nie udało się zakończyć operacji. Zamknij Archiwa, jeśli jest uruchomione, i spróbuj ponownie.",
                    "Could not complete the operation. Close Archiwa if it is running and try again."),L"Archiwa",OK_ERROR);
                return 0;
            }
            if(uninstall) {
                MessageBoxW(hwnd,T("Aplikacja została usunięta.","The app has been removed."),L"Archiwa",OK_INFO);
                cleanup_after_exit();
            }
            else {
                const W *message=integrationComplete ?
                    T("Zainstalowano Archiwa. Otworzyć aplikację?","Archiwa is installed. Open the app?") :
                    T("Pliki aplikacji są zainstalowane. Windows nie zezwolił na dodanie wszystkich skrótów lub wpisu w Ustawieniach. Otworzyć aplikację?",
                      "The app files are installed. Windows did not allow all shortcuts or the Installed apps entry. Open the app?");
                if(MessageBoxW(hwnd,message,L"Archiwa",YESNO|OK_INFO)==YES) ShellExecuteW(hwnd,L"open",executable,0,folder,NORM);
            }
            DestroyWindow(hwnd); return 0;
        }
    }
    if(msg==DESTROY_MSG) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE previous,PWSTR command,int show) {
    (void)module; (void)previous; (void)command; (void)show;
    ui_start();
    int count=0; W **args=CommandLineToArgvW(GetCommandLineW(),&count);
    uninstall=args && count>1 && equal(args[1],L"--uninstall");
#ifdef ARCHIWA_SMOKE
    if(args && count==2 && equal(args[1],L"--smoke-lifecycle")) {
        LocalFree(args);
        bool ok=user_path(folder,L"Archiwa") && !exists(folder) &&
            join(executable,PATH_CAP,folder,L"Archiwa.exe") && test_lifecycle();
        CoUninitialize();
        return ok ? 0 : 1;
    }
#endif
    if(args) LocalFree(args);
    polish=GetUserDefaultUILanguage()%1024==0x15;
    if(!locations()) { MessageBoxW(0,T("Nie znaleziono folderu użytkownika.","Could not find the user folder."),L"Archiwa",OK_ERROR); return 1; }
    WindowClass cls={sizeof(cls),0,setup_proc,0,0,instance,LoadIconW(instance,MAKEINTRESOURCEW(1)),
        LoadCursorW(0,(const W *)32512),background,0,L"ArchiwaSetup",0};
    RegisterClassExW(&cls);
    window=CreateWindowExW(APPWINDOW|CONTROLPARENT,L"ArchiwaSetup",L"Archiwa",
        FIXED_WINDOW|CLIPCHILDREN,0,0,570,304,0,0,instance,0);
    if(!window) return 1;
    title=control(window,L"STATIC",L"",0,0,0);
    SendMessageW(title,SETFONT,(WP)heading,1);
    description=control(window,L"STATIC",L"",0,0,0);
    location=control(window,L"STATIC",folder,0x4000,0,0);
    checkbox=control(window,L"BUTTON",L"",TABSTOP|3,0,4);
    install=control(window,L"BUTTON",L"",TABSTOP|1,0,1);
    cancel=control(window,L"BUTTON",L"",TABSTOP,0,2);
    language=control(window,L"COMBOBOX",L"",TABSTOP|VSCROLL|3,0,3);
    SendMessageW(language,CB_ADD,0,P(L"Polski")); SendMessageW(language,CB_ADD,0,P(L"English"));
    SendMessageW(language,CB_SELECT,polish ? 0 : 1,0);
    move(title,24,23,378,44); move(language,424,32,106,104);
    move(description,25,83,510,29); move(location,25,116,507,37);
    move(checkbox,25,155,510,30); ShowWindow(checkbox,uninstall ? 0 : SHOW);
    move(install,258,203,132,37); move(cancel,400,203,130,37);
    translate(); center_window(window,0); ShowWindow(window,SHOW); UpdateWindow(window);
    int result=loop(window); CoUninitialize(); return result;
}
