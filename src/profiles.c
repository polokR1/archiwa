#include "profiles.h"
#include <stdlib.h>
#include <wchar.h>

static const W profiles_file[]=L"ArchiwaProfiles.dat";
static const uint32_t profile_signature=0x31505241;

bool profiles_load(Profiles *profiles) {
    W path[PATH_CAP];
    profiles->count=0;
    if(!user_path(path,profiles_file)) return false;
    Handle file=CreateFileW(path,READ_ACCESS,SHARE_READ|SHARE_DELETE,0,OPEN_EXISTING,NORMAL_FILE,0);
    if(file==BAD_HANDLE) return GetLastError()==ERROR_FILE_NOT_FOUND;
    uint32_t header[2]={0};
    DWORD bytes=0;
    bool ok=ReadFile(file,header,sizeof(header),&bytes,0) && bytes==sizeof(header) &&
        header[0]==profile_signature && header[1]<=PROFILE_LIMIT;
    DWORD size=ok ? (DWORD)(header[1]*sizeof(Profile)) : 0;
    if(ok) ok=ReadFile(file,profiles->items,size,&bytes,0) && bytes==size;
    CloseHandle(file);
    if(!ok) return false;
    for(unsigned i=0;i<header[1];i++) {
        const Profile *p=&profiles->items[i];
        if(!*p->name || p->name[ARRAY(p->name)-1] || p->pattern[ARRAY(p->pattern)-1] ||
           p->extensions[ARRAY(p->extensions)-1] || p->excluded[ARRAY(p->excluded)-1] ||
           p->source[ARRAY(p->source)-1] || p->destination[ARRAY(p->destination)-1]) return false;
    }
    profiles->count=header[1];
    return true;
}

static bool profiles_write(const Profiles *profiles) {
    W path[PATH_CAP],temp[PATH_CAP],suffix[24];
    number(suffix,GetCurrentProcessId());
    if(!user_path(path,profiles_file) || !copy(temp,ARRAY(temp),path) || !append(temp,ARRAY(temp),L".") ||
       !append(temp,ARRAY(temp),suffix)) return false;
    Handle file=CreateFileW(temp,WRITE_ACCESS,0,0,CREATE_NEW,NORMAL_FILE,0);
    if(file==BAD_HANDLE) return false;
    uint32_t header[]={profile_signature,profiles->count};
    bool ok=write_all(file,header,sizeof(header)) &&
        write_all(file,profiles->items,(DWORD)(profiles->count*sizeof(Profile))) && FlushFileBuffers(file);
    CloseHandle(file);
    if(ok) ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok) DeleteFileW(temp);
    return ok;
}

ProfileSave profiles_update(Profiles *profiles,int selected,const Profile *incoming) {
    W path[PATH_CAP];
    if(!user_path(path,L"ArchiwaProfiles.lock")) return PROFILE_STORAGE_ERROR;
    Handle lock=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|DELETE,0,0,OPEN_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,0);
    if(lock==BAD_HANDLE) return PROFILE_STORAGE_ERROR;
    Profiles *current=memory(sizeof(*current));
    ProfileSave result=PROFILE_STORAGE_ERROR;
    if(current && profiles_load(current)) {
        int old=-1, sameName=-1;
        for(unsigned i=0;i<current->count;i++) {
            const Profile *existing=&current->items[i];
            if(selected>=0 && equal(existing->name,profiles->items[selected].name)) old=(int)i;
            if(incoming && equal(existing->name,incoming->name)) sameName=(int)i;
        }
        if(selected>=0 && (old<0 || memcmp(&current->items[old],&profiles->items[selected],sizeof(Profile))))
            result=PROFILE_CONFLICT;
        else if(sameName>=0 && old!=sameName) result=PROFILE_NAME_USED;
        else if(incoming && old<0 && current->count==PROFILE_LIMIT) result=PROFILE_STORAGE_ERROR;
        else {
            if(incoming) {
                if(old<0) old=(int)current->count++;
                current->items[old]=*incoming;
            } else {
                for(unsigned i=(unsigned)old+1;i<current->count;i++) current->items[i-1]=current->items[i];
                current->count--;
            }
            if(profiles_write(current)) { *profiles=*current; result=PROFILE_SAVED; }
        }
    }
    release(current);
    CloseHandle(lock);
    return result;
}

static bool profile_name(const Profile *profile,W *result,size_t capacity) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    W date[16];
    if(!GetDateFormatEx(LOCALE_NAME_INVARIANT,0,&now,L"yyyy-MM-dd",date,ARRAY(date),0)) return false;
    const W *folder=basename_w(profile->source);
    W drive[2]={profile->source[0],0};
    if(!*folder) folder=drive;
    result[0]=0;
    for(const W *p=profile->pattern;*p;) {
        if(*p==L'{') {
            const W *value;
            if(wcsncmp(p,L"{folder}",8)==0) { value=folder; p+=8; }
            else if(wcsncmp(p,L"{date}",6)==0) { value=date; p+=6; }
            else return false;
            if(!append(result,capacity,value)) return false;
        } else {
            W literal[2]={*p++,0};
            if(literal[0]==L'}' || !append(result,capacity,literal)) return false;
        }
    }
    return valid_filename(result);
}

bool profile_target(const Profile *profile,W *path) {
    W name[256],candidate[256];
    if(!profile_name(profile,name,ARRAY(name))) return false;
    size_t size=length(name);
    if(size>4 && equal(name+size-4,L".zip")) name[size-4]=0;
    if(!valid_filename(name)) return false;
    for(unsigned n=1;n<=10000;n++) {
        if(!copy(candidate,ARRAY(candidate),name)) return false;
        if(n>1) {
            W digits[24];
            number(digits,n);
            if(!append(candidate,ARRAY(candidate),L" (") || !append(candidate,ARRAY(candidate),digits) ||
               !append(candidate,ARRAY(candidate),L")")) return false;
        }
        if(!append(candidate,ARRAY(candidate),L".zip") ||
           !join(path,PATH_CAP,profile->destination,candidate)) return false;
        if(!exists(path)) return true;
    }
    return false;
}

static bool separated(W ch,bool extensions) {
    return ch==L';' || (extensions && (ch==L',' || ch==L' ' || ch==L'\t'));
}

static bool filter_match(const W *rules,const W *name,bool extensions) {
    bool hasRule=false;
    size_t nameLength=length(name);
    for(const W *p=rules;*p;) {
        const W *start=p;
        while(*p && !separated(*p,extensions)) p++;
        const W *end=p;
        if(*p) p++;
        while(start<end && (*start==L' ' || *start==L'\t')) start++;
        while(end>start && (end[-1]==L' ' || end[-1]==L'\t')) end--;
        if(start==end) continue;
        hasRule=true;
        if(extensions && end-start==1 && *start==L'*') return true;
        if(extensions && end-start>1 && *start==L'*' && start[1]==L'.') start+=2;
        else if(extensions && *start==L'.') start++;
        size_t n=(size_t)(end-start);
        if(!n) continue;
        if(!extensions && n==nameLength && CompareStringOrdinal(name,(int)n,start,(int)n,TRUE)==CSTR_EQUAL)
            return true;
        if(extensions && nameLength>n && name[nameLength-n-1]==L'.' &&
           CompareStringOrdinal(name+nameLength-n,(int)n,start,(int)n,TRUE)==CSTR_EQUAL) return true;
    }
    return extensions && !hasRule;
}

static bool scan_failure(Operation *op,Error error,const W *path) {
    op->code=error;
    if(path) wcsncpy_s(op->detail,ARRAY(op->detail),path,_TRUNCATE);
    return false;
}

static bool add_profile_file(Entries *files,const W *relative,const FindData *info,Operation *op) {
    if(files->count==files->capacity) {
        unsigned capacity=files->capacity ? files->capacity*2 : 256;
        if(capacity>131072) return scan_failure(op,NO_MEMORY,0);
        void *next=resize(files->items,(size_t)capacity*sizeof(Entry));
        if(!next) return scan_failure(op,NO_MEMORY,0);
        files->items=next;
        files->capacity=capacity;
    }
    W *name=duplicate(relative);
    if(!name) return scan_failure(op,NO_MEMORY,0);
    files->items[files->count++]=(Entry){
        .name=name,
        .size=((uint64_t)info->nFileSizeHigh<<32)|info->nFileSizeLow,
        .stamp=((uint64_t)info->ftLastWriteTime.dwHighDateTime<<32)|info->ftLastWriteTime.dwLowDateTime
    };
    return true;
}

static bool scan_folder(const Profile *profile,const W *folder,const W *relative,const W *target,
                        Entries *files,Operation *op,unsigned depth) {
    if(depth>100) return scan_failure(op,IO_ERROR,folder);
    if(op->cancel && InterlockedCompareExchange(op->cancel,0,0)) return scan_failure(op,CANCELED,0);
    W pattern[PATH_CAP];
    if(!join(pattern,ARRAY(pattern),folder,L"*")) return scan_failure(op,IO_ERROR,folder);
    FindData info;
    Handle find=FindFirstFileW(pattern,&info);
    if(find==BAD_HANDLE) return GetLastError()==ERROR_FILE_NOT_FOUND || scan_failure(op,IO_ERROR,folder);
    bool ok=true;
    do {
        if(equal(info.cFileName,L".") || equal(info.cFileName,L"..")) continue;
        bool isFolder=(info.dwFileAttributes&DIR)!=0;
        if(isFolder && filter_match(profile->excluded,info.cFileName,false)) continue;
        if(!isFolder && !filter_match(profile->extensions,info.cFileName,true)) continue;
        if((op->cancel && InterlockedCompareExchange(op->cancel,0,0)) ||
           (op->progress && !op->progress(op->progressData,files->count))) {
            ok=scan_failure(op,CANCELED,0); break;
        }
        W path[PATH_CAP],name[PATH_CAP];
        if(!join(path,ARRAY(path),folder,info.cFileName) || !join(name,ARRAY(name),relative,info.cFileName)) {
            ok=scan_failure(op,IO_ERROR,folder); break;
        }
        if(equal(path,target)) continue;
        if(info.dwFileAttributes&REPARSE) { ok=scan_failure(op,SOURCE_LINK,path); break; }
        if(isFolder) ok=scan_folder(profile,path,name,target,files,op,depth+1);
        else ok=add_profile_file(files,name,&info,op);
        if(!ok) break;
    } while(FindNextFileW(find,&info));
    if(ok && GetLastError()!=ERROR_NO_MORE_FILES) ok=scan_failure(op,IO_ERROR,folder);
    FindClose(find);
    return ok;
}

static int __cdecl compare_entries(const void *left,const void *right) {
    const Entry *a=left,*b=right;
    return CompareStringOrdinal(a->name,-1,b->name,-1,TRUE)-CSTR_EQUAL;
}

bool scan_profile(const Profile *profile,const W *target,Entries *files,Operation *op) {
    DWORD attrs=GetFileAttributesW(profile->source);
    if(attrs==INVALID_ATTRS || !(attrs&DIR)) return scan_failure(op,IO_ERROR,profile->source);
    if(attrs&REPARSE) return scan_failure(op,SOURCE_LINK,profile->source);
    if(!scan_folder(profile,profile->source,L"",target,files,op,0)) return false;
    if(!files->count) return scan_failure(op,NO_MATCHES,0);
    qsort(files->items,files->count,sizeof(Entry),compare_entries);
    return true;
}
