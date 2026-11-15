#include "engine.h"

#define NATIVE_FUNCTIONS(F) \
    F(void *,archive_read_new,(void)) \
    F(int,archive_read_support_filter_all,(void *)) \
    F(int,archive_read_support_format_all,(void *)) \
    F(int,archive_read_add_passphrase,(void *,const char *)) \
    F(int,archive_read_open_filename_w,(void *,const W *,size_t)) \
    F(int,archive_read_next_header,(void *,void **)) \
    F(int,archive_read_data_skip,(void *)) \
    F(int64_t,archive_read_data,(void *,void *,size_t)) \
    F(int,archive_read_free,(void *)) \
    F(const char *,archive_error_string,(void *)) \
    F(const W *,archive_entry_pathname_w,(void *)) \
    F(const char *,archive_entry_pathname_utf8,(void *)) \
    F(unsigned,archive_entry_filetype,(void *)) \
    F(const W *,archive_entry_hardlink_w,(void *)) \
    F(const char *,archive_entry_hardlink_utf8,(void *)) \
    F(int64_t,archive_entry_size,(void *)) \
    F(int64_t,archive_entry_mtime,(void *)) \
    F(int,archive_entry_mtime_is_set,(void *)) \
    F(int,archive_entry_is_encrypted,(void *)) \
    F(void *,archive_write_new,(void)) \
    F(int,archive_write_set_format_zip,(void *)) \
    F(int,archive_write_set_format_7zip,(void *)) \
    F(int,archive_write_set_format_pax_restricted,(void *)) \
    F(int,archive_write_add_filter_gzip,(void *)) \
    F(int,archive_write_set_options,(void *,const char *)) \
    F(int,archive_write_set_passphrase,(void *,const char *)) \
    F(int,archive_write_open_filename_w,(void *,const W *)) \
    F(int,archive_write_header,(void *,void *)) \
    F(int64_t,archive_write_data,(void *,const void *,size_t)) \
    F(int,archive_write_close,(void *)) \
    F(int,archive_write_free,(void *)) \
    F(void *,archive_entry_new,(void)) \
    F(void,archive_entry_free,(void *)) \
    F(void,archive_entry_copy_pathname_w,(void *,const W *)) \
    F(void,archive_entry_set_filetype,(void *,unsigned)) \
    F(void,archive_entry_set_perm,(void *,unsigned)) \
    F(void,archive_entry_set_size,(void *,int64_t)) \
    F(void,archive_entry_set_mtime,(void *,int64_t,long))

#define DECLARE(ret,name,args) static ret (*name) args;
NATIVE_FUNCTIONS(DECLARE)

bool engine_init(void) {
    Handle lib=LoadLibraryExW(L"archiveint.dll",0,0x800);
    if(!lib) return false;
#define LOAD(ret,name,args) name=(ret (*) args)GetProcAddress(lib,#name); if(!name) { FreeLibrary(lib); return false; }
    NATIVE_FUNCTIONS(LOAD)
    return true;
}

static bool failed(Operation *op,Error code,const W *detail) {
    op->code=code;
    if(detail) copy(op->detail,ARRAY(op->detail),detail);
    return false;
}
static bool contains(const char *str,const char *word) {
    if(!str) return false;
    for(;*str;str++) {
        size_t n=0;
        while(word[n] && str[n]==word[n]) n++;
        if(!word[n]) return true;
    }
    return false;
}
static bool native_error(Operation *op,void *archive) {
    const char *s=archive_error_string(archive);
    Error code=ARCHIVE_ERROR;
    if(contains(s,"not supported") && (contains(s,"ncrypt") || contains(s,"assword"))) code=UNSUPPORTED_ENCRYPTION;
    else if(contains(s,"assphrase") || contains(s,"assword")) code=PASSWORD_ERROR;
    if(s) MultiByteToWideChar(0,0,s,-1,op->detail,ARRAY(op->detail));
    return failed(op,code,0);
}
static bool report(Operation *op,unsigned value) {
    if((op->cancel && InterlockedCompareExchange(op->cancel,0,0)) ||
       (op->progress && !op->progress(op->progressData,value))) return failed(op,CANCELED,0);
    return true;
}
static void report_file(Operation *op,uint64_t done,uint64_t total) {
    if(op->fileProgress) op->fileProgress(op->progressData,done,total);
}
void entries_free(Entries *list) {
    for(unsigned i=0;i<list->count;i++) release(list->items[i].name);
    release(list->items);
    *list=(Entries){0};
}
static bool add_entry(Entries *list,const W *name,void *entry,Operation *op) {
    if(list->count==list->capacity) {
        unsigned capacity=list->capacity ? list->capacity*2 : 256;
        if(capacity>4194304) return failed(op,NO_MEMORY,0);
        void *next=resize(list->items,(size_t)capacity*sizeof(Entry));
        if(!next) return failed(op,NO_MEMORY,0);
        list->items=next; list->capacity=capacity;
    }
    W *saved=duplicate(name);
    if(!saved) return failed(op,NO_MEMORY,0);
    int64_t size=archive_entry_size(entry);
    list->items[list->count++]=(Entry){
        .name=saved,.size=size>0 ? (uint64_t)size : 0,
        .folder=archive_entry_filetype(entry)==0040000,.encrypted=archive_entry_is_encrypted(entry)>0
    };
    return true;
}
static void *reader(const W *path,const W *password,Operation *op) {
    void *archive=archive_read_new();
    if(!archive) { failed(op,NO_MEMORY,0); return 0; }
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);
    if(*password) {
        char key[2048]={0};
        int count=WideCharToMultiByte(65001,0,password,-1,key,ARRAY(key),0,0);
        int result=count ? archive_read_add_passphrase(archive,key) : -30;
        memset(key,0,sizeof(key));
        if(result<0) { failed(op,PASSWORD_ERROR,0); archive_read_free(archive); return 0; }
    }
    if(archive_read_open_filename_w(archive,path,65536)>=0) return archive;
    native_error(op,archive); archive_read_free(archive); return 0;
}
static const W *entry_name(void *entry,W *buffer) {
    const W *name=archive_entry_pathname_w(entry);
    if(name && *name) return name;
    const char *utf8=archive_entry_pathname_utf8(entry);
    if(utf8 && MultiByteToWideChar(65001,8,utf8,-1,buffer,PATH_CAP)>0) return buffer;
    return 0;
}
static bool root_entry(const W *name) {
    return equal(name,L".") || equal(name,L"./") || equal(name,L".\\");
}
bool read_archive(const W *path,const W *password,Entries *list,Operation *op) {
    W buffer[PATH_CAP];
    void *archive=reader(path,password,op);
    if(!archive) return false;
    void *entry;
    int status;
    while(report(op,list->count) && (status=archive_read_next_header(archive,&entry))!=1) {
        if(status < -20) { native_error(op,archive); break; }
        const W *name=entry_name(entry,buffer);
        if(!name) { failed(op,UNSAFE_NAME,0); break; }
        if(!root_entry(name) && !add_entry(list,name,entry,op)) break;
        if(archive_read_data_skip(archive)<-20) { native_error(op,archive); break; }
    }
    archive_read_free(archive);
    if(op->code!=SUCCESS) entries_free(list);
    return op->code==SUCCESS;
}

/* Reject Windows device names, aliases, alternate streams and traversal before writing. */
static bool safe_part(const W *part) {
    if(!*part) return false;
    size_t n=length(part);
    if(part[n-1]==L'.' || part[n-1]==L' ') return false;
    W stem[12]={0};
    for(size_t i=0; i<n; i++) {
        W c=part[i];
        if(c<32 || c==L':' || c==L'"' || c==L'<' || c==L'>' || c==L'|' || c==L'*' ||
           c==L'?' || c==L'\\' || c==L'/') return false;
    }
    size_t i=0;
    while(i<n && i<ARRAY(stem)-1 && part[i]!=L'.') { stem[i]=part[i]; i++; }
    while(i && stem[i-1]==L' ') stem[--i]=0;
    if(equal(stem,L"CON") || equal(stem,L"NUL") || equal(stem,L"AUX") || equal(stem,L"PRN") ||
       equal(stem,L"CONIN$") || equal(stem,L"CONOUT$")) return false;
    if(i==4) {
        W last=stem[3]; stem[3]=0;
        if(((last>=L'0' && last<=L'9') || last==0xb9 || last==0xb2 || last==0xb3) &&
           (equal(stem,L"COM") || equal(stem,L"LPT"))) return false;
    }
    return true;
}
bool valid_filename(const W *name) {
    return length(name)<=255 && safe_part(name);
}
static bool make_directory(const W *path) {
    if(CreateDirectoryW(path,0)) return true;
    DWORD attrs=GetFileAttributesW(path);
    return attrs!=INVALID_ATTRS && (attrs&DIR) && !(attrs&REPARSE);
}
static bool unpack_path(W *output,const W *base,const W *name,bool folder,Operation *op) {
    if(name[0]==L'/' || name[0]==L'\\' || !join(output,PATH_CAP,base,L"")) return failed(op,UNSAFE_NAME,name);
    size_t used=length(output);
    unsigned segments=0;
    while(*name) {
        W part[260];
        size_t n=0;
        while(*name && *name!=L'/' && *name!=L'\\') {
            if(n>=ARRAY(part)-1) return failed(op,UNSAFE_NAME,0);
            part[n++]=*name++;
        }
        part[n]=0;
        while(*name==L'/' || *name==L'\\') name++;
        if(equal(part,L".")) continue;
        if(!safe_part(part) || !append(output,PATH_CAP,part)) return failed(op,UNSAFE_NAME,part);
        if(++segments>125) return failed(op,UNSAFE_NAME,part);
        if(*name) {
            if(!make_directory(output)) return failed(op,IO_ERROR,output);
            if(!append(output,PATH_CAP,L"\\")) return failed(op,UNSAFE_NAME,0);
        }
    }
    if(!segments || length(output)==used) return failed(op,UNSAFE_NAME,0);
    if(folder && !make_directory(output)) return failed(op,IO_ERROR,output);
    return true;
}
static void remove_temp_tree(const W *path,unsigned depth) {
    if(depth>130) return;
    W item[PATH_CAP];
    FindData info;
    if(!join(item,PATH_CAP,path,L"*")) return;
    Handle find=FindFirstFileW(item,&info);
    if(find!=BAD_HANDLE) {
        do {
            if(equal(info.cFileName,L".") || equal(info.cFileName,L"..") || !join(item,PATH_CAP,path,info.cFileName)) continue;
            if(info.dwFileAttributes&DIR) {
                if(info.dwFileAttributes&REPARSE) RemoveDirectoryW(item); else remove_temp_tree(item,depth+1);
            } else DeleteFileW(item);
        } while(FindNextFileW(find,&info));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}
static bool temp_name(W *temp,const W *target) {
    W parent[PATH_CAP], n[32];
    if(!parent_w(parent,ARRAY(parent),target) || !join(temp,PATH_CAP,parent,L".archiwa-")) return false;
    number(n,GetCurrentProcessId());
    if(!append(temp,PATH_CAP,n) || !append(temp,PATH_CAP,L"-")) return false;
    number(n,GetTickCount64());
    return append(temp,PATH_CAP,n);
}
static bool read_data(void *archive,Handle output,Operation *op,unsigned index,int64_t size) {
    char bytes[65536];
    int64_t count;
    uint64_t done=0, total=size>0 ? (uint64_t)size : 0;
    report_file(op,0,total);
    while((count=archive_read_data(archive,bytes,sizeof(bytes)))>0) {
        if(!report(op,index)) return false;
        if(output && !write_all(output,bytes,(DWORD)count)) return failed(op,IO_ERROR,0);
        done+=(uint64_t)count;
        report_file(op,done,total);
    }
    return count==0 || native_error(op,archive);
}
static bool wanted(const Entries *list,const bool *selected,unsigned index,const W *name) {
    if(!selected) return true;
    if(index>=list->count || !equal(list->items[index].name,name)) return false;
    if(selected[index]) return true;
    for(unsigned i=0;i<list->count;i++) {
        if(!selected[i] || !list->items[i].folder) continue;
        const W *parent=list->items[i].name;
        size_t n=length(parent);
        while(n && (parent[n-1]==L'/' || parent[n-1]==L'\\')) n--;
        if(length(name)>n && (name[n]==L'/' || name[n]==L'\\') &&
           CompareStringOrdinal(parent,(int)n,name,(int)n,0)==2) return true;
    }
    return false;
}
static bool stream_archive(const W *path,const W *password,const W *base,const Entries *list,const bool *selected,Operation *op) {
    void *archive=reader(path,password,op);
    if(!archive) return false;
    void *entry;
    int status;
    unsigned index=0;
    W output[PATH_CAP], buffer[PATH_CAP];
    while(report(op,index) && (status=archive_read_next_header(archive,&entry))!=1) {
        if(status<-20) { native_error(op,archive); break; }
        if(!base) {
            if(!read_data(archive,0,op,index++,archive_entry_size(entry))) break;
            continue;
        }
        const W *name=entry_name(entry,buffer);
        if(!name) { failed(op,UNSAFE_NAME,0); break; }
        if(root_entry(name)) continue;
        if(selected && (index>=list->count || CompareStringOrdinal(list->items[index].name,-1,name,-1,0)!=2)) {
            failed(op,ARCHIVE_ERROR,name); break;
        }
        if(!wanted(list,selected,index++,name)) continue;
        unsigned type=archive_entry_filetype(entry);
        if((type!=0100000 && type!=0040000) || archive_entry_hardlink_w(entry) || archive_entry_hardlink_utf8(entry)) {
            failed(op,UNSAFE_NAME,name); break;
        }
        if(!unpack_path(output,base,name,type==0040000,op)) break;
        if(type==0040000) continue;
        Handle file=CreateFileW(output,WRITE_ACCESS,0,0,CREATE_NEW,NORMAL_FILE,0);
        if(file==BAD_HANDLE) { failed(op,exists(output) ? EXISTS : IO_ERROR,name); break; }
        bool result=read_data(archive,file,op,index,archive_entry_size(entry));
        if(result && archive_entry_mtime_is_set(entry)) {
            int64_t stamp=archive_entry_mtime(entry);
            if(stamp>=-11644473600LL && stamp<253402300800LL) {
                uint64_t ticks=(uint64_t)(stamp+11644473600LL)*10000000ULL;
                FileTime time={(DWORD)ticks,(DWORD)(ticks>>32)};
                SetFileTime(file,0,0,&time);
            }
        }
        CloseHandle(file);
        if(!result) break;
    }
    if(op->code==SUCCESS && selected && index!=list->count) failed(op,ARCHIVE_ERROR,0);
    archive_read_free(archive);
    return op->code==SUCCESS;
}
bool unpack_archive(const W *path,const W *password,const W *destination,const Entries *list,const bool *selected,Operation *op) {
    W temp[PATH_CAP];
    if(exists(destination)) return failed(op,EXISTS,destination);
    if(!temp_name(temp,destination) || !CreateDirectoryW(temp,0)) return failed(op,IO_ERROR,destination);
    bool ok=stream_archive(path,password,temp,list,selected,op) && report(op,list ? list->count : 0);
    if(ok && !MoveFileExW(temp,destination,0)) ok=failed(op,exists(destination) ? EXISTS : IO_ERROR,destination);
    if(!ok) remove_temp_tree(temp,0);
    return ok;
}
bool test_archive(const W *path,const W *password,Operation *op) {
    return stream_archive(path,password,0,0,0,op);
}
static bool add_source(void *archive,const W *source,const W *name,const Entry *expected,
                       Operation *op,unsigned *count,unsigned depth) {
    if(depth>100 || !report(op,*count)) return op->code!=SUCCESS ? false : failed(op,IO_ERROR,source);
    DWORD attrs=GetFileAttributesW(source);
    if(attrs==INVALID_ATTRS) return failed(op,expected ? SOURCE_CHANGED : IO_ERROR,source);
    if(attrs&REPARSE) return failed(op,SOURCE_LINK,source);
    bool isFolder=(attrs&DIR)!=0;
    if(expected && isFolder) return failed(op,SOURCE_CHANGED,source);
    Handle file=CreateFileW(source,READ_ACCESS,SHARE_READ,0,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(isFolder ? FILE_FLAG_BACKUP_SEMANTICS : NORMAL_FILE),0);
    if(file==BAD_HANDLE) return failed(op,IO_ERROR,source);
    BY_HANDLE_FILE_INFORMATION data;
    if(!GetFileInformationByHandle(file,&data)) {
        CloseHandle(file); return failed(op,IO_ERROR,source);
    }
    if(data.dwFileAttributes&REPARSE) {
        CloseHandle(file); return failed(op,SOURCE_LINK,source);
    }
    if(((data.dwFileAttributes&DIR)!=0)!=isFolder) {
        CloseHandle(file); return failed(op,SOURCE_CHANGED,source);
    }
    uint64_t size=isFolder ? 0 : ((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
    uint64_t ticks=((uint64_t)data.ftLastWriteTime.dwHighDateTime<<32)|data.ftLastWriteTime.dwLowDateTime;
    if(expected && (size!=expected->size || ticks!=expected->stamp)) {
        CloseHandle(file); return failed(op,SOURCE_CHANGED,source);
    }
    void *entry=archive_entry_new();
    if(!entry) { CloseHandle(file); return failed(op,NO_MEMORY,0); }
    archive_entry_copy_pathname_w(entry,name);
    archive_entry_set_filetype(entry,isFolder ? 0040000 : 0100000);
    archive_entry_set_perm(entry,isFolder ? 0755 : 0644);
    archive_entry_set_size(entry,(int64_t)size);
    archive_entry_set_mtime(entry,(int64_t)(ticks/10000000)-11644473600LL,(long)((ticks%10000000)*100));
    FindData info;
    bool ok=archive_write_header(archive,entry)==0 || native_error(op,archive);
    archive_entry_free(entry);
    if(ok && !isFolder) {
        char bytes[65536];
        uint64_t done=0;
        report_file(op,0,size);
        while(done<size && (ok=report(op,*count))) {
            uint64_t remaining=size-done;
            DWORD actual=0, request=remaining>sizeof(bytes) ? sizeof(bytes) : (DWORD)remaining;
            if(!ReadFile(file,bytes,request,&actual,0) || !actual) { ok=failed(op,IO_ERROR,source); break; }
            if(archive_write_data(archive,bytes,actual)!=(int64_t)actual) { ok=native_error(op,archive); break; }
            done+=actual;
            report_file(op,done,size);
        }
    }
    CloseHandle(file);
    if(!ok) return false;
    (*count)++;
    if(!isFolder) return true;
    W child[PATH_CAP], archived[PATH_CAP];
    if(!join(child,ARRAY(child),source,L"*")) return failed(op,IO_ERROR,source);
    Handle find=FindFirstFileW(child,&info);
    if(find==BAD_HANDLE) return GetLastError()==2 || failed(op,IO_ERROR,source);
    do {
        if(equal(info.cFileName,L".") || equal(info.cFileName,L"..")) continue;
        if(!join(child,ARRAY(child),source,info.cFileName) || !copy(archived,ARRAY(archived),name) ||
            !append(archived,ARRAY(archived),L"/") || !append(archived,ARRAY(archived),info.cFileName)) {
            ok=failed(op,IO_ERROR,source); break;
        }
        if(!add_source(archive,child,archived,0,op,count,depth+1)) { ok=false; break; }
    } while(FindNextFileW(find,&info));
    if(ok && GetLastError()!=18) ok=failed(op,IO_ERROR,source);
    FindClose(find);
    return ok;
}
static void *start_archive(const W *target,W *temp,Format format,unsigned level,const W *password,Operation *op) {
    if(exists(target)) { failed(op,EXISTS,target); return 0; }
    if(!temp_name(temp,target)) { failed(op,IO_ERROR,target); return 0; }
    Handle placeholder=CreateFileW(temp,WRITE_ACCESS,0,0,CREATE_NEW,NORMAL_FILE,0);
    if(placeholder==BAD_HANDLE) { failed(op,IO_ERROR,target); return 0; }
    CloseHandle(placeholder);
    void *archive=archive_write_new();
    if(!archive) { DeleteFileW(temp); failed(op,NO_MEMORY,0); return 0; }
    char quality[]="compression-level=6";
    quality[ARRAY(quality)-2]=(char)('0'+level);
    int status;
    switch(format) {
    case ZIP:
        status=archive_write_set_format_zip(archive);
        if(!status) status=archive_write_set_options(archive,"zip:hdrcharset=UTF-8");
        break;
    case SEVEN_Z:
        status=archive_write_set_format_7zip(archive);
        break;
    default:
        status=archive_write_set_format_pax_restricted(archive);
        if(!status) status=archive_write_add_filter_gzip(archive);
        if(!status) status=archive_write_set_options(archive,"pax:hdrcharset=UTF-8");
    }
    if(!status && format==SEVEN_Z && level==9) status=archive_write_set_options(archive,"7zip:compression=lzma2");
    if(!status) status=archive_write_set_options(archive,quality);
    if(!status && *password && format==ZIP) {
        char key[2048]={0};
        int n=WideCharToMultiByte(65001,0,password,-1,key,ARRAY(key),0,0);
        status=n ? archive_write_set_passphrase(archive,key) : -30;
        memset(key,0,sizeof(key));
        if(!status) status=archive_write_set_options(archive,"zip:encryption=aes256");
    }
    if(!status) status=archive_write_open_filename_w(archive,temp);
    if(status==0) return archive;
    native_error(op,archive);
    archive_write_free(archive);
    DeleteFileW(temp);
    return 0;
}
static bool finish_archive(void *archive,const W *temp,const W *target,bool ok,unsigned done,Operation *op) {
    if(ok && archive_write_close(archive)!=0) ok=native_error(op,archive);
    archive_write_free(archive);
    if(ok) ok=report(op,done);
    if(ok && !MoveFileExW(temp,target,0)) ok=failed(op,exists(target) ? EXISTS : IO_ERROR,target);
    if(!ok) DeleteFileW(temp);
    return ok;
}
bool make_archive(const W *target,const W *const *paths,unsigned count,Format format,unsigned level,const W *password,Operation *op) {
    for(unsigned i=0;i<count;i++) {
        const W *name=basename_w(paths[i]);
        if(!*name) return failed(op,IO_ERROR,paths[i]);
        size_t n=length(paths[i]);
        if(directory(paths[i]) && length(target)>n && target[n]==L'\\' &&
           CompareStringOrdinal(paths[i],(int)n,target,(int)n,1)==2) return failed(op,IO_ERROR,target);
        for(unsigned j=0;j<i;j++) {
            if(equal(name,basename_w(paths[j]))) return failed(op,DUPLICATE_INPUT,name);
        }
    }
    W temp[PATH_CAP];
    void *archive=start_archive(target,temp,format,level,password,op);
    if(!archive) return false;
    bool ok=true;
    unsigned done=0;
    for(unsigned i=0;i<count && ok;i++) ok=add_source(archive,paths[i],basename_w(paths[i]),0,op,&done,0);
    return finish_archive(archive,temp,target,ok,done,op);
}
bool make_profile_zip(const W *target,const W *root,const Entries *files,unsigned level,Operation *op) {
    if(!files->count) return failed(op,NO_MATCHES,0);
    W temp[PATH_CAP];
    void *archive=start_archive(target,temp,ZIP,level,L"",op);
    if(!archive) return false;
    bool ok=true;
    unsigned done=0;
    for(unsigned i=0;i<files->count && ok;i++) {
        const Entry *file=&files->items[i];
        W path[PATH_CAP], name[PATH_CAP];
        if(!join(path,ARRAY(path),root,file->name) || !copy(name,ARRAY(name),file->name)) {
            ok=failed(op,IO_ERROR,file->name); break;
        }
        for(W *p=name;*p;p++) if(*p==L'\\') *p=L'/';
        ok=add_source(archive,path,name,file,op,&done,0);
    }
    return finish_archive(archive,temp,target,ok,done,op);
}
