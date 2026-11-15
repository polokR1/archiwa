#ifndef ENGINE_H
#define ENGINE_H
#include "win.h"

typedef enum { ZIP, SEVEN_Z, TAR_GZ } Format;
typedef enum {
    SUCCESS, UNAVAILABLE, ARCHIVE_ERROR, IO_ERROR, NO_MEMORY, PASSWORD_ERROR,
    UNSUPPORTED_ENCRYPTION, UNSAFE_NAME, EXISTS, CANCELED, DUPLICATE_INPUT, SOURCE_LINK,
    NO_MATCHES, SOURCE_CHANGED
} Error;
typedef struct { W *name; uint64_t size, stamp; bool folder, encrypted; } Entry;
typedef struct { Entry *items; unsigned count, capacity; } Entries;
typedef struct {
    Error code;
    W detail[384];
    volatile LONG *cancel;
    bool (*progress)(void *,unsigned);
    void (*fileProgress)(void *,uint64_t,uint64_t);
    void *progressData;
} Operation;

bool engine_init(void);
void entries_free(Entries *);
bool read_archive(const W *,const W *,Entries *,Operation *);
bool unpack_archive(const W *,const W *,const W *,const Entries *,const bool *,Operation *);
bool test_archive(const W *,const W *,Operation *);
bool make_archive(const W *,const W *const *,unsigned,Format,unsigned,const W *,Operation *);
bool make_profile_zip(const W *,const W *,const Entries *,unsigned,Operation *);
bool valid_filename(const W *);
#endif
