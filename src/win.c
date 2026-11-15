#include "win.h"

HINSTANCE instance;
HFONT font, bold, heading;
HBRUSH background;

void *memory(size_t size) { return HeapAlloc(GetProcessHeap(),8,size); }
void *resize(void *p,size_t size) { return p ? HeapReAlloc(GetProcessHeap(),8,p,size) : memory(size); }
void release(void *p) { if(p) HeapFree(GetProcessHeap(),0,p); }
size_t length(const W *p) { size_t n=0; while(p[n]) n++; return n; }
bool equal(const W *a,const W *b) { return CompareStringOrdinal(a,-1,b,-1,1)==2; }
bool append(W *out,size_t cap,const W *s) {
    size_t n=length(out), size=length(s);
    if(n+size>=cap) return false;
    memcpy(out+n,s,(size+1)*sizeof(W));
    return true;
}
bool copy(W *out,size_t cap,const W *s) { out[0]=0; return append(out,cap,s); }
bool join(W *out,size_t cap,const W *base,const W *name) {
    size_t n=length(base);
    return copy(out,cap,base) && (!n || base[n-1]==L'\\' || append(out,cap,L"\\")) && append(out,cap,name);
}
const W *basename_w(const W *path) {
    const W *name=path;
    for(const W *p=path;*p;p++) if(*p==L'\\' || *p==L'/') name=p+1;
    return name;
}
bool parent_w(W *out,size_t cap,const W *path) {
    size_t n=(size_t)(basename_w(path)-path);
    if(!n || n>=cap) return false;
    if(n>3) n--;
    memcpy(out,path,n*sizeof(W)); out[n]=0; return true;
}
W *duplicate(const W *value) {
    size_t bytes=(length(value)+1)*sizeof(W);
    W *s=memory(bytes);
    if(s) memcpy(s,value,bytes);
    return s;
}
void number(W *out,uint64_t n) {
    W reverse[24]; unsigned i=0,j=0;
    do { reverse[i++]=(W)(L'0'+n%10); n/=10; } while(n);
    while(i) out[j++]=reverse[--i];
    out[j]=0;
}
bool exists(const W *p) { return GetFileAttributesW(p)!=INVALID_ATTRS; }
bool directory(const W *p) {
    DWORD attrs=GetFileAttributesW(p);
    return attrs!=INVALID_ATTRS && (attrs&DIR);
}
bool write_all(Handle file,const void *data,DWORD size) {
    DWORD written=0;
    return WriteFile(file,data,size,&written,0) && written==size;
}
bool user_path(W *out,const W *name) {
    W base[PATH_CAP];
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,ARRAY(base));
    return n>0 && n<ARRAY(base) && join(out,PATH_CAP,base,name);
}
void center_window(Handle window,Handle owner) {
    Rect w,r={0,0,GetSystemMetrics(0),GetSystemMetrics(1)};
    if(owner) GetWindowRect(owner,&r);
    GetWindowRect(window,&w);
    SetWindowPos(window,0,r.left+((r.right-r.left)-(w.right-w.left))/2,
        r.top+((r.bottom-r.top)-(w.bottom-w.top))/2,0,0,0x15);
}
Handle control(Handle parent,const W *type,const W *text,DWORD style,DWORD ex,int id) {
    Handle c=CreateWindowExW(ex,type,text,CHILD|VISIBLE|style,0,0,10,10,parent,H(id),instance,0);
    SendMessageW(c,SETFONT,(WP)font,1);
    return c;
}
void move(Handle c,int x,int y,int width,int height) { MoveWindow(c,x,y,width,height,1); }
void caption(Handle c,const W *s) { SetWindowTextW(c,s); }
void ui_start(void) {
    instance=GetModuleHandleW(0);
    CoInitializeEx(0,2);
    CommonControls cc={sizeof(cc),0x4021};
    InitCommonControlsEx(&cc);
    font=CreateFontW(-16,0,0,0,400,0,0,0,1,0,0,5,0,L"Segoe UI");
    bold=CreateFontW(-16,0,0,0,600,0,0,0,1,0,0,5,0,L"Segoe UI");
    heading=CreateFontW(-31,0,0,0,650,0,0,0,1,0,0,5,0,L"Segoe UI");
    background=CreateSolidBrush(COLOR(244,247,251));
}
int loop(Handle window) {
    Message m;
    int state;
    while((state=GetMessageW(&m,0,0,0))>0) {
        if(m.message==0x100 && m.wParam==0x1b && m.hwnd==window) {
            SendMessageW(window,CLOSE_MSG,0,0);
            continue;
        }
        if(!IsDialogMessageW(window,&m)) {
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    return state<0 ? 1 : (int)m.wParam;
}
