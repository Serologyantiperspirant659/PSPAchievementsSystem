#include <pspkernel.h>
#include <pspaudio.h>
#include <string.h>
#include "beep.h"

/* ============================================================
 * PSP CPU-generated beep using sceAudio output channel.
 *
 * Generates a simple two-tone "achievement chime" using a
 * square wave synthesized directly in a kernel thread.
 * No audio files needed - pure CPU generation.
 *
 * Audio format: 44100 Hz, 16-bit stereo (PSP hardware default)
 * ============================================================ */

#define BEEP_SAMPLE_RATE    44100
#define BEEP_SAMPLES        512     /* One audio block size */
#define BEEP_CHANNEL        0       /* PSP audio output channel 0-7 */

/* Two-tone chime: first note then second note */
#define TONE1_FREQ          880     /* A5 - first note (Hz) */
#define TONE2_FREQ          1109    /* C#6 - second note (Hz) */
#define TONE1_DURATION_MS   120
#define TONE2_DURATION_MS   200
#define BEEP_AMPLITUDE      18000   /* Volume 0..32767 - raised for audibility */

/* Audio buffer: stereo 16-bit = 4 bytes per sample */
static short g_audio_buf[BEEP_SAMPLES * 2];

static volatile int g_beep_requested = 0;
static volatile int g_beep_running   = 0;
static SceUID       g_beep_thid      = -1;
static int          g_channel        = -1;

/* ============================================================
 * Square wave generator
 * freq  - frequency in Hz
 * t     - current sample index (absolute, for phase continuity)
 * rate  - sample rate
 * amp   - amplitude (0..32767)
 * ============================================================ */
static short square_wave(int freq, int t, int rate, int amp)
{
    int period = rate / freq;
    if (period <= 0) return 0;
    int phase = t % period;
    return (phase < period / 2) ? (short)amp : (short)(-amp);
}

/* ============================================================
 * Beep thread: runs the audio output loop
 * ============================================================ */
static int beep_thread_func(SceSize args, void *argp)
{
    (void)args; (void)argp;

    int tone1_samples = (BEEP_SAMPLE_RATE * TONE1_DURATION_MS) / 1000;
    int tone2_samples = (BEEP_SAMPLE_RATE * TONE2_DURATION_MS) / 1000;
    int total_samples = tone1_samples + tone2_samples;

    while (g_beep_running) {
        /* Wait for a beep request */
        if (!g_beep_requested) {
            sceKernelDelayThread(5000); /* Check every 5ms */
            continue;
        }
        g_beep_requested = 0;

        /* Play the chime */
        int t = 0;
        while (t < total_samples) {
            int block_t = 0;
            while (block_t < BEEP_SAMPLES && t < total_samples) {
                int freq = (t < tone1_samples) ? TONE1_FREQ : TONE2_FREQ;
                short s = square_wave(freq, t, BEEP_SAMPLE_RATE, BEEP_AMPLITUDE);
                g_audio_buf[block_t * 2]     = s; /* Left  channel */
                g_audio_buf[block_t * 2 + 1] = s; /* Right channel */
                block_t++;
                t++;
            }
            /* Zero-pad the rest of the last block */
            while (block_t < BEEP_SAMPLES) {
                g_audio_buf[block_t * 2]     = 0;
                g_audio_buf[block_t * 2 + 1] = 0;
                block_t++;
            }
            /* Send block to hardware - blocks until consumed (precise timing) */
            sceAudioOutputBlocking(g_channel, PSP_AUDIO_VOLUME_MAX / 2, g_audio_buf);
        }
        /* One extra silent block to cleanly end the sound */
        memset(g_audio_buf, 0, sizeof(g_audio_buf));
        sceAudioOutputBlocking(g_channel, PSP_AUDIO_VOLUME_MAX / 2, g_audio_buf);
    }

    sceKernelExitDeleteThread(0);
    return 0;
}

/* ============================================================
 * Public API
 * ============================================================ */
int pach_beep_init(void)
{
    /* Reserve PSP audio channel */
    g_channel = sceAudioChReserve(BEEP_CHANNEL, BEEP_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) {
        /* Try any free channel if channel 0 is taken */
        g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BEEP_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
        if (g_channel < 0) return 0;
    }

    memset(g_audio_buf, 0, sizeof(g_audio_buf));
    g_beep_requested = 0;
    g_beep_running   = 1;

    /* Start the dedicated beep thread at very low priority */
    g_beep_thid = sceKernelCreateThread(
        "pach_beep",
        beep_thread_func,
        0x50,           /* Low priority - lower than draw thread (0x40) */
        0x2000,         /* 8KB stack */
        0, 0
    );
    if (g_beep_thid < 0) {
        sceAudioChRelease(g_channel);
        g_channel = -1;
        return 0;
    }

    sceKernelStartThread(g_beep_thid, 0, 0);
    return 1;
}

void pach_beep_play(void)
{
    if (g_channel < 0 || !g_beep_running) return;
    g_beep_requested = 1;
}

void pach_beep_shutdown(void)
{
    g_beep_running   = 0;
    g_beep_requested = 0;

    if (g_beep_thid >= 0) {
        sceKernelWaitThreadEnd(g_beep_thid, NULL);
        g_beep_thid = -1;
    }
    if (g_channel >= 0) {
        sceAudioChRelease(g_channel);
        g_channel = -1;
    }
}