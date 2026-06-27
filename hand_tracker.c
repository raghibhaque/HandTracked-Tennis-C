#include "hand_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Skin color detection thresholds (HSV space)
#define SKIN_H_MIN 0
#define SKIN_H_MAX 20
#define SKIN_S_MIN 10
#define SKIN_S_MAX 255
#define SKIN_V_MIN 60
#define SKIN_V_MAX 255

HandTracker* hand_tracker_init(void) {
    HandTracker *tracker = (HandTracker *)malloc(sizeof(HandTracker));
    
    // Initialize video capture from default camera
    tracker->capture = cvCaptureFromCAM(0);
    if (!tracker->capture) {
        fprintf(stderr, "Error: Could not open camera\n");
        free(tracker);
        return NULL;
    }
    
    // Set camera properties for better performance
    cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_WIDTH, 640);
    cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_HEIGHT, 480);
    cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FPS, 30);
    
    // Capture one frame to get dimensions
    tracker->frame = cvQueryFrame(tracker->capture);
    if (!tracker->frame) {
        fprintf(stderr, "Error: Could not capture initial frame\n");
        cvReleaseCapture(&tracker->capture);
        free(tracker);
        return NULL;
    }
    
    tracker->frame_width = tracker->frame->width;
    tracker->frame_height = tracker->frame->height;
    
    // Allocate working images
    tracker->hsv = cvCreateImage(cvGetSize(tracker->frame), 8, 3);
    tracker->mask = cvCreateImage(cvGetSize(tracker->frame), 8, 1);
    tracker->eroded = cvCreateImage(cvGetSize(tracker->frame), 8, 1);
    tracker->dilated = cvCreateImage(cvGetSize(tracker->frame), 8, 1);
    
    printf("Hand tracker initialized: %dx%d\n", tracker->frame_width, tracker->frame_height);
    
    return tracker;
}

bool hand_tracker_detect(HandTracker *tracker, float *x, float *y) {
    if (!tracker || !tracker->capture) return false;
    
    // Capture new frame
    tracker->frame = cvQueryFrame(tracker->capture);
    if (!tracker->frame) return false;
    
    // Convert BGR to HSV
    cvCvtColor(tracker->frame, tracker->hsv, CV_BGR2HSV);
    
    // Create binary mask for skin color
    CvScalar lower = cvScalar(SKIN_H_MIN, SKIN_S_MIN, SKIN_V_MIN, 0);
    CvScalar upper = cvScalar(SKIN_H_MAX, SKIN_S_MAX, SKIN_V_MAX, 0);
    cvInRangeS(tracker->hsv, lower, upper, tracker->mask);
    
    // Morphological operations to clean up mask
    cvErode(tracker->mask, tracker->eroded, NULL, 2);
    cvDilate(tracker->eroded, tracker->dilated, NULL, 2);
    
    // Find contours
    CvMemStorage *storage = cvCreateMemStorage(0);
    CvSeq *contours = NULL;
    cvFindContours(tracker->dilated, storage, &contours, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));
    
    bool detected = false;
    double max_area = 500;  // Minimum contour area
    CvSeq *largest_contour = NULL;
    
    // Find largest contour (likely the hand)
    for (CvSeq *seq = contours; seq; seq = seq->h_next) {
        double area = cvContourArea(seq, cvWholeSeq(seq), 0);
        if (area > max_area) {
            max_area = area;
            largest_contour = seq;
            detected = true;
        }
    }
    
    if (detected && largest_contour) {
        // Get bounding rect of hand
        CvRect rect = cvBoundingRect(largest_contour, 0);
        
        // Calculate center point
        *x = rect.x + rect.width / 2.0f;
        *y = rect.y + rect.height / 2.0f;
        
        // Normalize to game screen coordinates (640x480 camera -> game screen)
        // Map camera center point to paddle area (right side of court)
        *x = (*x / tracker->frame_width) * 100;  // Will be mapped in game logic
        *y = (*y / tracker->frame_height) * 100;
    }
    
    cvReleaseMemStorage(&storage);
    
    return detected;
}

IplImage* hand_tracker_get_frame(HandTracker *tracker) {
    return tracker ? tracker->frame : NULL;
}

void hand_tracker_cleanup(HandTracker *tracker) {
    if (!tracker) return;
    
    if (tracker->hsv) cvReleaseImage(&tracker->hsv);
    if (tracker->mask) cvReleaseImage(&tracker->mask);
    if (tracker->eroded) cvReleaseImage(&tracker->eroded);
    if (tracker->dilated) cvReleaseImage(&tracker->dilated);
    if (tracker->capture) cvReleaseCapture(&tracker->capture);
    
    free(tracker);
}
