#ifndef HAND_TRACKER_H
#define HAND_TRACKER_H

#include <stdbool.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HandTracker HandTracker;

// Initialize camera and hand tracking.
// renderer: the main game SDL_Renderer used to create the embedded preview texture.
HandTracker* hand_tracker_init(SDL_Renderer *renderer);

// Detect hand in current frame. Returns true if detected.
// x, y: normalized 0-100 coords of hand center.
bool hand_tracker_detect(HandTracker *tracker, float *x, float *y);

// Get the camera preview SDL_Texture (updated each detect call).
// The tracker owns this texture — do NOT destroy it.
SDL_Texture* hand_tracker_get_preview_texture(HandTracker *tracker);

// Force recalibration of the static skin-color background and adaptive skin
// model on the next N frames. Safe to call from any thread but the main
// tracker thread; expected to be called from the SDL event loop.
void hand_tracker_recalibrate(HandTracker *tracker);

// Load persisted YCrCb calibration bounds from disk if available.
// Called automatically by hand_tracker_init(); exposed for manual reloads.
bool hand_tracker_load_calibration(HandTracker *tracker);

// Save current adaptive YCrCb bounds to disk. No-op if calibration is not ready.
bool hand_tracker_save_calibration(HandTracker *tracker);

// Release all resources.
void hand_tracker_cleanup(HandTracker *tracker);

#ifdef __cplusplus
}
#endif

#endif
