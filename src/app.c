#include "profiles.h"
#include "archiwa-build.h"
#include <stdio.h>
#include <wchar.h>

enum { OPEN=101, EXTRACT, CHECK, SETTINGS, NEW_ARCHIVE, ADD_FILES, ADD_DIR, REMOVE,
       CREATE, FORMAT, PASSWORD, LANG, DEST, SAVE_PREFS, CANCEL, PICK_DEST, BACK,
       PROFILES, PROFILE_PICK, PROFILE_NAME, PROFILE_SOURCE, PROFILE_BROWSE_SOURCE,
       PROFILE_TYPES, PROFILE_EXCLUDE, PROFILE_DEST, PROFILE_BROWSE_DEST, PROFILE_PATTERN,
       PROFILE_SAVE, PROFILE_DELETE, PROFILE_PREVIEW, PROFILE_PACK,
       QUEUE, QUEUE_ADD, QUEUE_REMOVE, QUEUE_RUN, QUEUE_OPEN, QUEUE_FOLDER,
       HELP_PAGE, ABOUT_PAGE, CONTACT_PAGE, SUPPORT_COPY, SUPPORT_EMAIL, SUPPORT_SITE,
       START_VIEW, DEST_WITHOUT_PROMPT, RESET_PREFS, DETAIL_TEXT,
       DONE=0x8010, PROGRESS, FILE_PROGRESS, QUEUE_ITEM, QUEUE_RESULT };
enum { MODE_READ, MODE_CREATE, MODE_SETTINGS, MODE_PROFILES, MODE_QUEUE, MODE_HELP, MODE_ABOUT, MODE_CONTACT };
typedef enum { QUEUED, EXTRACTED, FAILED } QueueState;
typedef struct { W *path; QueueState state; Error error; } QueueEntry;
typedef struct {
    bool polish, openFolder;
    int format, level;
    W destination[PATH_CAP];
    int startView, defaultWithoutPrompt;
} Preferences;
typedef struct {
    Handle window, appIcon, title, subtitle, readTab, writeTab, settings, status, progress, cancel;
    Handle help, about, contact, detailText, supportCopy, supportEmail, supportSite;
    Handle open, extract, check, path, entries, addFiles, addDir, remove, sources;
    Handle formatLabel, format, levelLabel, level, passLabel, password, create;
    Handle langLabel, lang, defaultLabel, defaultFormat, defaultLevelLabel, defaultLevel,
        destLabel, dest, pickDest, openFolder, save, back, startLabel, startView, skipPrompt, reset;
    Handle profileTab, profilePick, profileName, profileSource, profileSourceBrowse, profileTypes,
        profileExclude, profileDest, profileDestBrowse, profilePattern, profileSave, profileDelete,
        profilePreview, profilePack, profileList, profileOutput;
    Handle profilePickLabel, profileNameLabel, profileSourceLabel, profileTypesLabel,
        profileExcludeLabel, profileDestLabel, profilePatternLabel;
    Handle queueTab, queueAdd, queueRemove, queueRun, queueOpen, queueFolder, queueList;
    int mode, previousMode, kind, focusIndex;
    bool enabled, busy, profileReady, queueHere;
    volatile LONG canceled;
    unsigned lastProgress;
    int lastPercent;
    ULONGLONG lastProgressTick;
    W archive[PATH_CAP], target[PATH_CAP], passwordText[512], statusText[384];
    W **inputs;
    unsigned count, capacity;
    Entries entriesData, incoming;
    Entries preview;
    Profiles profiles;
    Profile activeProfile;
    W profileSourceOverride[PATH_CAP], profileTarget[PATH_CAP];
    QueueEntry *queue;
    unsigned queueCount, queueCapacity, queueCurrent;
    W queueDestination[PATH_CAP];
    bool *selected;
    Format workFormat;
    unsigned workLevel;
    Operation operation;
    Preferences prefs;
} App;
static App app;
static const W *formats[]={L"ZIP",L"7z",L"TAR.GZ"};
static const unsigned levels[]={1,6,9};
static const int start_modes[]={MODE_READ,MODE_CREATE,MODE_PROFILES,MODE_QUEUE};
#define T(pl,en) (app.prefs.polish ? L##pl : L##en)
#define ARCHIVE_FILTER L"*.zip;*.rar;*.7z;*.tar;*.gz;*.tgz;*.bz2;*.xz;*.zst;*.cab;*.iso;*.001"

static void profile_choices(int);
static void profile_field_reset(void);
static void queue_rows(void);
static void queue_summary(void);
static void start_work(int);
static void show_error(void);
static void open_archive(const W *);
static bool extraction_target(const W *,const W *,W *);

static const W *archive_filter(void) {
    return app.prefs.polish ? L"Archiwa\0" ARCHIVE_FILTER L"\0Wszystkie pliki\0*.*\0" :
        L"Archives\0" ARCHIVE_FILTER L"\0All files\0*.*\0";
}

static void status(const W *s) {
    copy(app.statusText,ARRAY(app.statusText),s); caption(app.status,s);
}
static void count_status(void) {
    W text[96], n[24];
    number(n,app.mode==MODE_CREATE ? app.count : app.entriesData.count);
    copy(text,ARRAY(text),app.mode==MODE_CREATE ? T("Źródła: ","Sources: ") : T("Elementy: ","Items: "));
    append(text,ARRAY(text),n); status(text);
}
static int index_of(Handle item,int max,int fallback) {
    int value=(int)SendMessageW(item,CB_GET,0,0);
    return value>=0 && value<max ? value : fallback;
}
static void combo(Handle c,const W *const *items,unsigned size,int chosen) {
    SendMessageW(c,CB_RESET,0,0);
    for(unsigned i=0;i<size;i++) SendMessageW(c,CB_ADD,0,P(items[i]));
    SendMessageW(c,CB_SELECT,chosen,0);
}
static Preferences default_preferences(void) {
    return (Preferences){.polish=GetUserDefaultUILanguage()%1024==0x15,.level=1};
}
static void preferences_load(void) {
    app.prefs=default_preferences();
    W path[PATH_CAP];
    if(!user_path(path,L"ArchiwaPreferences.dat")) return;
    Handle file=CreateFileW(path,READ_ACCESS,SHARE_READ|SHARE_DELETE,0,OPEN_EXISTING,NORMAL_FILE,0);
    if(file==BAD_HANDLE) return;
    Preferences incoming=app.prefs;
    DWORD read=0;
    bool ok=ReadFile(file,&incoming,sizeof(incoming),&read,0);
    CloseHandle(file);
    if(ok && (read==offsetof(Preferences,startView) || read==sizeof(incoming)) &&
       incoming.destination[ARRAY(incoming.destination)-1]==0 &&
       incoming.format>=0 && incoming.format<3 && incoming.level>=0 && incoming.level<3 &&
       incoming.startView>=0 && incoming.startView<(int)ARRAY(start_modes) &&
       (incoming.defaultWithoutPrompt==0 || incoming.defaultWithoutPrompt==1)) {
        app.prefs=incoming;
    }
}
static bool preferences_save(const Preferences *prefs) {
    W path[PATH_CAP],temp[PATH_CAP], suffix[24];
    number(suffix,GetCurrentProcessId());
    if(!user_path(path,L"ArchiwaPreferences.dat") || !copy(temp,ARRAY(temp),path) ||
       !append(temp,ARRAY(temp),L".") || !append(temp,ARRAY(temp),suffix)) return false;
    Handle file=CreateFileW(temp,WRITE_ACCESS,0,0,CREATE_NEW,NORMAL_FILE,0);
    if(file==BAD_HANDLE) return false;
    bool ok=write_all(file,prefs,sizeof(*prefs));
    CloseHandle(file);
    if(ok) ok=MoveFileExW(temp,path,1|8)!=0;
    if(!ok) DeleteFileW(temp);
    return ok;
}
static void set_columns(bool insert) {
    const W *labels[]={T("Nazwa w archiwum","Name in archive"), T("Rozmiar","Size"), T("Typ","Type")};
    const int widths[]={480,126,120};
    for(unsigned i=0;i<3;i++) {
        Column column={0};
        column.mask=6; column.cx=widths[i]; column.pszText=(W *)labels[i];
        SendMessageW(app.entries,insert ? LV_NEWCOL : LV_SETCOLUMN,i,P(&column));
    }
}
static void settings_form(const Preferences *prefs) {
    const W *quality[]={T("Szybka","Fast"),T("Standardowa","Normal"),T("Maksymalna","Maximum")};
    const W *languages[]={L"Polski",L"English"};
    const W *views[]={T("Zawartość archiwum","Archive contents"),T("Nowe archiwum","New archive"),
        T("Profile ZIP","ZIP profiles"),T("Kolejka","Extract queue")};
    combo(app.defaultFormat,formats,3,prefs->format);
    combo(app.defaultLevel,quality,3,prefs->level);
    combo(app.lang,languages,2,prefs->polish ? 0 : 1);
    combo(app.startView,views,ARRAY(views),prefs->startView);
    caption(app.dest,prefs->destination);
    SendMessageW(app.openFolder,CHECK_SET,prefs->openFolder,0);
    SendMessageW(app.skipPrompt,CHECK_SET,prefs->defaultWithoutPrompt,0);
}
static void translate(void) {
    caption(app.readTab,T("Zawartość archiwum","Archive contents"));
    caption(app.writeTab,T("Nowe archiwum","New archive"));
    caption(app.settings,T("Ustawienia","Settings"));
    caption(app.help,T("Pomoc","Help"));
    caption(app.about,T("Informacje","About"));
    caption(app.contact,T("Kontakt","Contact"));
    caption(app.supportCopy,T("Kopiuj dane do zgłoszenia","Copy support details"));
    caption(app.supportEmail,T("Napisz e-mail…","Compose email…"));
    caption(app.supportSite,T("Otwórz stronę wsparcia…","Open support site…"));
    caption(app.open,T("Otwórz archiwum…","Open archive…"));
    caption(app.extract,T("Rozpakuj…","Extract…"));
    caption(app.check,T("Sprawdź archiwum","Test archive"));
    caption(app.addFiles,T("Dodaj pliki…","Add files…"));
    caption(app.addDir,T("Dodaj folder…","Add folder…"));
    caption(app.remove,T("Usuń zaznaczenie","Remove selected"));
    caption(app.formatLabel,T("Format","Format"));
    caption(app.levelLabel,T("Kompresja","Compression"));
    caption(app.passLabel,T("Hasło do ZIP (opcjonalne)","ZIP password (optional)"));
    caption(app.create,T("Utwórz archiwum…","Create archive…"));
    caption(app.langLabel,T("Język interfejsu","Interface language"));
    caption(app.startLabel,T("Karta po uruchomieniu","Startup view"));
    caption(app.defaultLabel,T("Domyślny format","Default format"));
    caption(app.defaultLevelLabel,T("Domyślna kompresja","Default compression"));
    caption(app.destLabel,T("Domyślny folder rozpakowywania (opcjonalnie)","Default extraction folder (optional)"));
    caption(app.pickDest,T("Wybierz…","Browse…"));
    caption(app.openFolder,T("Otwórz folder po rozpakowaniu","Open folder after extraction"));
    caption(app.skipPrompt,T("Używaj domyślnego folderu bez pytania","Use the default folder without asking"));
    caption(app.reset,T("Przywróć domyślne","Restore defaults"));
    caption(app.save,T("Zapisz ustawienia","Save settings"));
    caption(app.back,T("Wróć","Back"));
    caption(app.cancel,T("Anuluj operację","Cancel operation"));
    caption(app.profileTab,T("Profile ZIP","ZIP profiles"));
    caption(app.profilePickLabel,T("Zapisany profil","Saved profile"));
    caption(app.profileNameLabel,T("Nazwa profilu","Profile name"));
    caption(app.profileSourceLabel,T("Folder źródłowy","Source folder"));
    caption(app.profileTypesLabel,T("Rozszerzenia, np. pdf; jpg","Extensions, e.g. pdf; jpg"));
    caption(app.profileExcludeLabel,T("Pomiń foldery, np. Robocze","Skip folders, e.g. Drafts"));
    caption(app.profileDestLabel,T("Folder docelowy ZIP","ZIP destination"));
    caption(app.profilePatternLabel,T("Wzór nazwy ZIP ({folder}, {date})","ZIP name pattern ({folder}, {date})"));
    caption(app.profileSourceBrowse,T("Wybierz…","Browse…"));
    caption(app.profileDestBrowse,T("Wybierz…","Browse…"));
    caption(app.profileSave,T("Zapisz profil","Save profile"));
    caption(app.profileDelete,T("Usuń profil","Delete profile"));
    caption(app.profilePreview,T("Pokaż pliki","Preview files"));
    caption(app.profilePack,T("Utwórz ZIP","Create ZIP"));
    caption(app.queueTab,T("Kolejka","Extract queue"));
    caption(app.queueAdd,T("Dodaj archiwa…","Add archives…"));
    caption(app.queueRemove,T("Usuń zaznaczone","Remove selected"));
    caption(app.queueRun,T("Rozpakuj kolejkę…","Extract queue…"));
    caption(app.queueOpen,T("Otwórz podgląd","Open preview"));
    caption(app.queueFolder,T("Otwórz folder wyników","Open output folder"));
    const W *quality[]={T("Szybka","Fast"),T("Standardowa","Normal"),T("Maksymalna","Maximum")};
    combo(app.format,formats,3,index_of(app.format,3,app.prefs.format));
    combo(app.level,quality,3,index_of(app.level,3,app.prefs.level));
    settings_form(&app.prefs);
    set_columns(false);
    Column column={0};
    column.mask=LVCF_TEXT;
    column.pszText=(W *)T("Archiwum","Archive");
    SendMessageW(app.queueList,LV_SETCOLUMN,0,P(&column));
    column.pszText=(W *)T("Wynik","Result");
    SendMessageW(app.queueList,LV_SETCOLUMN,1,P(&column));
    profile_choices(index_of(app.profilePick,(int)app.profiles.count+1,0));
    queue_rows();
}
static void rows(void) {
    SendMessageW(app.entries,0xb,0,0);
    SendMessageW(app.entries,LV_DELETEALL,0,0);
    for(unsigned i=0;i<app.entriesData.count;i++) {
        Entry *entry=&app.entriesData.items[i];
        ListItem item={0};
        item.mask=1; item.iItem=i; item.pszText=entry->name;
        if(SendMessageW(app.entries,LV_INSERT,0,P(&item))<0) break;
        W n[36]=L"";
        if(!entry->folder) { number(n,entry->size); append(n,ARRAY(n),L" B"); }
        item.iSubItem=1; item.pszText=n; SendMessageW(app.entries,LV_SETTEXT,i,P(&item));
        item.iSubItem=2;
        item.pszText=(W *)(entry->encrypted ? T("Z hasłem","Encrypted") :
            entry->folder ? T("Folder","Folder") : T("Plik","File"));
        SendMessageW(app.entries,LV_SETTEXT,i,P(&item));
    }
    SendMessageW(app.entries,0xb,1,0); InvalidateRect(app.entries,0,1);
}
static void visibility(const Handle *list,unsigned count,bool visible) {
    for(unsigned i=0;i<count;i++) ShowWindow(list[i],visible ? SHOW : 0);
}
static void show_details(int view) {
    bool contact=view==MODE_CONTACT, about=view==MODE_ABOUT;
    bool details=contact || about || view==MODE_HELP;
    ShowWindow(app.detailText,details ? SHOW : 0);
    ShowWindow(app.supportCopy,contact || about ? SHOW : 0);
    ShowWindow(app.supportEmail,contact && *ARCHIWA_SUPPORT_EMAIL ? SHOW : 0);
    ShowWindow(app.supportSite,contact && *ARCHIWA_SUPPORT_URL ? SHOW : 0);
    if(!details) return;

    W body[2048];
    if(view==MODE_HELP) {
        caption(app.detailText,T(
            "Otwieranie i rozpakowywanie\r\n"
            "Przeciągnij archiwum do okna lub użyj przycisku Otwórz archiwum. Zaznacz pliki do wypakowania albo wybierz Rozpakuj, aby wypakować całość do osobnego folderu.\r\n\r\n"
            "Tworzenie archiwum\r\n"
            "Na karcie Nowe archiwum dodaj pliki lub foldery. Wybierz ZIP, 7z lub TAR.GZ i poziom kompresji. Dla ZIP możesz ustawić hasło.\r\n\r\n"
            "Profile ZIP i kolejka\r\n"
            "Zapisz reguły pakowania w Profilach ZIP i sprawdź podgląd przed utworzeniem paczki. Przeciągnij kilka archiwów, aby dodać je do kolejki. Dla pliku wymagającego hasła wybierz Otwórz podgląd i rozpakuj go osobno.\r\n\r\n"
            "Ustawienia\r\n"
            "Wybierz język, kartę startową oraz domyślny folder rozpakowywania. Zatwierdź przyciskiem Zapisz ustawienia.",
            "Open and extract\r\n"
            "Drop an archive into the window or choose Open archive. Select the files to extract or choose Extract to unpack the entire archive to a separate folder.\r\n\r\n"
            "Create an archive\r\n"
            "Add files or folders on the New archive tab. Choose ZIP, 7z or TAR.GZ and a compression level. ZIP supports a password.\r\n\r\n"
            "ZIP profiles and the extraction queue\r\n"
            "Save packing rules in ZIP profiles and review the file list before creating the archive. Drop several archives to queue them for extraction. When an archive needs a password, use Open preview and extract it separately.\r\n\r\n"
            "Settings\r\n"
            "Choose your language, startup view and default extraction folder. Select Save settings to apply the changes."));
    } else if(about) {
        swprintf_s(body,ARRAY(body),T("Archiwa %ls (x64)\r\nPoloQP\r\n\r\nProgram do otwierania i tworzenia archiwów na Windows 11. Pakowanie, rozpakowywanie i profile ZIP działają offline.",
            "Archiwa %ls (x64)\r\nPoloQP\r\n\r\nArchive manager for Windows 11. Creating and extracting archives and using ZIP profiles work offline."),
            ARCHIWA_VERSION_WIDE);
        caption(app.detailText,body);
    } else {
        copy(body,ARRAY(body),T(
            "Opisując problem, podaj wykonywaną czynność i komunikat programu. Przycisk poniżej kopiuje wersję aplikacji i język interfejsu.",
            "When reporting an issue, include the action you took and the message shown. The button below copies the app version and interface language."));
        if(*ARCHIWA_SUPPORT_EMAIL) {
            append(body,ARRAY(body),L"\r\n\r\n");
            append(body,ARRAY(body),ARCHIWA_SUPPORT_EMAIL);
        }
        if(*ARCHIWA_SUPPORT_URL) {
            append(body,ARRAY(body),L"\r\n\r\n");
            append(body,ARRAY(body),ARCHIWA_SUPPORT_URL);
        }
        if(!*ARCHIWA_SUPPORT_EMAIL && !*ARCHIWA_SUPPORT_URL) {
            append(body,ARRAY(body),T("\r\n\r\nW tym wydaniu nie skonfigurowano adresu wsparcia.",
                "\r\n\r\nThis build has no support address configured."));
        }
        caption(app.detailText,body);
    }
    SendMessageW(app.detailText,EM_SETSEL,0,0);
    SendMessageW(app.detailText,EM_SCROLLCARET,0,0);
}
static void layout(void) {
    Rect r;
    GetClientRect(app.window,&r);
    int width=r.right, height=r.bottom, interior=width-48, bottom=height-62;
    move(app.appIcon,24,23,32,32); move(app.title,67,16,152,42); move(app.subtitle,25,64,interior,26);
    move(app.settings,width-146,24,122,34);
    move(app.about,width-258,24,104,34);
    move(app.contact,width-359,24,93,34);
    move(app.help,width-453,24,86,34);
    move(app.readTab,24,101,175,34); move(app.writeTab,207,101,156,34);
    move(app.profileTab,371,101,133,34); move(app.queueTab,512,101,141,34);
    move(app.open,24,150,172,34); move(app.extract,204,150,130,34); move(app.check,342,150,172,34);
    move(app.path,26,194,interior-4,24);
    move(app.entries,24,224,interior,bottom-235);
    SendMessageW(app.entries,LV_COLWIDTH,0,interior-240);
    move(app.addFiles,24,150,145,34); move(app.addDir,177,150,145,34); move(app.remove,330,150,165,34);
    move(app.sources,24,196,interior,bottom-322);
    int options=bottom-113;
    move(app.formatLabel,24,options,138,22); move(app.format,24,options+24,138,150);
    move(app.levelLabel,175,options,145,22); move(app.level,175,options+24,145,150);
    move(app.passLabel,334,options,interior-309,22); move(app.password,334,options+24,interior-310,29);
    move(app.create,width-222,options+66,198,34);
    move(app.langLabel,24,153,270,22); move(app.lang,24,177,180,150);
    move(app.startLabel,231,153,260,22); move(app.startView,231,177,205,150);
    move(app.defaultLabel,24,225,195,22); move(app.defaultFormat,24,250,180,145);
    move(app.defaultLevelLabel,231,225,220,22); move(app.defaultLevel,231,250,180,145);
    move(app.destLabel,24,303,interior,22); move(app.dest,24,330,interior-126,30);
    move(app.pickDest,width-142,330,118,30); move(app.openFolder,24,371,interior,29);
    move(app.skipPrompt,24,403,interior,29);
    move(app.back,24,451,104,34); move(app.reset,136,451,164,34);
    move(app.save,width-208,451,184,34);
    int left=interior/2-8, right=24+left+16;
    move(app.profilePickLabel,24,146,left,21); move(app.profilePick,24,168,left,178);
    move(app.profileNameLabel,right,146,left,21); move(app.profileName,right,168,left,28);
    move(app.profileSourceLabel,24,206,left,21); move(app.profileDestLabel,right,206,left,21);
    move(app.profileSource,24,228,left-100,28); move(app.profileSourceBrowse,24+left-93,228,93,28);
    move(app.profileDest,right,228,left-100,28); move(app.profileDestBrowse,right+left-93,228,93,28);
    move(app.profileTypesLabel,24,266,left,21); move(app.profileExcludeLabel,right,266,left,21);
    move(app.profileTypes,24,288,left,28); move(app.profileExclude,right,288,left,28);
    move(app.profilePatternLabel,24,326,interior,21); move(app.profilePattern,24,348,interior,28);
    move(app.profileSave,24,385,130,32); move(app.profileDelete,162,385,130,32);
    move(app.profilePreview,width-288,385,128,32); move(app.profilePack,width-152,385,128,32);
    move(app.profileOutput,24,425,interior,22);
    move(app.profileList,24,453,interior,bottom-459);
    move(app.queueAdd,24,150,140,34); move(app.queueRemove,172,150,143,34);
    move(app.queueOpen,323,150,139,34); move(app.queueRun,width-206,150,182,34);
    move(app.queueList,24,198,interior,bottom-244);
    SendMessageW(app.queueList,LV_COLWIDTH,0,interior-250);
    move(app.queueFolder,24,bottom-37,210,31);
    move(app.detailText,24,146,interior,bottom-202);
    move(app.supportCopy,24,bottom-44,220,34); move(app.supportEmail,252,bottom-44,147,34);
    move(app.supportSite,407,bottom-44,199,34);
    move(app.status,24,bottom+20,interior-(app.busy ? 168 : 0),24);
    move(app.cancel,width-180,bottom+15,156,34); move(app.progress,24,bottom+4,interior-166,12);
}
static void mode(int value) {
    const Handle readGroup[]={app.open,app.extract,app.check,app.path,app.entries};
    const Handle writeGroup[]={app.addFiles,app.addDir,app.remove,app.sources,app.formatLabel,app.format,
        app.levelLabel,app.level,app.passLabel,app.password,app.create};
    const Handle prefsGroup[]={app.langLabel,app.lang,app.defaultLabel,app.defaultFormat,app.defaultLevelLabel,
        app.defaultLevel,app.destLabel,app.dest,app.pickDest,app.openFolder,app.save,app.back,
        app.startLabel,app.startView,app.skipPrompt,app.reset};
    const Handle profileGroup[]={app.profilePickLabel,app.profilePick,app.profileNameLabel,app.profileName,
        app.profileSourceLabel,app.profileSource,app.profileSourceBrowse,app.profileTypesLabel,app.profileTypes,
        app.profileExcludeLabel,app.profileExclude,app.profileDestLabel,app.profileDest,app.profileDestBrowse,
        app.profilePatternLabel,app.profilePattern,app.profileSave,app.profileDelete,app.profilePreview,
        app.profilePack,app.profileList,app.profileOutput};
    const Handle queueGroup[]={app.queueAdd,app.queueRemove,app.queueRun,app.queueOpen,app.queueFolder,app.queueList};
    if(value==MODE_SETTINGS && app.mode!=MODE_SETTINGS) app.previousMode=app.mode;
    app.mode=value;
    visibility(readGroup,ARRAY(readGroup),value==MODE_READ);
    visibility(writeGroup,ARRAY(writeGroup),value==MODE_CREATE);
    visibility(prefsGroup,ARRAY(prefsGroup),value==MODE_SETTINGS);
    visibility(profileGroup,ARRAY(profileGroup),value==MODE_PROFILES);
    visibility(queueGroup,ARRAY(queueGroup),value==MODE_QUEUE);
    show_details(value);
    SendMessageW(app.readTab,SETFONT,(WP)(value==MODE_READ ? bold : font),1);
    SendMessageW(app.writeTab,SETFONT,(WP)(value==MODE_CREATE ? bold : font),1);
    SendMessageW(app.profileTab,SETFONT,(WP)(value==MODE_PROFILES ? bold : font),1);
    SendMessageW(app.queueTab,SETFONT,(WP)(value==MODE_QUEUE ? bold : font),1);
    caption(app.subtitle,value==MODE_READ ? T("Otwórz archiwum lub przeciągnij je do okna.","Open an archive or drop it into this window.") :
        value==MODE_CREATE ? T("Dodaj pliki i foldery lub przeciągnij je do okna.","Add files and folders or drop them into this window.") :
        value==MODE_PROFILES ? T("Zapisz reguły i sprawdź listę plików przed pakowaniem.","Save your rules and review the files before packing.") :
        value==MODE_QUEUE ? T("Przeciągnij archiwa. Każde trafi do osobnego folderu.","Drop archives to extract each into its own folder.") :
        value==MODE_HELP ? T("Pomoc","Help") :
        value==MODE_ABOUT ? T("Informacje o programie","About Archiwa") :
        value==MODE_CONTACT ? T("Kontakt z wydawcą","Contact the publisher") :
        T("Zmień język i domyślne opcje pracy.","Choose your language and defaults."));
    if(value==MODE_SETTINGS) status(T("Ustawienia są zapisywane po kliknięciu Zapisz.","Click Save settings to keep your changes."));
    else if(value==MODE_QUEUE) queue_summary();
    else if(value==MODE_PROFILES) {
        status(app.profileReady ? T("Podgląd jest gotowy.","Preview is ready.") :
            T("Puste pole rozszerzeń uwzględnia wszystkie pliki.","Leave extensions blank to include all files."));
    } else if(value>=MODE_HELP) status(L"");
    else count_status();
    layout();
}
static bool browse_folder(W *target,const W *title) {
    BrowseInfo info={0};
    info.hwndOwner=app.window; info.lpszTitle=title; info.ulFlags=0x41;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&info);
    if(!item) return false;
    bool ok=SHGetPathFromIDListW(item,target)!=0;
    CoTaskMemFree(item);
    return ok;
}
static bool pick_file(W *out,DWORD size,const W *filter,DWORD flags,bool save,const W *def,const W *initial) {
    OPENFILENAMEW of={0};
    of.lStructSize=sizeof(of); of.hwndOwner=app.window; of.lpstrFile=out; of.nMaxFile=size; of.lpstrFilter=filter; of.nFilterIndex=1;
    of.Flags=0x80000|0x800|0x8|flags; of.lpstrDefExt=def; of.lpstrInitialDir=initial;
    return (save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of))!=0;
}
static void input_add(const W *path) {
    for(unsigned i=0;i<app.count;i++) if(equal(app.inputs[i],path)) return;
    if(app.count==app.capacity) {
        unsigned capacity=app.capacity ? app.capacity*2 : 32;
        if(capacity>16384) return;
        void *next=resize(app.inputs,capacity*sizeof(W *));
        if(!next) return;
        app.inputs=next; app.capacity=capacity;
    }
    W *saved=duplicate(path);
    if(!saved) return;
    if(SendMessageW(app.sources,LB_ADD,0,P(saved))<0) { release(saved); return; }
    app.inputs[app.count++]=saved;
}
static void pick_sources(void) {
    W *out=memory(32768*sizeof(W));
    if(!out) return;
    if(pick_file(out,32768,T("Wszystkie pliki\0*.*\0","All files\0*.*\0"),0x1000|0x200,false,0,0)) {
        const W *p=out+length(out)+1;
        if(!*p) input_add(out);
        else for(;*p;p+=length(p)+1) {
            W source[PATH_CAP];
            if(join(source,ARRAY(source),out,p)) input_add(source);
        }
        count_status();
    }
    release(out);
}

static void list_value(Handle list,unsigned row,int column,const W *text) {
    ListItem item={0};
    item.iSubItem=column; item.pszText=(W *)text;
    SendMessageW(list,LV_SETTEXT,row,P(&item));
}

static void profile_choices(int chosen) {
    SendMessageW(app.profilePick,CB_RESET,0,0);
    SendMessageW(app.profilePick,CB_ADD,0,P(T("(Nowy profil)","(New profile)")));
    for(unsigned i=0;i<app.profiles.count;i++)
        SendMessageW(app.profilePick,CB_ADD,0,P(app.profiles.items[i].name));
    SendMessageW(app.profilePick,CB_SELECT,chosen,0);
    EnableWindow(app.profileDelete,chosen>0 && !app.busy);
}

static void invalidate_preview(void) {
    app.profileReady=false;
    entries_free(&app.preview);
    caption(app.profileOutput,L"");
    SendMessageW(app.profileList,LB_RESET,0,0);
    EnableWindow(app.profilePack,FALSE);
}

static void profile_field_reset(void) {
    int current=index_of(app.profilePick,(int)app.profiles.count+1,0);
    const Profile *profile=current ? &app.profiles.items[current-1] : 0;
    caption(app.profileName,profile ? profile->name : L"");
    caption(app.profileSource,*app.profileSourceOverride ? app.profileSourceOverride : profile ? profile->source : L"");
    caption(app.profileDest,profile ? profile->destination : L"");
    caption(app.profileTypes,profile ? profile->extensions : L"");
    caption(app.profileExclude,profile ? profile->excluded : L"");
    caption(app.profilePattern,profile ? profile->pattern : L"{folder}_{date}");
    EnableWindow(app.profileDelete,current>0);
    invalidate_preview();
}

static bool folder_field(Handle control,W *out) {
    W input[PATH_CAP];
    if(!GetWindowTextW(control,input,ARRAY(input))) return false;
    DWORD size=GetFullPathNameW(input,PATH_CAP,out,0);
    if(!size || size>=PATH_CAP || !directory(out)) return false;
    while(size>3 && out[size-1]==L'\\') out[--size]=0;
    return true;
}

static bool profile_from_form(Profile *profile,W *target) {
    *profile=(Profile){0};
    GetWindowTextW(app.profileName,profile->name,ARRAY(profile->name));
    GetWindowTextW(app.profileTypes,profile->extensions,ARRAY(profile->extensions));
    GetWindowTextW(app.profileExclude,profile->excluded,ARRAY(profile->excluded));
    GetWindowTextW(app.profilePattern,profile->pattern,ARRAY(profile->pattern));
    if(!folder_field(app.profileSource,profile->source) || !folder_field(app.profileDest,profile->destination)) {
        status(T("Wybierz istniejący folder źródłowy i docelowy.","Choose existing source and destination folders."));
        return false;
    }
    if(!profile_target(profile,target)) {
        status(T("Użyj poprawnej nazwy ZIP. Dostępne znaczniki to {folder} i {date}.",
                 "Use a valid ZIP name. Available placeholders are {folder} and {date}."));
        return false;
    }
    return true;
}

static void change_profile(bool remove) {
    int selected=index_of(app.profilePick,(int)app.profiles.count+1,0)-1;
    Profile profile;
    W target[PATH_CAP];
    if(remove) {
        if(selected<0 || MessageBoxW(app.window,T("Usunąć zapisany profil?","Delete the saved profile?"),
            app.profiles.items[selected].name,YESNO|OK_INFO)!=YES) return;
    } else {
        if(!profile_from_form(&profile,target)) return;
        if(!*profile.name || !profile.name[wcsspn(profile.name,L" \t")]) {
            status(T("Wpisz nazwę profilu.","Enter a profile name.")); return;
        }
    }
    ProfileSave result=profiles_update(&app.profiles,selected,remove ? 0 : &profile);
    if(result==PROFILE_CONFLICT) {
        if(profiles_load(&app.profiles)) { profile_choices(0); profile_field_reset(); }
        status(T("Profil zmienił się w innym oknie. Wybierz go ponownie.",
                 "The profile changed in another window. Select it again."));
        return;
    }
    if(result!=PROFILE_SAVED) {
        status(result==PROFILE_NAME_USED ? T("Taka nazwa profilu już istnieje.","That profile name already exists.") :
            T("Nie udało się zapisać profili.","Could not save profiles."));
        return;
    }
    int chosen=0;
    if(!remove) for(unsigned i=0;i<app.profiles.count;i++) {
        if(equal(app.profiles.items[i].name,profile.name)) { chosen=(int)i+1; break; }
    }
    app.profileSourceOverride[0]=0;
    profile_choices(chosen); profile_field_reset();
    status(remove ? T("Usunięto profil.","Profile deleted.") : T("Zapisano profil.","Profile saved."));
}

static void preview_profile(void) {
    invalidate_preview();
    if(profile_from_form(&app.activeProfile,app.profileTarget)) start_work(PROFILE_PREVIEW);
}

static void show_preview(void) {
    W statusLine[128],digits[24];
    SendMessageW(app.profileList,WM_SETREDRAW,FALSE,0);
    for(unsigned i=0;i<app.preview.count;i++) {
        if(SendMessageW(app.profileList,LB_ADD,0,P(app.preview.items[i].name))<0) {
            SendMessageW(app.profileList,WM_SETREDRAW,TRUE,0);
            invalidate_preview();
            app.operation=(Operation){.code=NO_MEMORY}; show_error(); return;
        }
    }
    SendMessageW(app.profileList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(app.profileList,0,TRUE);
    caption(app.profileOutput,app.profileTarget);
    number(digits,app.preview.count);
    copy(statusLine,ARRAY(statusLine),T("Pliki do spakowania: ","Files to pack: "));
    append(statusLine,ARRAY(statusLine),digits); status(statusLine);
    app.profileReady=true;
    EnableWindow(app.profilePack,TRUE);
}

static const W *error_message(Error code) {
    switch(code) {
    case UNAVAILABLE: return T("Ten Windows nie udostępnia zgodnego silnika archiwów. Użyj aktualnego Windows 11.",
        "This Windows version lacks a compatible archive engine. Use an up-to-date Windows 11.");
    case PASSWORD_ERROR: return T("Archiwum wymaga poprawnego hasła.","The archive requires the correct password.");
    case UNSUPPORTED_ENCRYPTION: return T("Silnik nie obsługuje szyfrowania tego archiwum.","The archive engine does not support this archive’s encryption.");
    case UNSAFE_NAME: return T("Archiwum zawiera niedozwoloną nazwę lub dowiązanie.","The archive contains an invalid path or a link.");
    case EXISTS: return T("Plik lub folder już istnieje. Wybierz nową nazwę.","The file or folder already exists. Choose a new name.");
    case DUPLICATE_INPUT: return T("Dwa źródła mają tę samą nazwę. Usuń jedno z nich.","Two inputs have the same name. Remove one of them.");
    case SOURCE_LINK: return T("Źródło zawiera dowiązanie lub plik dostępny wyłącznie w chmurze.","A source contains a link or a cloud-only file.");
    case IO_ERROR: return T("Nie można odczytać źródła lub zapisać wyniku w wybranym miejscu.","Could not read the source or write the result to the selected location.");
    case NO_MEMORY: return T("Za mało pamięci do wykonania operacji.","There is not enough memory to complete the operation.");
    case NO_MATCHES: return T("Żaden plik nie pasuje do reguł profilu.","No files match the profile rules.");
    case SOURCE_CHANGED: return T("Pliki zmieniły się od podglądu. Pokaż listę ponownie.","Files changed since the preview. Refresh the file list.");
    default: return T("Nie udało się przetworzyć archiwum. Plik może być uszkodzony lub mieć nieobsługiwany format.",
        "Could not process the archive. The file may be damaged or use an unsupported format.");
    }
}
static const W *queue_state(const QueueEntry *entry) {
    if(entry->state==QUEUED) return T("Czeka","Waiting");
    if(entry->state==EXTRACTED) return T("Rozpakowano","Extracted");
    switch(entry->error) {
    case PASSWORD_ERROR: return T("Wymaga hasła","Password required");
    case UNSUPPORTED_ENCRYPTION: return T("Nieobsługiwane szyfrowanie","Unsupported encryption");
    case UNSAFE_NAME: return T("Niedozwolona ścieżka","Invalid archive path");
    case EXISTS: return T("Wynik już istnieje","Output exists");
    case IO_ERROR: return T("Błąd odczytu lub zapisu","Read or write error");
    default: return T("Błąd archiwum","Archive error");
    }
}
static void queue_rows(void) {
    SendMessageW(app.queueList,WM_SETREDRAW,FALSE,0);
    SendMessageW(app.queueList,LV_DELETEALL,0,0);
    for(unsigned i=0;i<app.queueCount;i++) {
        ListItem item={0};
        item.mask=LVIF_TEXT; item.iItem=i; item.pszText=app.queue[i].path;
        if(SendMessageW(app.queueList,LV_INSERT,0,P(&item))<0) break;
        list_value(app.queueList,i,1,queue_state(&app.queue[i]));
    }
    SendMessageW(app.queueList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(app.queueList,0,TRUE);
}
static void queue_summary(void) {
    unsigned completed=0,failed=0;
    for(unsigned i=0;i<app.queueCount;i++) {
        completed+=app.queue[i].state==EXTRACTED;
        failed+=app.queue[i].state==FAILED;
    }
    W text[200];
    swprintf_s(text,ARRAY(text),T("Rozpakowano: %u   Błędy: %u   Czeka: %u","Extracted: %u   Errors: %u   Waiting: %u"),
        completed,failed,app.queueCount-completed-failed);
    status(text);
}
static bool queue_add(const W *path) {
    if(directory(path)) return true;
    for(unsigned i=0;i<app.queueCount;i++) if(equal(app.queue[i].path,path)) return true;
    if(app.queueCount==app.queueCapacity) {
        unsigned capacity=app.queueCapacity ? app.queueCapacity*2 : 32;
        if(capacity>4096) return false;
        void *next=resize(app.queue,capacity*sizeof(QueueEntry));
        if(!next) return false;
        app.queue=next; app.queueCapacity=capacity;
    }
    W *saved=duplicate(path);
    if(!saved) return false;
    app.queue[app.queueCount++]=(QueueEntry){.path=saved};
    return true;
}
static void queue_changed(bool complete) {
    queue_rows();
    if(complete) queue_summary();
    else status(T("Nie udało się dodać wszystkich archiwów. Limit kolejki to 4096 plików.",
                  "Could not add every archive. The queue supports up to 4096 files."));
}
static void pick_archives(void) {
    W *out=memory(32768*sizeof(W));
    if(!out) return;
    if(pick_file(out,32768,archive_filter(),OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT,false,0,0)) {
        const W *p=out+length(out)+1;
        bool ok=true;
        if(!*p) ok=queue_add(out);
        else for(;*p && ok;p+=length(p)+1) {
            W source[PATH_CAP];
            ok=join(source,ARRAY(source),out,p) && queue_add(source);
        }
        queue_changed(ok);
    }
    release(out);
}
static void drop_sources(Handle drop) {
    unsigned count=DragQueryFileW(drop,0xffffffffu,0,0);
    W path[PATH_CAP];
    if(!app.busy) {
        if(app.mode==MODE_READ && count>1) mode(MODE_QUEUE);
        bool complete=true;
        for(unsigned i=0;complete && i<count;i++) {
            if(DragQueryFileW(drop,i,0,0)>=ARRAY(path) || !DragQueryFileW(drop,i,path,ARRAY(path))) {
                complete=false; break;
            }
            if(app.mode==MODE_READ) { if(!directory(path)) open_archive(path); break; }
            if(app.mode==MODE_PROFILES) {
                if(directory(path)) caption(app.profileSource,path);
                break;
            }
            if(app.mode==MODE_CREATE) input_add(path);
            if(app.mode==MODE_QUEUE) complete=queue_add(path);
        }
        if(app.mode==MODE_CREATE) count_status();
        if(app.mode==MODE_QUEUE) queue_changed(complete);
    }
    DragFinish(drop);
}
static void show_error(void) {
    const W *message=error_message(app.operation.code);
    W text[1000];
    copy(text,ARRAY(text),message);
    if(*app.operation.detail) { append(text,ARRAY(text),L"\n\n"); append(text,ARRAY(text),app.operation.detail); }
    status(message); MessageBoxW(app.window,text,L"Archiwa",OK_ERROR);
}
static Handle keyWindow, keyField;
static bool keyResult, keyDone;
static W requestedPassword[512];
static LRESULT CALLBACK key_proc(HWND window,unsigned msg,WP w,LPARAM l) {
    if(msg==COMMAND && LOW(w)==1) {
        GetWindowTextW(keyField,requestedPassword,ARRAY(requestedPassword));
        keyResult=*requestedPassword!=0; DestroyWindow(window); return 0;
    }
    if(msg==CLOSE_MSG || (msg==COMMAND && LOW(w)==2)) { DestroyWindow(window); return 0; }
    if(msg==DESTROY_MSG) { keyDone=true; return 0; }
    return DefWindowProcW(window,msg,w,l);
}
static bool ask_password(void) {
    WindowClass cls={sizeof(cls),0,key_proc,0,0,instance,0,LoadCursorW(0,(const W *)32512),background,0,L"ArchiwaPassword",0};
    RegisterClassExW(&cls);
    keyResult=false; keyDone=false; requestedPassword[0]=0;
    keyWindow=CreateWindowExW(CONTROLPARENT,L"ArchiwaPassword",T("Hasło do archiwum","Archive password"),
        0x00c80000,0,0,432,169,app.window,0,instance,0);
    if(!keyWindow) return false;
    Handle prompt=control(keyWindow,L"STATIC",T("Wpisz hasło do archiwum.","Enter the archive password."),0,0,0);
    keyField=control(keyWindow,L"EDIT",L"",TABSTOP|0xa0,CLIENTEDGE,3);
    SendMessageW(keyField,0xc5,511,0);
    Handle ok=control(keyWindow,L"BUTTON",L"OK",TABSTOP|1,0,1);
    Handle cancel=control(keyWindow,L"BUTTON",T("Anuluj","Cancel"),TABSTOP,0,2);
    move(prompt,16,12,380,23); move(keyField,16,39,382,28); move(ok,192,84,98,29); move(cancel,300,84,98,29);
    EnableWindow(app.window,0); center_window(keyWindow,app.window); ShowWindow(keyWindow,SHOW); SetFocus(keyField);
    Message message;
    while(!keyDone && GetMessageW(&message,0,0,0)>0) {
        if(message.message==0x100 && message.wParam==0x1b) { DestroyWindow(keyWindow); continue; }
        if(message.message==0x100 && message.wParam==0x0d && message.hwnd==keyField) {
            SendMessageW(keyWindow,COMMAND,1,0); continue;
        }
        if(!IsDialogMessageW(keyWindow,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(app.window,1);
    if(keyResult) copy(app.passwordText,ARRAY(app.passwordText),requestedPassword);
    memset(requestedPassword,0,sizeof(requestedPassword));
    return keyResult;
}
static bool progress_callback(void *unused,unsigned count) {
    (void)unused;
    if(count!=app.lastProgress && (count<16 || count%16==0)) {
        app.lastProgress=count;
        PostMessageW(app.window,PROGRESS,count,0);
    }
    return true;
}
static void file_progress(void *unused,uint64_t done,uint64_t total) {
    (void)unused;
    int percent=total ? (done>=total ? 100 : (int)((double)done*100.0/(double)total)) : -1;
    if(percent==app.lastPercent) return;
    ULONGLONG now=GetTickCount64();
    if(app.lastPercent>=0 && now-app.lastProgressTick<50) return;
    app.lastPercent=percent;
    app.lastProgressTick=now;
    PostMessageW(app.window,FILE_PROGRESS,(WP)(INT_PTR)percent,0);
}
static void unpack_queue(void) {
    for(unsigned i=0;i<app.queueCount;i++) {
        const QueueEntry *item=&app.queue[i];
        if(item->state==EXTRACTED) continue;
        if(InterlockedCompareExchange(&app.canceled,0,0)) { app.operation.code=CANCELED; break; }
        W target[PATH_CAP], nearby[PATH_CAP];
        const W *root=app.queueDestination;
        if(app.queueHere && parent_w(nearby,PATH_CAP,item->path)) root=nearby;
        Operation work=app.operation;
        app.lastProgress=0;
        app.lastPercent=-2;
        PostMessageW(app.window,QUEUE_ITEM,i,0);
        if(*root && extraction_target(item->path,root,target))
            unpack_archive(item->path,L"",target,0,0,&work);
        else work.code=IO_ERROR;
        if(work.code==CANCELED) { app.operation.code=CANCELED; break; }
        PostMessageW(app.window,QUEUE_RESULT,i,work.code);
    }
}
static DWORD worker(void *unused) {
    (void)unused;
    const W *key=app.passwordText;
    switch(app.kind) {
    case OPEN: read_archive(app.target,key,&app.incoming,&app.operation); break;
    case EXTRACT: unpack_archive(app.archive,key,app.target,&app.entriesData,app.selected,&app.operation); break;
    case CHECK: test_archive(app.archive,key,&app.operation); break;
    case CREATE: make_archive(app.target,(const W *const *)app.inputs,app.count,app.workFormat,app.workLevel,key,&app.operation); break;
    case PROFILE_PREVIEW: scan_profile(&app.activeProfile,app.profileTarget,&app.preview,&app.operation); break;
    case PROFILE_PACK: make_profile_zip(app.profileTarget,app.activeProfile.source,&app.preview,app.workLevel,&app.operation); break;
    case QUEUE_RUN: unpack_queue(); break;
    }
    PostMessageW(app.window,DONE,0,0); return 0;
}
static void busy(bool active) {
    app.busy=active;
    const Handle actions[]={app.open,app.extract,app.check,app.addFiles,app.addDir,app.remove,app.create,
        app.format,app.level,app.password,app.entries,app.sources,app.readTab,app.writeTab,app.settings,
        app.help,app.about,app.contact,
        app.profileTab,app.profilePick,app.profileName,app.profileSource,app.profileSourceBrowse,app.profileTypes,
        app.profileExclude,app.profileDest,app.profileDestBrowse,app.profilePattern,app.profileSave,app.profilePreview,
        app.profilePack,app.profileList,app.queueTab,app.queueAdd,app.queueRemove,app.queueRun,app.queueOpen,
        app.queueFolder,app.queueList};
    for(unsigned i=0;i<ARRAY(actions);i++) EnableWindow(actions[i],!active);
    EnableWindow(app.password,!active && index_of(app.format,3,0)==ZIP);
    EnableWindow(app.profileDelete,!active && index_of(app.profilePick,(int)app.profiles.count+1,0)>0);
    EnableWindow(app.profilePack,!active && app.profileReady);
    ShowWindow(app.cancel,active ? SHOW : 0); EnableWindow(app.cancel,TRUE);
    ShowWindow(app.progress,active ? SHOW : 0);
    SendMessageW(app.progress,PBM_SETMARQUEE,FALSE,0);
    SetWindowLongPtrW(app.progress,GWL_STYLE,GetWindowLongPtrW(app.progress,GWL_STYLE)|PBS_MARQUEE);
    if(active) SendMessageW(app.progress,PBM_SETMARQUEE,TRUE,42);
    layout();
}
static void start_work(int type) {
    if(!app.enabled) {
        release(app.selected); app.selected=0;
        memset(app.passwordText,0,sizeof(app.passwordText));
        app.operation=(Operation){.code=UNAVAILABLE}; show_error(); return;
    }
    app.kind=type; InterlockedExchange(&app.canceled,0); app.lastProgress=0; app.lastPercent=-2;
    app.operation=(Operation){.cancel=&app.canceled,.progress=progress_callback,.fileProgress=file_progress};
    busy(true); status(T("Operacja w toku…","Working…"));
    Handle thread=CreateThread(0,0,worker,0,0,0);
    if(thread) CloseHandle(thread);
    else {
        busy(false); release(app.selected); app.selected=0;
        memset(app.passwordText,0,sizeof(app.passwordText));
        app.operation.code=IO_ERROR; show_error();
    }
}
static void finish_work(void) {
    busy(false);
    Error code=app.operation.code;
    if(code==PASSWORD_ERROR && (app.kind==OPEN || app.kind==EXTRACT || app.kind==CHECK)) {
        if(ask_password()) { start_work(app.kind); return; }
        code=CANCELED;
    }
    memset(app.passwordText,0,sizeof(app.passwordText));
    release(app.selected); app.selected=0;
    if(app.kind==QUEUE_RUN && (code==SUCCESS || code==CANCELED)) {
        queue_summary();
        if(code==CANCELED) {
            W summary[384]=L"";
            copy(summary,ARRAY(summary),T("Anulowano. ","Canceled. "));
            append(summary,ARRAY(summary),app.statusText);
            status(summary);
        }
        if(code==SUCCESS && app.prefs.openFolder)
            ShellExecuteW(app.window,L"open",app.queueDestination,0,0,NORM);
        return;
    }
    if(app.kind==PROFILE_PREVIEW && code!=SUCCESS) entries_free(&app.preview);
    if(app.kind==PROFILE_PACK && code==SOURCE_CHANGED) invalidate_preview();
    if(code==CANCELED) { status(T("Anulowano operację.","Operation canceled.")); return; }
    if(code!=SUCCESS) { show_error(); return; }
    switch(app.kind) {
    case OPEN:
        entries_free(&app.entriesData); app.entriesData=app.incoming; app.incoming=(Entries){0};
        copy(app.archive,ARRAY(app.archive),app.target);
        caption(app.path,app.archive); rows(); count_status(); break;
    case EXTRACT:
        status(T("Rozpakowano do nowego folderu.","Extracted to a new folder."));
        if(app.prefs.openFolder) ShellExecuteW(app.window,L"open",app.target,0,0,NORM);
        break;
    case CHECK: status(T("Sprawdzono dane archiwum. Brak zgłoszonych błędów.","Archive data checked. No errors reported.")); break;
    case CREATE:
        caption(app.password,L""); status(T("Zapisano archiwum.","Archive saved."));
        if(MessageBoxW(app.window,T("Archiwum zapisane. Otworzyć jego zawartość?","Archive saved. Open its contents?"),
           L"Archiwa",YESNO|OK_INFO)==YES) { mode(MODE_READ); start_work(OPEN); }
        break;
    case PROFILE_PREVIEW: show_preview(); break;
    case PROFILE_PACK:
        invalidate_preview();
        caption(app.profileOutput,app.profileTarget);
        status(T("Zapisano ZIP według profilu.","Profile ZIP saved."));
        break;
    }
}
static void open_archive(const W *given) {
    W path[PATH_CAP]=L"";
    if(given) { if(!copy(path,ARRAY(path),given)) return; }
    else if(!pick_file(path,ARRAY(path),archive_filter(),OFN_FILEMUSTEXIST,false,0,0)) return;
    copy(app.target,ARRAY(app.target),path); start_work(OPEN);
}
static bool extraction_folder(W *root) {
    if(*app.prefs.destination && directory(app.prefs.destination) &&
       (app.prefs.defaultWithoutPrompt ||
        MessageBoxW(app.window,app.prefs.destination,T("Użyć domyślnego folderu?","Use the default folder?"),YESNO|OK_INFO)==YES)) {
        return copy(root,PATH_CAP,app.prefs.destination);
    }
    return browse_folder(root,T("Wybierz folder nadrzędny. Program utworzy osobny folder dla każdego archiwum.",
        "Choose a parent folder. Each archive will be extracted to its own folder."));
}
static bool extraction_target(const W *archive,const W *root,W *target) {
    W name[256],candidate[256];
    if(!copy(name,ARRAY(name),basename_w(archive))) return false;
    W *ext=0;
    for(W *p=name;*p;p++) if(*p==L'.') ext=p;
    if(ext && ext!=name) {
        if(equal(ext,L".gz") && ext-name>=4 && CompareStringOrdinal(ext-4,4,L".tar",4,1)==2 && ext-4!=name) ext-=4;
        *ext=0;
    }
    if(!valid_filename(name) && !copy(name,ARRAY(name),L"Archiwa")) return false;
    if(!join(target,PATH_CAP,root,name)) return false;
    for(unsigned n=2;exists(target);n++) {
        W digit[24]; number(digit,n);
        if(!copy(candidate,ARRAY(candidate),name) || !append(candidate,ARRAY(candidate),L" (") ||
           !append(candidate,ARRAY(candidate),digit) || !append(candidate,ARRAY(candidate),L")") ||
           !join(target,PATH_CAP,root,candidate)) return false;
    }
    return true;
}
static void extract(void) {
    if(!*app.archive) { status(T("Najpierw otwórz archiwum.","Open an archive first.")); return; }
    W root[PATH_CAP];
    if(!extraction_folder(root)) return;
    if(!extraction_target(app.archive,root,app.target)) {
        app.operation=(Operation){.code=IO_ERROR}; show_error(); return;
    }
    int i=(int)SendMessageW(app.entries,LV_NEXT,(WP)-1,SELECTED);
    if(i>=0) {
        app.selected=memory(app.entriesData.count*sizeof(bool));
        if(!app.selected) { app.operation=(Operation){.code=NO_MEMORY}; show_error(); return; }
        for(;i>=0 && (unsigned)i<app.entriesData.count;i=(int)SendMessageW(app.entries,LV_NEXT,i,SELECTED)) app.selected[i]=true;
    }
    start_work(EXTRACT);
}
static void run_queue(void) {
    for(unsigned i=0;i<app.queueCount;i++) {
        if(app.queue[i].state==EXTRACTED) continue;
        if(extraction_folder(app.queueDestination)) {
            app.queueHere=false;
            start_work(QUEUE_RUN);
        }
        return;
    }
    status(T("Dodaj archiwa do rozpakowania.","Add archives to extract."));
}
static void create_archive(void) {
    if(!app.count) { status(T("Dodaj co najmniej jeden plik lub folder.","Add at least one file or folder.")); return; }
    const W *filters[]={L"ZIP (*.zip)\0*.zip\0",L"7z (*.7z)\0*.7z\0",L"TAR.GZ (*.tar.gz)\0*.tar.gz\0"};
    const W *extensions[]={L"zip",L"7z",L"tar.gz"};
    int format=index_of(app.format,3,0);
    W path[PATH_CAP]=L"";
    if(!pick_file(path,ARRAY(path),filters[format],0x8000,true,0,0)) return;
    const W *extension=extensions[format];
    size_t n=length(path), m=length(extension);
    if(n<=m || path[n-m-1]!=L'.' || !equal(path+n-m,extension)) {
        if(!append(path,ARRAY(path),L".") || !append(path,ARRAY(path),extension)) return;
    }
    copy(app.target,ARRAY(app.target),path);
    app.workFormat=(Format)format;
    app.workLevel=levels[index_of(app.level,3,1)];
    if(format==ZIP) GetWindowTextW(app.password,app.passwordText,ARRAY(app.passwordText));
    start_work(CREATE);
}
static void save_settings(void) {
    Preferences preferences={0};
    preferences.polish=index_of(app.lang,2,0)==0;
    preferences.format=index_of(app.defaultFormat,3,0);
    preferences.level=index_of(app.defaultLevel,3,1);
    preferences.openFolder=SendMessageW(app.openFolder,CHECK_GET,0,0)==1;
    preferences.startView=index_of(app.startView,ARRAY(start_modes),0);
    preferences.defaultWithoutPrompt=SendMessageW(app.skipPrompt,CHECK_GET,0,0)==1;
    GetWindowTextW(app.dest,preferences.destination,ARRAY(preferences.destination));
    if(*preferences.destination && !directory(preferences.destination)) {
        MessageBoxW(app.window,T("Wybierz istniejący folder rozpakowywania.","Choose an existing extraction folder."),L"Archiwa",OK_ERROR);
        return;
    }
    if(!preferences_save(&preferences)) { app.operation=(Operation){.code=IO_ERROR}; show_error(); return; }
    app.prefs=preferences;
    translate(); rows(); mode(app.previousMode); status(T("Zapisano ustawienia.","Settings saved."));
    if(!*app.archive) caption(app.path,T("Brak otwartego archiwum","No archive open"));
    SendMessageW(app.format,CB_SELECT,app.prefs.format,0);
    SendMessageW(app.level,CB_SELECT,app.prefs.level,0);
    EnableWindow(app.password,app.prefs.format==ZIP);
}
static bool copy_support_details(void) {
    W details[160];
    swprintf_s(details,ARRAY(details),T("Archiwa %ls (x64)\r\nJęzyk interfejsu: polski","Archiwa %ls (x64)\r\nInterface language: English"),
        ARCHIWA_VERSION_WIDE);
    size_t bytes=(length(details)+1)*sizeof(W);
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(!data) return false;
    void *buffer=GlobalLock(data);
    if(!buffer) { GlobalFree(data); return false; }
    memcpy(buffer,details,bytes);
    GlobalUnlock(data);
    if(!OpenClipboard(app.window)) { GlobalFree(data); return false; }
    bool ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,data);
    CloseClipboard();
    if(!ok) GlobalFree(data);
    return ok;
}
static void open_support_link(const W *link) {
    if((INT_PTR)ShellExecuteW(app.window,L"open",link,0,0,NORM)<=32)
        status(T("Nie udało się otworzyć adresu wsparcia.","Could not open the support address."));
}
static void action(unsigned id,unsigned notification,LPARAM data) {
    W path[PATH_CAP];
    if(id==CANCEL && app.busy) {
        InterlockedExchange(&app.canceled,1); EnableWindow(app.cancel,0);
        status(T("Anulowanie operacji…","Canceling…")); return;
    }
    if(app.busy) return;
    switch(id) {
    case OPEN: open_archive(notification==1 ? (const W *)data : 0); break;
    case EXTRACT: extract(); break;
    case CHECK:
        if(*app.archive) start_work(CHECK); else status(T("Najpierw otwórz archiwum.","Open an archive first."));
        break;
    case NEW_ARCHIVE: mode(MODE_CREATE); break;
    case 100: mode(MODE_READ); break;
    case SETTINGS: settings_form(&app.prefs); mode(MODE_SETTINGS); break;
    case HELP_PAGE: mode(MODE_HELP); break;
    case ABOUT_PAGE: mode(MODE_ABOUT); break;
    case CONTACT_PAGE: mode(MODE_CONTACT); break;
    case SUPPORT_COPY:
        status(copy_support_details() ? T("Skopiowano dane do zgłoszenia.","Support details copied.") :
            T("Nie udało się skopiować danych.","Could not copy support details."));
        break;
    case SUPPORT_EMAIL:
        if(*ARCHIWA_SUPPORT_EMAIL) open_support_link(L"mailto:" ARCHIWA_SUPPORT_EMAIL L"?subject=Archiwa");
        break;
    case SUPPORT_SITE:
        if(*ARCHIWA_SUPPORT_URL) open_support_link(ARCHIWA_SUPPORT_URL);
        break;
    case ADD_FILES: pick_sources(); break;
    case ADD_DIR:
        if(browse_folder(path,T("Wybierz folder do spakowania.","Choose a folder to archive."))) {
            input_add(path); count_status();
        }
        break;
    case REMOVE:
        for(unsigned i=app.count;i>0;i--) if(SendMessageW(app.sources,LB_GETSEL,i-1,0)>0) {
            release(app.inputs[i-1]);
            for(unsigned j=i;j<app.count;j++) app.inputs[j-1]=app.inputs[j];
            app.count--; SendMessageW(app.sources,LB_DEL,i-1,0);
        }
        count_status(); break;
    case CREATE: create_archive(); break;
    case FORMAT: EnableWindow(app.password,index_of(app.format,3,0)==ZIP); break;
    case PICK_DEST:
        if(browse_folder(path,T("Wybierz domyślny folder rozpakowywania.","Choose the default extraction folder."))) caption(app.dest,path);
        break;
    case SAVE_PREFS: save_settings(); break;
    case RESET_PREFS: {
        Preferences defaults=default_preferences();
        settings_form(&defaults);
        status(T("Przywrócono domyślne wartości. Zapisz ustawienia, aby je zastosować.",
            "Defaults restored. Select Save settings to apply them."));
        break;
    }
    case BACK: mode(app.previousMode); break;
    case PROFILES: mode(MODE_PROFILES); break;
    case PROFILE_PICK:
        if(notification==CBN_SELCHANGE) profile_field_reset();
        break;
    case PROFILE_BROWSE_SOURCE:
        if(browse_folder(path,T("Wybierz folder źródłowy.","Choose the source folder."))) {
            app.profileSourceOverride[0]=0;
            caption(app.profileSource,path);
        }
        break;
    case PROFILE_BROWSE_DEST:
        if(browse_folder(path,T("Wybierz folder docelowy ZIP.","Choose where the ZIP will be saved."))) caption(app.profileDest,path);
        break;
    case PROFILE_SOURCE: case PROFILE_TYPES: case PROFILE_EXCLUDE: case PROFILE_DEST: case PROFILE_PATTERN:
        if(notification==EN_CHANGE && app.profileReady) {
            invalidate_preview();
            status(T("Pokaż listę plików po zmianie reguł.","Preview the files after changing the rules."));
        }
        break;
    case PROFILE_SAVE: change_profile(false); break;
    case PROFILE_DELETE: change_profile(true); break;
    case PROFILE_PREVIEW: preview_profile(); break;
    case PROFILE_PACK:
        if(app.profileReady) {
            app.workLevel=levels[app.prefs.level];
            start_work(PROFILE_PACK);
        }
        break;
    case QUEUE: mode(MODE_QUEUE); break;
    case QUEUE_ADD: pick_archives(); break;
    case QUEUE_REMOVE:
        for(unsigned i=app.queueCount;i>0;i--) {
            if(!(SendMessageW(app.queueList,LVM_GETITEMSTATE,i-1,LVIS_SELECTED)&LVIS_SELECTED)) continue;
            release(app.queue[i-1].path);
            memmove(&app.queue[i-1],&app.queue[i],(app.queueCount-i)*sizeof(QueueEntry));
            app.queueCount--;
        }
        queue_changed(true);
        break;
    case QUEUE_RUN: run_queue(); break;
    case QUEUE_OPEN: {
        int selected=(int)SendMessageW(app.queueList,LV_NEXT,(WP)-1,SELECTED);
        if(selected>=0 && (unsigned)selected<app.queueCount) {
            mode(MODE_READ);
            open_archive(app.queue[selected].path);
        } else status(T("Zaznacz archiwum na liście.","Select an archive in the list."));
        break;
    }
    case QUEUE_FOLDER:
        if(*app.queueDestination && directory(app.queueDestination))
            ShellExecuteW(app.window,L"open",app.queueDestination,0,0,NORM);
        else status(T("Najpierw uruchom rozpakowywanie.","Run the extraction queue first."));
        break;
    }
}
static LRESULT CALLBACK main_proc(HWND window,unsigned msg,WP w,LPARAM l) {
    switch(msg) {
    case SIZE_MSG: if(app.title) layout(); return 0;
    case GETMINMAXINFO_MSG: ((MINMAXINFO *)l)->ptMinTrackSize=(Point){690,630}; return 0;
    case COMMAND: action(LOW(w),HIGH(w),l); return 0;
    case WM_HELP: if(!app.busy) mode(MODE_HELP); return TRUE;
    case DONE: finish_work(); return 0;
    case FILE_PROGRESS:
        if(app.busy && !InterlockedCompareExchange(&app.canceled,0,0)) {
            int percent=(int)(INT_PTR)w;
            LONG_PTR style=GetWindowLongPtrW(app.progress,GWL_STYLE);
            if(percent<0) {
                if(!(style&PBS_MARQUEE)) {
                    SetWindowLongPtrW(app.progress,GWL_STYLE,style|PBS_MARQUEE);
                    SendMessageW(app.progress,PBM_SETMARQUEE,TRUE,42);
                }
            } else {
                if(style&PBS_MARQUEE) {
                    SendMessageW(app.progress,PBM_SETMARQUEE,FALSE,0);
                    SetWindowLongPtrW(app.progress,GWL_STYLE,style&~PBS_MARQUEE);
                }
                SendMessageW(app.progress,PBM_SETPOS,(WP)percent,0);
            }
        }
        return 0;
    case QUEUE_ITEM: {
        W text[160];
        app.queueCurrent=(unsigned)w;
        swprintf_s(text,ARRAY(text),T("Rozpakowywanie archiwum %u z %u…","Extracting archive %u of %u…"),
            app.queueCurrent+1,app.queueCount);
        if(!InterlockedCompareExchange(&app.canceled,0,0)) status(text);
        return 0;
    }
    case QUEUE_RESULT:
        app.queue[w].error=(Error)l;
        app.queue[w].state=l==SUCCESS ? EXTRACTED : FAILED;
        list_value(app.queueList,(unsigned)w,1,queue_state(&app.queue[w]));
        return 0;
    case PROGRESS:
        if(app.busy && app.kind!=QUEUE_RUN && !InterlockedCompareExchange(&app.canceled,0,0)) {
            W text[120], n[24]; number(n,w);
            copy(text,ARRAY(text),T("Przetwarzanie elementów: ","Processing items: "));
            append(text,ARRAY(text),n); status(text);
        }
        return 0;
    case DROP_MSG: drop_sources((Handle)w); return 0;
    case CTLCOLORSTATIC:
        SetTextColor((Handle)w,COLOR(33,53,78)); SetBkColor((Handle)w,COLOR(244,247,251));
        return (intptr_t)background;
    case CLOSE_MSG:
        if(app.busy) { action(CANCEL,0,0); return 0; }
        DestroyWindow(window); return 0;
    case DESTROY_MSG: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,msg,w,l);
}
#define CT(type,text,style,ex,id) control(app.window,L##type,L##text,style,ex,id)
#define BUTTON(id) CT("BUTTON","",TABSTOP,0,id)
#define LABEL() CT("STATIC","",0,0,0)
#define COMBO(id) CT("COMBOBOX","",TABSTOP|VSCROLL|3,0,id)
#define EDIT(id) CT("EDIT","",TABSTOP|ES_AUTOHSCROLL,CLIENTEDGE,id)
static void setup_profiles(void) {
    app.profileTab=BUTTON(PROFILES);
    app.profilePickLabel=LABEL(); app.profilePick=COMBO(PROFILE_PICK);
    app.profileNameLabel=LABEL(); app.profileName=EDIT(PROFILE_NAME);
    app.profileSourceLabel=LABEL(); app.profileSource=EDIT(PROFILE_SOURCE);
    app.profileSourceBrowse=BUTTON(PROFILE_BROWSE_SOURCE);
    app.profileDestLabel=LABEL(); app.profileDest=EDIT(PROFILE_DEST);
    app.profileDestBrowse=BUTTON(PROFILE_BROWSE_DEST);
    app.profileTypesLabel=LABEL(); app.profileTypes=EDIT(PROFILE_TYPES);
    app.profileExcludeLabel=LABEL(); app.profileExclude=EDIT(PROFILE_EXCLUDE);
    app.profilePatternLabel=LABEL(); app.profilePattern=EDIT(PROFILE_PATTERN);
    const struct { Handle control; size_t size; } fields[]={
        {app.profileName,ARRAY(app.activeProfile.name)}, {app.profileSource,PATH_CAP},
        {app.profileDest,PATH_CAP}, {app.profileTypes,ARRAY(app.activeProfile.extensions)},
        {app.profileExclude,ARRAY(app.activeProfile.excluded)}, {app.profilePattern,ARRAY(app.activeProfile.pattern)}
    };
    for(unsigned i=0;i<ARRAY(fields);i++) SendMessageW(fields[i].control,EM_SETLIMITTEXT,fields[i].size-1,0);
    app.profileSave=BUTTON(PROFILE_SAVE); app.profileDelete=BUTTON(PROFILE_DELETE);
    app.profilePreview=BUTTON(PROFILE_PREVIEW); app.profilePack=BUTTON(PROFILE_PACK);
    app.profileOutput=CT("STATIC","",SS_PATHELLIPSIS,0,0);
    app.profileList=CT("LISTBOX","",VSCROLL|WS_HSCROLL|TABSTOP|LBS_NOINTEGRALHEIGHT,CLIENTEDGE,0);
    SendMessageW(app.profileList,LB_SETHORIZONTALEXTENT,2300,0);
}
static void setup_queue(void) {
    app.queueTab=BUTTON(QUEUE);
    app.queueAdd=BUTTON(QUEUE_ADD); app.queueRemove=BUTTON(QUEUE_REMOVE); app.queueOpen=BUTTON(QUEUE_OPEN);
    app.queueRun=BUTTON(QUEUE_RUN); app.queueFolder=BUTTON(QUEUE_FOLDER);
    app.queueList=CT("SysListView32","",TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,CLIENTEDGE,0);
    SendMessageW(app.queueList,LV_EXSTYLE,0,LVS_EX_DOUBLEBUFFER|LVS_EX_FULLROWSELECT);
    Column column={0};
    column.mask=LVCF_WIDTH;
    column.cx=360; SendMessageW(app.queueList,LV_NEWCOL,0,P(&column));
    column.cx=220; SendMessageW(app.queueList,LV_NEWCOL,1,P(&column));
}
static void setup_window(void) {
    app.appIcon=CT("STATIC","",SS_ICON|SS_CENTERIMAGE,0,0);
    HICON icon=LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,32,32,LR_SHARED);
    SendMessageW(app.appIcon,STM_SETICON,(WP)icon,0);
    app.status=LABEL(); app.title=CT("STATIC","Archiwa",0,0,0);
    SendMessageW(app.title,SETFONT,(WP)heading,1);
    app.subtitle=LABEL();
    app.help=BUTTON(HELP_PAGE); app.contact=BUTTON(CONTACT_PAGE); app.about=BUTTON(ABOUT_PAGE); app.settings=BUTTON(SETTINGS);
    app.readTab=BUTTON(100); app.writeTab=BUTTON(NEW_ARCHIVE);
    app.open=BUTTON(OPEN); app.extract=BUTTON(EXTRACT); app.check=BUTTON(CHECK);
    app.path=CT("STATIC","",0x4000,0,0);
    app.entries=CT("SysListView32","",TABSTOP|1|8,CLIENTEDGE,0);
    SendMessageW(app.entries,LV_EXSTYLE,0,0x10020);
    set_columns(true);
    app.addFiles=BUTTON(ADD_FILES); app.addDir=BUTTON(ADD_DIR); app.remove=BUTTON(REMOVE);
    app.sources=CT("LISTBOX","",VSCROLL|TABSTOP|0x900,CLIENTEDGE,0);
    app.formatLabel=LABEL(); app.format=COMBO(FORMAT); app.levelLabel=LABEL(); app.level=COMBO(0);
    app.passLabel=LABEL(); app.password=CT("EDIT","",TABSTOP|0xa0,CLIENTEDGE,PASSWORD);
    SendMessageW(app.password,0xc5,511,0);
    app.create=BUTTON(CREATE);
    app.langLabel=LABEL(); app.lang=COMBO(LANG); app.defaultLabel=LABEL(); app.defaultFormat=COMBO(0);
    app.startLabel=LABEL(); app.startView=COMBO(START_VIEW);
    app.defaultLevelLabel=LABEL(); app.defaultLevel=COMBO(0); app.destLabel=LABEL();
    app.dest=CT("EDIT","",TABSTOP|0x80,CLIENTEDGE,DEST); app.pickDest=BUTTON(PICK_DEST);
    app.openFolder=CT("BUTTON","",TABSTOP|3,0,0);
    app.skipPrompt=CT("BUTTON","",TABSTOP|BS_AUTOCHECKBOX,0,DEST_WITHOUT_PROMPT);
    app.save=BUTTON(SAVE_PREFS); app.back=BUTTON(BACK); app.reset=BUTTON(RESET_PREFS);
    setup_profiles(); setup_queue();
    app.detailText=CT("EDIT","",TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|VSCROLL,CLIENTEDGE,DETAIL_TEXT);
    SendMessageW(app.detailText,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(12,12));
    app.supportCopy=BUTTON(SUPPORT_COPY); app.supportEmail=BUTTON(SUPPORT_EMAIL); app.supportSite=BUTTON(SUPPORT_SITE);
    app.cancel=BUTTON(CANCEL); app.progress=CT("msctls_progress32","",PBS_MARQUEE,0,0);
    ShowWindow(app.cancel,0); ShowWindow(app.progress,0);
}
static void startup_arguments(int count,W **args) {
    if(count<2) return;
    if(equal(args[1],L"--profile") && count==3) {
        if(directory(args[2])) copy(app.profileSourceOverride,PATH_CAP,args[2]);
        mode(MODE_PROFILES);
        if(app.profiles.count==1) profile_choices(1);
        profile_field_reset();
    } else if(equal(args[1],L"--extract-here") && count>=3) {
        mode(MODE_QUEUE);
        bool complete=true;
        for(int i=2;i<count && complete;i++) complete=queue_add(args[i]);
        queue_changed(complete);
        if(complete && app.queueCount && parent_w(app.queueDestination,PATH_CAP,app.queue[0].path)) {
            app.queueHere=true;
            start_work(QUEUE_RUN);
        }
    } else if(count>2) {
        mode(MODE_QUEUE);
        bool complete=true;
        for(int i=1;i<count && complete;i++) complete=queue_add(args[i]);
        queue_changed(complete);
    } else {
        mode(MODE_READ);
        open_archive(args[1]);
    }
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE previous,PWSTR command,int show) {
    (void)module; (void)previous; (void)command; (void)show;
    ui_start(); preferences_load();
    bool loaded=profiles_load(&app.profiles);
    app.enabled=engine_init();
    WindowClass cls={sizeof(cls),0,main_proc,0,0,instance,LoadIconW(instance,MAKEINTRESOURCEW(1)),
        LoadCursorW(0,(const W *)32512),background,0,L"ArchiwaMain",0};
    RegisterClassExW(&cls);
    Rect workArea={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&workArea,0);
    int width=min(940,workArea.right-workArea.left), height=min(730,workArea.bottom-workArea.top);
    app.window=CreateWindowExW(APPWINDOW|CONTROLPARENT,L"ArchiwaMain",L"Archiwa",OVERLAPPED|CLIPCHILDREN,
        workArea.left+(workArea.right-workArea.left-width)/2,workArea.top+(workArea.bottom-workArea.top-height)/2,
        width,height,0,0,instance,0);
    if(!app.window) return 1;
    setup_window(); translate(); profile_field_reset();
    EnableWindow(app.password,app.prefs.format==ZIP);
    mode(start_modes[app.prefs.startView]);
    caption(app.path,T("Brak otwartego archiwum","No archive open"));
    DragAcceptFiles(app.window,1);
    ShowWindow(app.window,SHOW); UpdateWindow(app.window);
    int count=0; W **args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(app.enabled && args) startup_arguments(count,args);
    if(args) LocalFree(args);
    if(!app.enabled) { app.operation=(Operation){.code=UNAVAILABLE}; show_error(); }
    else if(!loaded && !app.busy) status(T("Nie udało się odczytać zapisanych profili.","Could not load the saved profiles."));
    int result=loop(app.window);
    entries_free(&app.entriesData);
    entries_free(&app.incoming);
    entries_free(&app.preview);
    for(unsigned i=0;i<app.count;i++) release(app.inputs[i]);
    release(app.inputs);
    for(unsigned i=0;i<app.queueCount;i++) release(app.queue[i].path);
    release(app.queue);
    CoUninitialize();
    return result;
}
