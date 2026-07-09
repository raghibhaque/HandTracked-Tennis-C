#include "hand_tracker.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

struct HandTracker {
    cv::VideoCapture capture;
    cv::Mat frame;
    cv::Mat hsv;
    cv::Mat mask;
    cv::Mat eroded;
    cv::Mat dilated;
    int frame_width = 0;
    int frame_height = 0;
    cv::Point2f smoothed_center = cv::Point2f(-1.0f, -1.0f);
    bool has_smoothed_center = false;
    bool preview_window_open = false;
};

static constexpr int SKIN_H_MIN = 0;
static constexpr int SKIN_H_MAX = 20;
static constexpr int SKIN_S_MIN = 10;
static constexpr int SKIN_S_MAX = 255;
static constexpr int SKIN_V_MIN = 60;
static constexpr int SKIN_V_MAX = 255;
static constexpr double MIN_CONTOUR_AREA = 900.0;
static constexpr double MIN_SOLIDITY = 0.55;
static constexpr double MAX_ASPECT_RATIO = 2.8;
static const char *PREVIEW_WINDOW_NAME = "Hand Tennis - Camera Preview";

static bool open_camera_index(HandTracker *tracker, int camera_index) {
    if (!tracker->capture.open(camera_index, cv::CAP_ANY)) {
        return false;
    }

    tracker->capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    tracker->capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    tracker->capture.set(cv::CAP_PROP_FPS, 30);
    tracker->capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

    for (int attempt = 0; attempt < 50; ++attempt) {
        tracker->capture.read(tracker->frame);
        if (!tracker->frame.empty()) {
            tracker->frame_width = tracker->frame.cols;
            tracker->frame_height = tracker->frame.rows;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    tracker->capture.release();
    return false;
}

HandTracker* hand_tracker_init(void) {
    HandTracker *tracker = new HandTracker();

    const char *camera_index_env = std::getenv("HAND_TENNIS_CAMERA_INDEX");
    if (camera_index_env && *camera_index_env) {
        int camera_index = std::atoi(camera_index_env);
        if (open_camera_index(tracker, camera_index)) {
            std::printf("Using camera index %d (HAND_TENNIS_CAMERA_INDEX)\n", camera_index);
        }
    }

    if (!tracker->capture.isOpened()) {
        for (int camera_index = 0; camera_index < 10; ++camera_index) {
            if (open_camera_index(tracker, camera_index)) {
                std::printf("Using camera index %d\n", camera_index);
                break;
            }
        }
    }

    if (!tracker->capture.isOpened()) {
        std::fprintf(stderr, "Error: Could not open any camera index 0-9\n");
        delete tracker;
        return nullptr;
    }

    cv::namedWindow(PREVIEW_WINDOW_NAME, cv::WINDOW_NORMAL);
    cv::resizeWindow(PREVIEW_WINDOW_NAME, 800, 600);
    tracker->preview_window_open = true;

    std::printf("Hand tracker initialized: %dx%d\n", tracker->frame_width, tracker->frame_height);
    return tracker;
}

bool hand_tracker_detect(HandTracker *tracker, float *x, float *y) {
    if (!tracker || !tracker->capture.isOpened()) return false;

    if (!tracker->capture.read(tracker->frame) || tracker->frame.empty()) {
        return false;
    }

    cv::Mat blurred;
    cv::GaussianBlur(tracker->frame, blurred, cv::Size(7, 7), 0.0);
    cv::cvtColor(blurred, tracker->hsv, cv::COLOR_BGR2HSV);

    const cv::Scalar lower(SKIN_H_MIN, SKIN_S_MIN, SKIN_V_MIN);
    const cv::Scalar upper(SKIN_H_MAX, SKIN_S_MAX, SKIN_V_MAX);
    cv::inRange(tracker->hsv, lower, upper, tracker->mask);

    cv::morphologyEx(tracker->mask, tracker->eroded, cv::MORPH_OPEN, cv::Mat(), cv::Point(-1, -1), 2);
    cv::morphologyEx(tracker->eroded, tracker->dilated, cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(tracker->dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool detected = false;
    double best_score = MIN_CONTOUR_AREA;
    std::vector<cv::Point> largest_contour;
    cv::Point2f tracked_center = tracker->has_smoothed_center ? tracker->smoothed_center : cv::Point2f(-1.0f, -1.0f);

    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area < MIN_CONTOUR_AREA) {
            continue;
        }

        cv::Rect rect = cv::boundingRect(contour);
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }

        double aspect_ratio = static_cast<double>(std::max(rect.width, rect.height)) /
                              static_cast<double>(std::min(rect.width, rect.height));
        if (aspect_ratio > MAX_ASPECT_RATIO) {
            continue;
        }

        double rect_area = static_cast<double>(rect.area());
        double solidity = rect_area > 0.0 ? area / rect_area : 0.0;
        if (solidity < MIN_SOLIDITY) {
            continue;
        }

        cv::Point2f contour_center(rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);
        double score = area;
        if (tracker->has_smoothed_center) {
            double dx = contour_center.x - tracked_center.x;
            double dy = contour_center.y - tracked_center.y;
            score -= std::sqrt(dx * dx + dy * dy) * 2.0;
        }

        if (score > best_score) {
            best_score = score;
            largest_contour = contour;
            detected = true;
            tracked_center = contour_center;
        }
    }

    if (detected) {
        cv::Rect rect = cv::boundingRect(largest_contour);
        cv::Point2f center(rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);

        if (tracker->has_smoothed_center) {
            tracker->smoothed_center.x = tracker->smoothed_center.x * 0.7f + center.x * 0.3f;
            tracker->smoothed_center.y = tracker->smoothed_center.y * 0.7f + center.y * 0.3f;
        } else {
            tracker->smoothed_center = center;
            tracker->has_smoothed_center = true;
        }

        *x = (tracker->smoothed_center.x / tracker->frame_width) * 100.0f;
        *y = (tracker->smoothed_center.y / tracker->frame_height) * 100.0f;
    } else {
        tracker->has_smoothed_center = false;
    }

    cv::Mat preview_frame = tracker->frame.clone();
    if (detected) {
        cv::Rect rect = cv::boundingRect(largest_contour);
        cv::rectangle(preview_frame, rect, cv::Scalar(0, 255, 0), 2);
        cv::circle(preview_frame, tracker->smoothed_center, 6, cv::Scalar(0, 255, 255), -1);
        cv::putText(preview_frame, "Hand tracked", cv::Point(16, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    } else {
        cv::putText(preview_frame, "Searching for hand...", cv::Point(16, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }

    cv::imshow(PREVIEW_WINDOW_NAME, preview_frame);
    cv::waitKey(1);

    return detected;
}

void hand_tracker_cleanup(HandTracker *tracker) {
    if (!tracker) return;

    if (tracker->capture.isOpened()) {
        tracker->capture.release();
    }

    if (tracker->preview_window_open) {
        cv::destroyWindow(PREVIEW_WINDOW_NAME);
    }

    delete tracker;
}