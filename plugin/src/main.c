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

PSP_MODULE_INFO("PspAchievements", 0x1000, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define LOG_PATH "ms0:/PSP/ACH/pach_log.txt"
static void log_msg(const char *msg) {
    SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, msg, strlen(msg)); sceIoWrite(fd, "\n", 1); sceIoClose(fd); }
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
static int                       g_supported_game = 0; /* 1 = game is in game_map.dat */
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
            sceKernelDelayThread(16 * 1000);
            continue;
        }

        sceDisplayWaitVblankStartCB();
        pach_popup_draw_current();

        sceKernelDelayThread(8000);
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

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pach_popup_init();

    if (pach_beep_init()) {
        log_msg("Beep audio: OK");
    } else {
        log_msg("Beep audio: FAILED (no audio channel available)");
    }

    /* Wait for ARK-4 to mount the ISO into disc0: */
    sceKernelDelayThread(4 * 1000 * 1000);

    /* --- Game detection --- */
    if (!pach_detect_game_code(g_game_code, sizeof(g_game_code))) {
        /* No disc mounted (XMB, homebrew launcher, etc.) - sleep forever.
         * This is NOT an error, just an unsupported environment. */
        log_msg("No disc detected - plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    log_msg("Game detected:");
    log_msg(g_game_code);

    /* --- Load game map --- */
    if (!pach_gamemap_load(&g_mapdb, PACH_GAME_MAP_FILE)) {
        log_msg("ERROR: could not load game_map.dat - plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    /* --- Check if this game is supported --- */
    PACH_GameMapEntry *entry = pach_gamemap_find_by_code(&g_mapdb, g_game_code);
    if (!entry) {
        /* Game is running but has no achievements - idle silently.
         * Do NOT crash, do NOT show any popup. */
        log_msg("Game not in game_map.dat - no achievements, plugin idle");
        while (g_running) sceKernelDelayThread(500 * 1000);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    g_supported_game = 1;

    /* --- Load .ach file --- */
    {
        char path[128];
        strcpy(path, PACH_GAMES_DIR);
        strcat(path, entry->ach_file);

        if (!pach_game_load_file(&g_game, path)) {
            log_msg("ERROR: could not load .ach file - plugin idle");
            while (g_running) sceKernelDelayThread(500 * 1000);
            sceKernelExitDeleteThread(0);
            return 0;
        }
    }

    /* --- Initialize achievement runtime --- */
    g_game_progress = pach_profile_get_or_create_game(
        &g_profile, g_game.header.game_id,
        g_game.header.num_achievements);
    rc_glue_init(&g_rc_state);
    g_num_parsed = rc_glue_parse_all(&g_game, g_parsed, PACH_MAX_GAME_ACH);
    g_game_loaded = 1;
    log_msg("Game logic loaded OK");

    pach_popup_show("Achievements", "System Loaded OK");
    log_msg("Showing startup popup");

    /* ============================================================
     * MAIN LOOP
     * ============================================================ */
    while (g_running) {
        pach_popup_update();

        /* WARMUP: populate delta slots without triggering unlocks */
        if (warmup_frames < 200) {
            warmup_frames++;

            RC_EvalResult warmup_res = rc_glue_update(
                &g_game, g_game_progress, &g_rc_state,
                g_parsed, g_num_parsed);

            /* If anything fired during warmup, undo it - it will
             * fire again properly after warmup is complete */
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
                log_msg("Warmup complete. Hits reset.");
                int i;
                for (i = 0; i < g_num_parsed; i++) {
                    int g2, c;
                    for (g2 = 0; g2 < g_parsed[i].num_groups; g2++)
                        for (c = 0; c < g_parsed[i].groups[g2].count; c++)
                            g_parsed[i].groups[g2].conds[c].current_hits = 0;
                }
            }

            sceKernelDelayThread(16 * 1000);
            continue;
        }

        /* Logic evaluation every ~80ms */
        static int eval_counter = 0;
        eval_counter++;
        if (eval_counter >= 5) {
            eval_counter = 0;

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

        /* Debug trigger: L + R */
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_LTRIGGER) && (pad.Buttons & PSP_CTRL_RTRIGGER)) {
            pach_popup_show("Debug", "Engine is ACTIVE");
            pach_beep_play();
            while ((pad.Buttons & PSP_CTRL_LTRIGGER) && g_running) {
                sceCtrlPeekBufferPositive(&pad, 1);
                pach_popup_update();
                sceKernelDelayThread(16 * 1000);
            }
        }

        sceKernelDelayThread(16 * 1000);
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