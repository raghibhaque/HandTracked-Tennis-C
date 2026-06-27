#ifndef HAND_TRACKER_H
#define HAND_TRACKER_H

#include <opencv2/core/version.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/videoio/videoio_c.h>
#include <opencv2/highgui/highgui_c.h>
#include <stdbool.h>

typedef struct {
    CvCapture *capture;
    IplImage *frame;
    IplImage *hsv;
    IplImage *mask;
    IplImage *eroded;
    IplImage *dilated;
    int frame_width;
    int frame_height;
} HandTracker;

// Initialize camera and hand tracking
HandTracker* hand_tracker_init(void);

// Detect hand center point in current frame
// Returns true if hand detected, fills x and y with coordinates
bool hand_tracker_detect(HandTracker *tracker, float *x, float *y);

// Get current camera frame (for display)
IplImage* hand_tracker_get_frame(HandTracker *tracker);

// Cleanup and release resources
void hand_tracker_cleanup(HandTracker *tracker);

#endif
