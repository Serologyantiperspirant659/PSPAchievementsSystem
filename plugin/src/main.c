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
 * ============================================================ */
static int draw_thread_func(SceSize args, void *argp) {
    (void)args; (void)argp;
    
    while (g_running) {
        if (!pach_popup_is_active()) {
            sceKernelDelayThread(100 * 1000); /* Sleep when no popup */
            continue;
        }
        
        /* 
         * Using waitVblankStartcb instead of standard waitVblankStart.
         * The 'cb' version allows the thread to be interrupted and prevents
         * hard locks during game loading screens.
         */
        sceDisplayWaitVblankStartCB();
        pach_popup_draw_current();
        
        /* Small delay to hit both framebuffers in a double-buffered game */
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

    /* Let the game initialize memory fully */
    sceKernelDelayThread(5 * 1000 * 1000);

    if (g_game_loaded) {
        pach_popup_show("Achievements", "System Loaded OK");
        log_msg("Showing startup popup");
    }

    while (g_running) {
        pach_popup_update();

        /* WARMUP: Allow Delta variables to cache before evaluating */
        if (warmup_frames < 200) {
            warmup_frames++;
            if (g_game_loaded && g_game_progress && g_game.loaded && g_num_parsed > 0) {
                rc_glue_update(&g_game, g_game_progress, &g_rc_state, g_parsed, g_num_parsed);
            }
            if (warmup_frames == 200) {
                log_msg("Warmup complete. Hits reset.");
                for (int i = 0; i < g_num_parsed; i++) {
                    for (int g = 0; g < g_parsed[i].num_groups; g++) {
                        for (int c = 0; c < g_parsed[i].groups[g].count; c++) {
                            g_parsed[i].groups[g].conds[c].current_hits = 0;
                        }
                    }
                }
            }
            sceKernelDelayThread(16 * 1000);
            continue;
        }

        /* Logic Evaluation (Running every ~80ms to save CPU) */
        static int eval_counter = 0;
        eval_counter++;
        if (eval_counter >= 5) {
            eval_counter = 0;
            if (g_game_loaded && g_game_progress && g_game.loaded && g_num_parsed > 0) {
                RC_EvalResult res = rc_glue_update(&g_game, g_game_progress, &g_rc_state, g_parsed, g_num_parsed);
                if (res.unlocked_index >= 0 && res.unlocked_def) {
                    log_msg("ACHIEVEMENT UNLOCKED!");
                    log_msg(res.unlocked_def->title);
                    pach_popup_show(res.unlocked_def->title, res.unlocked_def->desc);
                    pach_profile_save(&g_profile); /* Save immediately to memory stick */
                }
            }
        }

        /* Debug Popup Trigger */
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(&pad, 1);
        if ((pad.Buttons & PSP_CTRL_LTRIGGER) && (pad.Buttons & PSP_CTRL_RTRIGGER)) {
            pach_popup_show("Debug", "Engine is ACTIVE");
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

    /* Initialize profile logic */
    init_profile();
    
    /* Initialize game logic */
    if (pach_detect_game_code(g_game_code, sizeof(g_game_code))) {
        log_msg(g_game_code);
        if (pach_gamemap_load(&g_mapdb, PACH_GAME_MAP_FILE)) {
            PACH_GameMapEntry *entry = pach_gamemap_find_by_code(&g_mapdb, g_game_code);
            if (entry) {
                char path[128];
                strcpy(path, PACH_GAMES_DIR);
                strcat(path, entry->ach_file);
                if (pach_game_load_file(&g_game, path)) {
                    g_game_progress = pach_profile_get_or_create_game(&g_profile, g_game.header.game_id, g_game.header.num_achievements);
                    rc_glue_init(&g_rc_state);
                    g_num_parsed = rc_glue_parse_all(&g_game, g_parsed, PACH_MAX_GAME_ACH);
                    g_game_loaded = 1;
                    log_msg("Game logic loaded OK");
                }
            }
        }
    }

    /* Start Logic Thread (Normal Priority) */
    g_logic_thid = sceKernelCreateThread("pach_logic", logic_thread_func, 0x30, 0x8000, 0, 0);
    if (g_logic_thid >= 0) sceKernelStartThread(g_logic_thid, 0, 0);

    /* Start Draw Thread (Low Priority) */
    g_draw_thid = sceKernelCreateThread("pach_draw", draw_thread_func, 0x40, 0x4000, 0, 0);
    if (g_draw_thid >= 0) sceKernelStartThread(g_draw_thid, 0, 0);

    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    g_running = 0;
    
    if (g_logic_thid >= 0) { sceKernelWaitThreadEnd(g_logic_thid, NULL); sceKernelDeleteThread(g_logic_thid); }
    if (g_draw_thid >= 0) { sceKernelWaitThreadEnd(g_draw_thid, NULL); sceKernelDeleteThread(g_draw_thid); }
    
    if (g_game_loaded) pach_profile_save(&g_profile);
    log_msg("=== PLUGIN STOP ===");
    return 0;
}