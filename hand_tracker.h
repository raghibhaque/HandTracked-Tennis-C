#ifndef HAND_TRACKER_H
#define HAND_TRACKER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HandTracker HandTracker;

// Initialize camera and hand tracking
HandTracker* hand_tracker_init(void);

// Detect hand center point in current frame
// Returns true if hand detected, fills x and y with coordinates
bool hand_tracker_detect(HandTracker *tracker, float *x, float *y);

// Cleanup and release resources
void hand_tracker_cleanup(HandTracker *tracker);

#ifdef __cplusplus
}
#endif

#endif
