#ifndef WIN_H
#define WIN_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string.h>

typedef wchar_t W;
typedef HANDLE Handle;
typedef WPARAM WP;
typedef POINT Point;
typedef RECT Rect;
typedef MSG Message;
typedef WNDCLASSEXW WindowClass;
typedef BROWSEINFOW BrowseInfo;
typedef INITCOMMONCONTROLSEX CommonControls;
typedef LVCOLUMNW Column;
typedef LVITEMW ListItem;
typedef FILETIME FileTime;
typedef WIN32_FIND_DATAW FindData;

#define BAD_HANDLE ((Handle)(intptr_t)-1)
#define ARRAY(a) (sizeof(a)/sizeof((a)[0]))
#define PATH_CAP 4096
#define DIR 0x10u
#define REPARSE 0x400u
#define INVALID_ATTRS 0xffffffffu
#define READ_ACCESS 0x80000000u
#define WRITE_ACCESS 0x40000000u
#define SHARE_READ 1
#define SHARE_DELETE 4
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define NORMAL_FILE 0x80
#define COLOR(r,g,b) ((DWORD)((r)|((g)<<8)|((b)<<16)))
#define P(v) ((LPARAM)(intptr_t)(v))
#define H(v) ((Handle)(intptr_t)(v))
#define LOW(v) ((unsigned short)(v))
#define HIGH(v) ((unsigned short)((v)>>16))

#define CHILD 0x40000000u
#define VISIBLE 0x10000000u
#define TABSTOP 0x00010000u
#define VSCROLL 0x00200000u
#define BORDER 0x00800000u
#define OVERLAPPED 0x00cf0000u
#define FIXED_WINDOW 0x00ca0000u
#define CLIPCHILDREN 0x02000000u
#define APPWINDOW 0x00040000u
#define CONTROLPARENT 0x00010000u
#define CLIENTEDGE 0x200u
#define CENTER 1u
#define SETFONT 0x30
#define COMMAND 0x111
#define SIZE_MSG 5
#define CLOSE_MSG 0x10
#define DESTROY_MSG 2
#define DROP_MSG 0x233
#define CTLCOLORSTATIC 0x138
#define GETMINMAXINFO_MSG 0x24
#define CB_ADD 0x143
#define CB_RESET 0x14b
#define CB_SELECT 0x14e
#define CB_GET 0x147
#define CHECK_SET 0xf1
#define CHECK_GET 0xf0
#define LB_ADD 0x180
#define LB_DEL 0x182
#define LB_SELCOUNT 0x190
#define LB_RESET 0x184
#define LVM 0x1000
#define LV_COUNT (LVM+4)
#define LV_DELETEALL (LVM+9)
#define LV_NEXT (LVM+12)
#define LV_NEWCOL (LVM+97)
#define LV_SETCOLUMN (LVM+96)
#define LV_COLWIDTH (LVM+30)
#define LV_INSERT (LVM+77)
#define LV_SETTEXT (LVM+116)
#define LV_EXSTYLE (LVM+54)
#define SELECTED 2
#define SHOW 5
#define NORM 1
#define OK_INFO 0x40
#define OK_ERROR 0x10
#define YESNO 0x04
#define YES 6

void *memory(size_t);
void *resize(void *,size_t);
void release(void *);
size_t length(const W *);
bool equal(const W *,const W *);
bool append(W *,size_t,const W *);
bool copy(W *,size_t,const W *);
bool join(W *,size_t,const W *,const W *);
const W *basename_w(const W *);
bool parent_w(W *,size_t,const W *);
W *duplicate(const W *);
void number(W *,uint64_t);
bool exists(const W *);
bool directory(const W *);
bool write_all(Handle,const void *,DWORD);
bool user_path(W *,const W *);
void center_window(Handle,Handle);
Handle control(Handle,const W *,const W *,DWORD,DWORD,int);
void move(Handle,int,int,int,int);
void caption(Handle,const W *);
void ui_start(void);
int loop(Handle);

extern HINSTANCE instance;
extern HFONT font, bold, heading;
extern HBRUSH background;
#endif
