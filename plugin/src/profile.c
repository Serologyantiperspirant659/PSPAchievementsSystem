#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psprtc.h>
#include <string.h>

#include "profile.h"
#include "paths.h"

#define LOG_PATH "ms0:/PSP/ACH/pach_log.txt"
static void prof_log(const char *msg) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, msg, strlen(msg)); sceIoWrite(fd, "\n", 1); sceIoClose(fd); }
}

static void build_profile_path(char *out, const char *name) {
    strcpy(out, PACH_PROFILES_DIR);
    strcat(out, name);
    strcat(out, ".prof");
}

int pach_profile_ensure_dirs(void) {
    sceIoMkdir(PACH_ROOT_DIR, 0777);
    sceIoMkdir(PACH_GAMES_DIR, 0777);
    sceIoMkdir(PACH_PROFILES_DIR, 0777);
    return 1;
}

int pach_profile_get_active_name(char *out_name, int max_len) {
    SceUID fd = sceIoOpen(PACH_ACTIVE_PROFILE, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    memset(out_name, 0, max_len);
    int read_bytes = sceIoRead(fd, out_name, max_len - 1);
    sceIoClose(fd);
    if (read_bytes <= 0) return 0;
    out_name[max_len - 1] = '\0';
    return 1;
}

int pach_profile_set_active_name(const char *name) {
    if (!name || !name[0]) return 0;
    SceUID fd = sceIoOpen(PACH_ACTIVE_PROFILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    sceIoWrite(fd, name, strlen(name));
    sceIoClose(fd);
    return 1;
}

void pach_profile_init_empty(PACH_ProfileData *profile, const char *name) {
    if (!profile) return;
    memset(profile, 0, sizeof(PACH_ProfileData));
    profile->header.magic[0] = PACH_PROFILE_MAGIC_0;
    profile->header.magic[1] = PACH_PROFILE_MAGIC_1;
    profile->header.magic[2] = PACH_PROFILE_MAGIC_2;
    profile->header.magic[3] = PACH_PROFILE_MAGIC_3;
    profile->header.version = PACH_VERSION;
    profile->header.num_games = 0;
    if (name) {
        strncpy(profile->header.username, name, PACH_MAX_PROFILE_NAME - 1);
        profile->header.username[PACH_MAX_PROFILE_NAME - 1] = '\0';
    }
}

int pach_profile_load(PACH_ProfileData *profile, const char *name) {
    SceUID fd;
    char path[128];
    int read_bytes;

    if (!profile || !name || !name[0]) return 0;
    build_profile_path(path, name);

    prof_log("profile: loading...");
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) { prof_log("profile: not found, will create new"); return 0; }

    memset(profile, 0, sizeof(PACH_ProfileData));
    read_bytes = sceIoRead(fd, profile, sizeof(PACH_ProfileData));
    sceIoClose(fd);

    if (read_bytes != sizeof(PACH_ProfileData)) {
        prof_log("profile: size mismatch, resetting");
        return 0;
    }

    if (profile->header.magic[0] != PACH_PROFILE_MAGIC_0 || profile->header.version != PACH_VERSION) {
        prof_log("profile: old version detected, resetting to v3");
        return 0;
    }

    prof_log("profile: loaded successfully!");
    return 1;
}

int pach_profile_save(PACH_ProfileData *profile) {
    SceUID fd;
    char path[128];

    if (!profile || !profile->header.username[0]) return 0;
    build_profile_path(path, profile->header.username);

    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) { prof_log("profile: ERROR saving!"); return 0; }

    int written = sceIoWrite(fd, profile, sizeof(PACH_ProfileData));
    sceIoClose(fd);

    if (written == sizeof(PACH_ProfileData)) {
        prof_log("profile: saved successfully!");
        return 1;
    }
    return 0;
}

PACH_ProfileGameProgress *pach_profile_find_game(PACH_ProfileData *profile, int game_id) {
    if (!profile) return 0;
    for (int i = 0; i < profile->header.num_games; i++) {
        if (profile->games[i].game_id == game_id) return &profile->games[i];
    }
    return 0;
}

PACH_ProfileGameProgress *pach_profile_get_or_create_game(PACH_ProfileData *profile, int game_id, int num_achievements) {
    PACH_ProfileGameProgress *gp = pach_profile_find_game(profile, game_id);
    if (gp) return gp;

    if (profile->header.num_games >= PACH_MAX_PROFILE_GAMES) return 0;

    gp = &profile->games[profile->header.num_games];
    memset(gp, 0, sizeof(PACH_ProfileGameProgress));
    gp->game_id = game_id;
    gp->num_achievements = num_achievements;
    profile->header.num_games++;
    prof_log("profile: created new game entry");

    return gp;
}

int pach_profile_is_unlocked(PACH_ProfileGameProgress *gp, int ach_index) {
    if (!gp || ach_index < 0 || ach_index >= gp->num_achievements) return 0;
    return gp->unlock_time[ach_index] > 0 ? 1 : 0;
}

void pach_profile_set_unlocked(PACH_ProfileGameProgress *gp, int ach_index) {
    if (!gp || ach_index < 0 || ach_index >= gp->num_achievements) return;
    
    ScePspDateTime time;
    sceRtcGetCurrentClockLocalTime(&time);
    
    unsigned int y = (time.year > 2000) ? (time.year - 2000) : 0;
    unsigned int ts = (y * 31536000) + (time.month * 2592000) + (time.day * 86400) + 
                      (time.hour * 3600) + (time.minute * 60) + time.second;
                      
    if (ts == 0) ts = 1;
    
    gp->unlock_time[ach_index] = ts;
}