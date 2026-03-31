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

static volatile int g_running = 1;
static SceUID g_logic_thid = -1;
static SceUID g_draw_thid  = -1;
static PACH_ProfileData          g_profile;
static PACH_GameMapDb            g_mapdb;
static PACH_LoadedGame           g_game;
static PACH_ProfileGameProgress *g_game_progress = NULL;
static RC_RuntimeState           g_rc_state;
static RC_ParsedAchievement      g_parsed[PACH_MAX_GAME_ACH];
static int                       g_num_parsed = 0;
static int                       g_game_loaded = 0;
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
 * DRAW THREAD (Dedicated, Low Priority, Flicker-Free)
 *
 * Always polls at ~16ms intervals so it never misses the start
 * of a popup. The 100ms sleep was causing the first popup in a
 * sequence to be skipped because the logic thread could advance
 * the queue while the draw thread was sleeping.
 * ============================================================ */
static int draw_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;

    while (g_running) {
        if (!pach_popup_is_active()) {
            /* Short sleep - wake up quickly when a popup arrives */
            sceKernelDelayThread(16 * 1000);
            continue;
        }

        sceDisplayWaitVblankStartCB();
        pach_popup_draw_current();

        /* Draw again after 8ms to cover both framebuffers
         * in double-buffered games */
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

    /* Initialize beep audio after kernel is ready */
    if (pach_beep_init()) {
        log_msg("Beep audio: OK");
    } else {
        log_msg("Beep audio: FAILED (no audio channel available)");
    }

    /* Wait 4 seconds for ARK-4 to mount the ISO into disc0: */
    sceKernelDelayThread(4 * 1000 * 1000);

    /* Detect the game and load achievements */
    if (pach_detect_game_code(g_game_code, sizeof(g_game_code))) {
        log_msg("Game Detected:");
        log_msg(g_game_code);

        if (pach_gamemap_load(&g_mapdb, PACH_GAME_MAP_FILE)) {
            PACH_GameMapEntry *entry = pach_gamemap_find_by_code(&g_mapdb, g_game_code);
            if (entry) {
                char path[128];
                strcpy(path, PACH_GAMES_DIR);
                strcat(path, entry->ach_file);
                if (pach_game_load_file(&g_game, path)) {
                    g_game_progress = pach_profile_get_or_create_game(
                        &g_profile, g_game.header.game_id,
                        g_game.header.num_achievements);
                    rc_glue_init(&g_rc_state);
                    g_num_parsed = rc_glue_parse_all(&g_game, g_parsed, PACH_MAX_GAME_ACH);
                    g_game_loaded = 1;
                    log_msg("Game logic loaded OK");
                }
            } else {
                log_msg("Game found, but not in game_map.dat");
            }
        }
    } else {
        log_msg("Failed to detect game code");
    }

    /* Show startup popup depending on load result */
    if (g_game_loaded) {
        pach_popup_show("Achievements", "System Loaded OK");
        log_msg("Showing startup popup");
    }

    while (g_running) {
        pach_popup_update();

        /* WARMUP: run the evaluator so delta slots get populated,
         * but do NOT act on any results - no popups, no unlocks. */
        if (warmup_frames < 200) {
            warmup_frames++;
            if (g_game_loaded && g_game_progress && g_game.loaded && g_num_parsed > 0) {
                /* Evaluate but throw away the result during warmup.
                 * IMPORTANT: rc_glue_update marks achievements as unlocked
                 * inside profile - we must NOT call it during warmup.
                 * Instead we only update delta snapshots manually. */
                int i;
                for (i = 0; i < g_num_parsed; i++) {
                    /* Just tick deltas without evaluating conditions */
                }
                /* Call a read-only delta update - we need a way to tick
                 * deltas without triggering unlocks. For now we call
                 * update normally but immediately re-activate any that fired. */
                RC_EvalResult warmup_res = rc_glue_update(
                    &g_game, g_game_progress, &g_rc_state,
                    g_parsed, g_num_parsed);

                /* During warmup: if something fired, re-activate it so it
                 * can fire again properly after warmup is complete */
                if (warmup_res.unlocked_count > 0) {
                    int i2;
                    for (i2 = 0; i2 < warmup_res.unlocked_count; i2++) {
                        int idx = warmup_res.unlocked_indices[i2];
                        /* Re-activate so it gets evaluated again after warmup */
                        g_parsed[idx].is_active = 1;
                        /* Clear the unlock from profile */
                        if (g_game_progress && idx < g_game_progress->num_achievements) {
                            g_game_progress->unlock_time[idx] = 0;
                        }
                    }
                }
            }
            if (warmup_frames == 200) {
                log_msg("Warmup complete. Hits reset.");
                int i;
                for (i = 0; i < g_num_parsed; i++) {
                    int g2, c;
                    for (g2 = 0; g2 < g_parsed[i].num_groups; g2++) {
                        for (c = 0; c < g_parsed[i].groups[g2].count; c++) {
                            g_parsed[i].groups[g2].conds[c].current_hits = 0;
                        }
                    }
                }
            }
            sceKernelDelayThread(16 * 1000);
            continue;
        }

        /* Logic Evaluation (~80ms interval to save CPU) */
        static int eval_counter = 0;
        eval_counter++;
        if (eval_counter >= 5) {
            eval_counter = 0;
            if (g_game_loaded && g_game_progress && g_game.loaded && g_num_parsed > 0) {
                RC_EvalResult res = rc_glue_update(
                    &g_game, g_game_progress, &g_rc_state,
                    g_parsed, g_num_parsed);

                /* Handle all achievements that fired this frame */
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
                    /* Single beep for the whole batch */
                    pach_beep_play();
                    pach_profile_save(&g_profile);
                }
            }
        }

        /* Debug Popup Trigger (L + R buttons) */
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