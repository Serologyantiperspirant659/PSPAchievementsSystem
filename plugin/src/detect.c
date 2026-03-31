#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <string.h>

#include "detect.h"

static int detect_from_umd_data(char *out_code, int max_len)
{
    SceUID fd;
    char buf[64];
    int read_bytes;
    int i, j;

    fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;

    memset(buf, 0, sizeof(buf));
    read_bytes = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);

    if (read_bytes <= 0)
        return 0;

    buf[read_bytes] = '\0';

    j = 0;
    for (i = 0; i < read_bytes && j < max_len - 1; i++) {
        if (buf[i] == '|' || buf[i] == '\r' || buf[i] == '\n')
            break;
        if (buf[i] != '-')
            out_code[j++] = buf[i];
    }
    out_code[j] = '\0';

    return (j > 0) ? 1 : 0;
}

int pach_detect_game_code(char *out_code, int max_len)
{
    if (!out_code || max_len < 10)
        return 0;

    memset(out_code, 0, max_len);

    /* Try to read game code from UMD disc.
     * Returns 0 if no disc is mounted or file is missing.
     * The caller must handle the 0 case gracefully - do NOT
     * add a fallback here, it causes crashes on unsupported games. */
    return detect_from_umd_data(out_code, max_len);
}