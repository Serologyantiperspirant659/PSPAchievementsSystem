#include <pspkernel.h>
#include <pspaudio.h>
#include <string.h>
#include "beep.h"

/* ============================================================
 * Achievement unlock beep — kernel-mode safe.
 *
 * Uses libpspaudio_driver which provides kernel-callable
 * versions of sceAudioChReserve / sceAudioOutputBlocking /
 * sceAudioChRelease.
 *
 * Generates a short two-tone chime (~880Hz + ~1047Hz)
 * played in a dedicated thread to avoid blocking the caller.
 * ============================================================ */

#define BEEP_SAMPLE_RATE   44100
#define BEEP_SAMPLES       1024   /* per output call — PSP requires 64-aligned */
#define BEEP_DURATION_MS   180    /* total beep length */
#define BEEP_VOLUME        0x6000 /* 75% volume */

/* Simple 16-bit stereo sine approximation using integer math.
 * We avoid libm/floats — use a small lookup table instead. */
static const short sine_table[16] = {
    0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
    0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
};

static volatile int g_beep_channel = -1;
static volatile int g_beep_pending = 0;
static SceUID       g_beep_thid    = -1;
static volatile int g_beep_running = 0;

/* Generate a tone by stepping through the 16-entry sine table.
 * phase_acc and phase_step are in 16.16 fixed point.
 * Fills interleaved stereo buffer (L, R, L, R, ...). */
static void gen_tone(short *buf, int num_samples,
                     unsigned int *phase_acc, unsigned int phase_step,
                     int volume)
{
    int i;
    for (i = 0; i < num_samples; i++) {
        int idx = (*phase_acc >> 16) & 0xF;   /* 0-15 */
        int val = (sine_table[idx] * volume) >> 15;
        short s = (short)val;
        buf[i * 2]     = s;  /* L */
        buf[i * 2 + 1] = s;  /* R */
        *phase_acc += phase_step;
    }
}

static int beep_thread_func(SceSize args, void *argp)
{
    (void)args; (void)argp;
    short buf[BEEP_SAMPLES * 2];  /* stereo */

    while (g_beep_running) {
        if (!g_beep_pending) {
            sceKernelDelayThread(50 * 1000);  /* 50ms idle poll */
            continue;
        }
        g_beep_pending = 0;

        /* Reserve audio channel */
        int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                   BEEP_SAMPLES,
                                   PSP_AUDIO_FORMAT_STEREO);
        if (ch < 0) continue;

        /* Tone 1: ~880 Hz for 100ms  (A5 note) */
        /* phase_step = (freq / SAMPLE_RATE) * 16 * 65536 */
        /* 880 / 44100 * 16 * 65536 = ~20971 */
        {
            unsigned int phase = 0;
            unsigned int step = (880u * 16u * 65536u) / BEEP_SAMPLE_RATE;
            int total_samples = (BEEP_SAMPLE_RATE * 100) / 1000; /* 100ms */
            int remaining = total_samples;

            while (remaining > 0) {
                int count = (remaining > BEEP_SAMPLES) ? BEEP_SAMPLES : remaining;
                memset(buf, 0, sizeof(buf));
                gen_tone(buf, count, &phase, step, BEEP_VOLUME);
                sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
                remaining -= count;
            }
        }

        /* Tone 2: ~1047 Hz for 80ms  (C6 note) */
        {
            unsigned int phase = 0;
            unsigned int step = (1047u * 16u * 65536u) / BEEP_SAMPLE_RATE;
            int total_samples = (BEEP_SAMPLE_RATE * 80) / 1000; /* 80ms */
            int remaining = total_samples;

            while (remaining > 0) {
                int count = (remaining > BEEP_SAMPLES) ? BEEP_SAMPLES : remaining;
                memset(buf, 0, sizeof(buf));
                gen_tone(buf, count, &phase, step, BEEP_VOLUME);
                sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
                remaining -= count;
            }
        }

        sceAudioChRelease(ch);
    }

    sceKernelExitDeleteThread(0);
    return 0;
}

int pach_beep_init(void)
{
    g_beep_running = 1;
    g_beep_pending = 0;
    g_beep_thid = sceKernelCreateThread("pach_beep",
                                         beep_thread_func,
                                         0x30,    /* same priority as logic */
                                         0x2000,  /* 8KB stack */
                                         0, 0);
    if (g_beep_thid < 0) return 0;
    sceKernelStartThread(g_beep_thid, 0, NULL);
    return 1;
}

void pach_beep_play(void)
{
    g_beep_pending = 1;
}

void pach_beep_shutdown(void)
{
    g_beep_running = 0;
    if (g_beep_thid >= 0) {
        sceKernelWaitThreadEnd(g_beep_thid, NULL);
        g_beep_thid = -1;
    }
}