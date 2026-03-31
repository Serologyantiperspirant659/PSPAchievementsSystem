#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <string.h>

#include "paths.h"
#include "profile.h"
#include "game_db.h"
#include "game_map.h"
#include "detect.h"
#include "popup.h"
#include "rcheevos_glue.h"
#include "beep.h"
#include "memory.h"

PSP_MODULE_INFO("PspAchievements", 0x1000, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/ACH/pach_log.txt"

static void log_msg(const char *msg) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, msg, strlen(msg)); sceIoWrite(fd, "\n", 1); sceIoClose(fd); }
}

static void log_uint(const char *label, unsigned int val) {
    char buf[64];
    char tmp[16];
    int i = 0, j, tlen = 0;
    unsigned int v = val;
    while (*label) buf[i++] = *label++;
    buf[i++] = '=';
    buf[i++] = '0'; buf[i++] = 'x';
    if (v == 0) { tmp[tlen++] = '0'; }
    else {
        while (v > 0) {
            int d = v & 0xF;
            tmp[tlen++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
            v >>= 4;
        }
    }
    for (j = tlen - 1; j >= 0; j--) buf[i++] = tmp[j];
    buf[i] = '\0';
    log_msg(buf);
}

static volatile int g_running    = 1;
static SceUID g_logic_thid       = -1;
static SceUID g_draw_thid        = -1;
static PACH_ProfileData          g_profile;
static PACH_GameMapDb            g_mapdb;
static PACH_LoadedGame           g_game;
static PACH_ProfileGameProgress *g_game_progress = NULL;
static RC_RuntimeState           g_rc_state;
static RC_ParsedAchievement      g_parsed[PACH_MAX_GAME_ACH];
static int                       g_num_parsed     = 0;
static int                       g_game_loaded    = 0;
static int                       g_supported_game = 0;
static char                      g_game_code[16];
static char                      g_active_profile_name[32];

/* ============================================================
 * PROFILE INITIALIZATION
 * ============================================================ */
static int init_profile(void) {
    memset(&g_profile, 0, sizeof(g_profile));
    memset(g_active_profile_name, 0, sizeof(g_active_profile_name));
    pach_profile_ensure_dirs();

    if (pach_profile_get_active_name(g_active_profile_name, sizeof(g_active_profile_name))) {
        if (pach_profile_load(&g_profile, g_active_profile_name)) {
            log_msg("Profile LOADED from disk!");
            return 1;
        }
    }

    log_msg("Creating NEW default profile");
    strcpy(g_active_profile_name, "default");
    pach_profile_init_empty(&g_profile, g_active_profile_name);
    pach_profile_save(&g_profile);
    pach_profile_set_active_name(g_active_profile_name);
    return 1;
}

/* ============================================================
 * DRAW THREAD
 * ============================================================ */
static int draw_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;

    while (g_running) {
        if (!pach_popup_is_active()) {
            sceKernelDelayThread(100 * 1000); /* 100ms - idle polling */
            continue;
        }
        sceDisplayWaitVblankStartCB();
        pach_popup_update();
        /* Draw 3 times across the frame (~16.7ms at 60fps).
         * The game may overwrite the back buffer during rendering,
         * so we redraw several times to ensure at least one
         * draw lands after the game finishes its frame. */
        pach_popup_draw_current();
        sceKernelDelayThread(5000);   /* +5ms */
        pach_popup_draw_current();
        sceKernelDelayThread(5000);   /* +10ms */
        pach_popup_draw_current();
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

/* ============================================================
 * LOGIC THREAD
 * ============================================================ */
static int logic_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;
    int warmup_frames = 0;

    /* Disable FPU exceptions — game memory may contain SNaN values
       that would trap the MIPS FPU when used in float operations. */
    __asm__ volatile("ctc1 $zero, $31");

    log_msg("STEP 1: thread started");
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pach_popup_init();
    log_msg("STEP 2: popup init done");

    if (pach_beep_init()) log_msg("STEP 3: beep OK");
    else                  log_msg("STEP 3: beep FAILED");

    log_msg("STEP 4: waiting 5s for disc...");
    sceKernelDelayThread(5 * 1000 * 1000);
    log_msg("STEP 5: detecting game...");

    if (!pach_detect_game_code(g_game_code, sizeof(g_game_code))) {
        log_msg("No disc detected - plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    log_msg("STEP 6: game detected:");
    log_msg(g_game_code);

    if (!pach_gamemap_load(&g_mapdb, PACH_GAME_MAP_FILE)) {
        log_msg("ERROR: could not load game_map.dat - plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }
    log_msg("STEP 7: gamemap loaded");

    PACH_GameMapEntry *entry = pach_gamemap_find_by_code(&g_mapdb, g_game_code);
    if (!entry) {
        log_msg("Game not in game_map.dat - no achievements, plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }
    log_msg("STEP 8: game found in map");
    g_supported_game = 1;

    log_msg("STEP 8a: building path");
    {
        char path[128];
        strcpy(path, PACH_GAMES_DIR);
        log_msg("STEP 8b: strcpy done");
        strcat(path, entry->ach_file);
        log_msg("STEP 8c: strcat done");
        log_msg("STEP 9: loading ach file:");
        log_msg(path);
        if (!pach_game_load_file(&g_game, path)) {
            log_msg("ERROR: could not load .ach file - plugin idle");
            while (g_running) sceKernelDelayThread(500 * 1000);
            sceKernelExitDeleteThread(0);
            return 0;
        }
    }
    log_msg("STEP 10: ach file loaded");

    g_game_progress = pach_profile_get_or_create_game(
        &g_profile, g_game.header.game_id,
        g_game.header.num_achievements);
    log_msg("STEP 11: profile game entry ready");

    rc_glue_init(&g_rc_state);
    log_msg("STEP 12: rc_glue_init done");

    g_num_parsed = rc_glue_parse_all(&g_game, g_parsed, PACH_MAX_GAME_ACH);
    log_msg("STEP 13: rc_glue_parse_all done");

    g_game_loaded = 1;
    log_msg("STEP 14: game logic loaded OK");

    pach_popup_show("Achievements", "System Loaded OK");
    log_msg("STEP 15: startup popup shown");

    log_msg("STEP 16: entering main loop");
    while (g_running) {

        if (warmup_frames < 200) {
            warmup_frames++;

            /* Skip first 50 frames - let game fully initialize memory */
            if (warmup_frames <= 50) {
                sceKernelDelayThread(100 * 1000);  /* 100ms per frame */
                continue;
            }

            /* Only evaluate every 5th warmup frame to reduce CPU load */
            if ((warmup_frames % 5) != 0) {
                sceKernelDelayThread(100 * 1000);
                continue;
            }

            if (warmup_frames == 55) {
                log_uint("num_parsed", (unsigned int)g_num_parsed);
                log_msg("STEP 17: starting warmup eval");
            }

            RC_EvalResult warmup_res = rc_glue_update(
                &g_game, g_game_progress, &g_rc_state,
                g_parsed, g_num_parsed);

            if (warmup_frames == 55) log_msg("STEP 18: first rc_glue_update returned");

            /* Undo any unlocks that fired during warmup */
            if (warmup_res.unlocked_count > 0) {
                int i;
                for (i = 0; i < warmup_res.unlocked_count; i++) {
                    int idx = warmup_res.unlocked_indices[i];
                    g_parsed[idx].is_active = 1;
                    if (g_game_progress && idx < g_game_progress->num_achievements)
                        g_game_progress->unlock_time[idx] = 0;
                }
            }

            if (warmup_frames == 200) {
                int i, g2, c;
                log_msg("STEP 19: warmup complete, resetting hits");
                for (i = 0; i < g_num_parsed; i++) {
                    for (g2 = 0; g2 < g_parsed[i].num_groups; g2++) {
                        RC_CondGroup *grp = &g_parsed[i].groups[g2];
                        for (c = 0; c < grp->count; c++)
                            grp->conds[c].current_hits = 0;
                    }
                }
                /* Also reset unlock times so warmup unlocks don't persist */
                if (g_game_progress) {
                    int ii;
                    for (ii = 0; ii < g_game_progress->num_achievements; ii++)
                        g_game_progress->unlock_time[ii] = 0;
                }
                log_msg("STEP 20: ready");
            }

            sceKernelDelayThread(100 * 1000);  /* 100ms between warmup evals */
            continue;
        }

        /* ---- Normal evaluation ----
         * rc_glue_update uses round-robin (1 ach per call).
         * With 39 achievements @ 100ms interval = ~4 sec per full cycle.
         * This keeps CPU load near zero. */
        {
            RC_EvalResult res = rc_glue_update(
                &g_game, g_game_progress, &g_rc_state,
                g_parsed, g_num_parsed);

            if (res.unlocked_count > 0) {
                int i;
                for (i = 0; i < res.unlocked_count; i++) {
                    if (res.unlocked_defs[i]) {
                        log_msg("ACHIEVEMENT UNLOCKED!");
                        log_msg(res.unlocked_defs[i]->title);
                        pach_popup_show(res.unlocked_defs[i]->title,
                                        res.unlocked_defs[i]->desc);
                    }
                }
                pach_beep_play();
                pach_profile_save(&g_profile);
            }
        }

        /* Debug trigger: L + R + Up */
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_LTRIGGER) && (pad.Buttons & PSP_CTRL_RTRIGGER) && (pad.Buttons & PSP_CTRL_UP)) {
            pach_popup_show("Debug", "Engine is ACTIVE");
            pach_beep_play();
            while ((pad.Buttons & PSP_CTRL_LTRIGGER) && (pad.Buttons & PSP_CTRL_UP) && g_running) {
                sceCtrlPeekBufferPositive(&pad, 1);
                sceKernelDelayThread(16 * 1000);
            }
        }

        sceKernelDelayThread(100 * 1000);  /* 100ms between normal evals */
    }

    sceKernelExitDeleteThread(0);
    return 0;
}

/* ============================================================
 * MODULE ENTRY POINT
 * ============================================================ */
int module_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    g_running = 1;

    sceIoMkdir("ms0:/PSP/ACH", 0777);
    sceIoMkdir("ms0:/PSP/ACH/games", 0777);
    sceIoMkdir("ms0:/PSP/ACH/profiles", 0777);

    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, "=== PLUGIN START ===\n", 21); sceIoClose(fd); }

    init_profile();

    g_logic_thid = sceKernelCreateThread("pach_logic", logic_thread_func, 0x30, 0x8000, 0, 0);
    if (g_logic_thid >= 0) sceKernelStartThread(g_logic_thid, 0, 0);

    g_draw_thid = sceKernelCreateThread("pach_draw", draw_thread_func, 0x40, 0x4000, 0, 0);
    if (g_draw_thid >= 0) sceKernelStartThread(g_draw_thid, 0, 0);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    g_running = 0;
    pach_beep_shutdown();
    if (g_logic_thid >= 0) { sceKernelWaitThreadEnd(g_logic_thid, NULL); }
    if (g_draw_thid  >= 0) { sceKernelWaitThreadEnd(g_draw_thid,  NULL); }
    if (g_game_loaded) pach_profile_save(&g_profile);
    log_msg("=== PLUGIN STOP ===");
    return 0;
}