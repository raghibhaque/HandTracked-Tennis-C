#ifndef CALIB_H
#define CALIB_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int y_lo, cr_lo, cb_lo;
    int y_hi, cr_hi, cb_hi;
} Calibration;

// Load calibration from ~/.hand_tennis/calib.json (or %USERPROFILE%\.hand_tennis on Windows).
// Returns true and fills out on success.
bool calib_load(Calibration *out);

// Persist calibration; creates the directory if missing.
bool calib_save(const Calibration *in);

// Absolute path to the calibration file (valid until process exit).
const char *calib_path(void);

#ifdef __cplusplus
}
#endif

#endif
