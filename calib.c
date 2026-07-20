#include "calib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #define MKDIR(path) _mkdir(path)
  #define PATH_SEP "\\"
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define MKDIR(path) mkdir((path), 0755)
  #define PATH_SEP "/"
#endif

static char g_path[1024] = {0};

static void ensure_path(void) {
    if (g_path[0]) return;

#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !*home) home = ".";

    char dir[800];
    snprintf(dir, sizeof(dir), "%.500s%s.hand_tennis", home, PATH_SEP);
    MKDIR(dir);  // best-effort; ignore failure (may already exist)
    snprintf(g_path, sizeof(g_path), "%s%scalib.json", dir, PATH_SEP);
}

const char *calib_path(void) {
    ensure_path();
    return g_path;
}

// Locate `"key"`, skip to `:`, read integer. Whitespace-tolerant.
static bool find_int(const char *buf, const char *key, int *out) {
    const char *p = strstr(buf, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    return sscanf(p + 1, " %d", out) == 1;
}

bool calib_load(Calibration *out) {
    ensure_path();
    FILE *f = fopen(g_path, "rb");
    if (!f) return false;

    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    Calibration c = {0};
    if (!find_int(buf, "\"y_lo\"",  &c.y_lo))  return false;
    if (!find_int(buf, "\"cr_lo\"", &c.cr_lo)) return false;
    if (!find_int(buf, "\"cb_lo\"", &c.cb_lo)) return false;
    if (!find_int(buf, "\"y_hi\"",  &c.y_hi))  return false;
    if (!find_int(buf, "\"cr_hi\"", &c.cr_hi)) return false;
    if (!find_int(buf, "\"cb_hi\"", &c.cb_hi)) return false;

    *out = c;
    return true;
}

bool calib_save(const Calibration *in) {
    ensure_path();
    FILE *f = fopen(g_path, "wb");
    if (!f) return false;

    fprintf(f,
            "{\"y_lo\":%d,\"cr_lo\":%d,\"cb_lo\":%d,"
            "\"y_hi\":%d,\"cr_hi\":%d,\"cb_hi\":%d}\n",
            in->y_lo, in->cr_lo, in->cb_lo,
            in->y_hi, in->cr_hi, in->cb_hi);
    fclose(f);
    return true;
}
