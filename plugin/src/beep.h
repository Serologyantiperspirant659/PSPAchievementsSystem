#ifndef BEEP_H
#define BEEP_H

/* Initialize the audio channel for beep output.
 * Call once at startup (from logic thread, after pspkernel is ready).
 * Returns 1 on success, 0 on failure. */
int  pach_beep_init(void);

/* Play a short achievement unlock beep (non-blocking, async).
 * Safe to call from logic thread. */
void pach_beep_play(void);

/* Release the audio channel. Call from module_stop(). */
void pach_beep_shutdown(void);

#endif /* BEEP_H */