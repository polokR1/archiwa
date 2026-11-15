#ifndef PROFILES_H
#define PROFILES_H
#include "engine.h"

#define PROFILE_LIMIT 64

typedef struct {
    W name[96], pattern[192], extensions[256], excluded[256];
    W source[PATH_CAP], destination[PATH_CAP];
} Profile;

typedef struct {
    unsigned count;
    Profile items[PROFILE_LIMIT];
} Profiles;

typedef enum { PROFILE_SAVED, PROFILE_NAME_USED, PROFILE_CONFLICT, PROFILE_STORAGE_ERROR } ProfileSave;

bool profiles_load(Profiles *);
ProfileSave profiles_update(Profiles *,int,const Profile *);
bool profile_target(const Profile *,W *);
bool scan_profile(const Profile *,const W *,Entries *,Operation *);

#endif
