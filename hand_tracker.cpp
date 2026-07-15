#include "hand_tracker.h"

#include <opencv2/opencv.hpp>

#include <SDL2/SDL.h>

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
    cv::Mat mask2;
    cv::Mat eroded;
    cv::Mat dilated;
    cv::Mat preview_rgb;
    SDL_Renderer *main_renderer = nullptr;
    SDL_Texture  *preview_texture = nullptr;
    int frame_width = 0;
    int frame_height = 0;
    cv::Point2f last_center = cv::Point2f(-1.0f, -1.0f);
    bool has_last_center = false;
    int lost_frames = 0;
};

// Primary skin range (hue 0-25 covers fair to medium-dark tones in OpenCV 0-180 scale)
static constexpr int SKIN_H_MIN  = 0;
static constexpr int SKIN_H_MAX  = 25;
static constexpr int SKIN_S_MIN  = 10;
static constexpr int SKIN_S_MAX  = 255;
static constexpr int SKIN_V_MIN  = 50;
static constexpr int SKIN_V_MAX  = 255;
// Second skin range: hue wraps near 180 (red-side skin tones)
static constexpr int SKIN_H2_MIN = 160;
static constexpr int SKIN_H2_MAX = 180;
static constexpr double MIN_CONTOUR_AREA = 900.0;
static constexpr double MAX_CONTOUR_AREA = 35000.0;
// True solidity (area / convex-hull area)
static constexpr double MIN_SOLIDITY = 0.45;
static constexpr double MAX_ASPECT_RATIO = 3.0;
// Distance penalty weight for temporal continuity
static constexpr double DISTANCE_PENALTY_WEIGHT = 15.0;
// Frames of no detection before continuity resets
static constexpr int GRACE_FRAMES = 5;

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
            tracker->frame_width  = tracker->frame.cols;
            tracker->frame_height = tracker->frame.rows;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    tracker->capture.release();
    return false;
}

HandTracker* hand_tracker_init(SDL_Renderer *renderer) {
    HandTracker *tracker = new HandTracker();
    tracker->main_renderer = renderer;

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

    // Create the preview texture using the main game renderer so it can be
    // drawn directly into the game window without a texture ownership transfer.
    if (renderer) {
        tracker->preview_texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            tracker->frame_width,
            tracker->frame_height
        );
        if (!tracker->preview_texture) {
            std::fprintf(stderr, "Warning: Could not create preview texture: %s\n", SDL_GetError());
        }
    }

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

    // Primary skin range (H 0-25)
    cv::inRange(tracker->hsv,
                cv::Scalar(SKIN_H_MIN, SKIN_S_MIN, SKIN_V_MIN),
                cv::Scalar(SKIN_H_MAX, SKIN_S_MAX, SKIN_V_MAX),
                tracker->mask);

    // Second skin range (H 160-180, red-side wrap-around for darker/reddish tones)
    cv::inRange(tracker->hsv,
                cv::Scalar(SKIN_H2_MIN, SKIN_S_MIN, SKIN_V_MIN),
                cv::Scalar(SKIN_H2_MAX, SKIN_S_MAX, SKIN_V_MAX),
                tracker->mask2);

    cv::bitwise_or(tracker->mask, tracker->mask2, tracker->mask);

    cv::morphologyEx(tracker->mask,   tracker->eroded,  cv::MORPH_OPEN,  cv::Mat(), cv::Point(-1, -1), 2);
    cv::morphologyEx(tracker->eroded, tracker->dilated, cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(tracker->dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool detected = false;
    double best_score = MIN_CONTOUR_AREA;
    std::vector<cv::Point> best_contour;
    cv::Point2f best_center(-1.0f, -1.0f);

    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area < MIN_CONTOUR_AREA || area > MAX_CONTOUR_AREA) continue;

        cv::Rect rect = cv::boundingRect(contour);
        if (rect.width <= 0 || rect.height <= 0) continue;

        double aspect_ratio = static_cast<double>(std::max(rect.width, rect.height)) /
                              static_cast<double>(std::min(rect.width, rect.height));
        if (aspect_ratio > MAX_ASPECT_RATIO) continue;

        // True solidity: contour area / convex hull area
        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        double hull_area = cv::contourArea(hull);
        double solidity = hull_area > 0.0 ? area / hull_area : 0.0;
        if (solidity < MIN_SOLIDITY) continue;

        // Accurate centroid via image moments
        cv::Moments m = cv::moments(contour);
        cv::Point2f centroid(-1.0f, -1.0f);
        if (m.m00 > 0.0) {
            centroid = cv::Point2f(static_cast<float>(m.m10 / m.m00),
                                   static_cast<float>(m.m01 / m.m00));
        } else {
            centroid = cv::Point2f(rect.x + rect.width / 2.0f,
                                   rect.y + rect.height / 2.0f);
        }

        // Prefer contour nearest to last known position (temporal continuity)
        double score = area;
        if (tracker->has_last_center) {
            double dx = centroid.x - tracker->last_center.x;
            double dy = centroid.y - tracker->last_center.y;
            score -= std::sqrt(dx * dx + dy * dy) * DISTANCE_PENALTY_WEIGHT;
        }

        if (score > best_score) {
            best_score   = score;
            best_contour = contour;
            best_center  = centroid;
            detected     = true;
        }
    }

    cv::Point2f palm_center(-1.0f, -1.0f);

    if (detected) {
        // Distance transform on filled contour mask: the pixel with the maximum
        // distance to any boundary is always inside the palm, never in a finger.
        cv::Mat palm_mask = cv::Mat::zeros(tracker->dilated.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> tmp = {best_contour};
        cv::drawContours(palm_mask, tmp, 0, 255, cv::FILLED);
        cv::Mat dist;
        cv::distanceTransform(palm_mask, dist, cv::DIST_L2, 5);
        cv::Point max_loc;
        cv::minMaxLoc(dist, nullptr, nullptr, nullptr, &max_loc);
        palm_center = cv::Point2f(static_cast<float>(max_loc.x),
                                  static_cast<float>(max_loc.y));

        *x = (palm_center.x / tracker->frame_width)  * 100.0f;
        *y = (palm_center.y / tracker->frame_height) * 100.0f;

        tracker->last_center     = palm_center;
        tracker->has_last_center = true;
        tracker->lost_frames     = 0;
    } else {
        tracker->lost_frames++;
        if (tracker->lost_frames > GRACE_FRAMES) {
            tracker->has_last_center = false;
        }
    }

    // Build annotated preview frame and push to SDL texture
    if (tracker->preview_texture) {
        cv::Mat preview_frame = tracker->frame.clone();
        if (detected) {
            cv::Rect rect = cv::boundingRect(best_contour);
            cv::rectangle(preview_frame, rect, cv::Scalar(0, 255, 0), 2);
            cv::circle(preview_frame, palm_center, 8, cv::Scalar(0, 255, 255), -1);
            cv::putText(preview_frame, "Palm tracked", cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        } else {
            cv::putText(preview_frame, "Searching...", cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 80, 255), 2, cv::LINE_AA);
        }

        cv::cvtColor(preview_frame, tracker->preview_rgb, cv::COLOR_BGR2RGB);
        SDL_UpdateTexture(
            tracker->preview_texture,
            NULL,
            tracker->preview_rgb.data,
            static_cast<int>(tracker->preview_rgb.step)
        );
    }

    return detected;
}

SDL_Texture* hand_tracker_get_preview_texture(HandTracker *tracker) {
    return tracker ? tracker->preview_texture : nullptr;
}

void hand_tracker_cleanup(HandTracker *tracker) {
    if (!tracker) return;

    if (tracker->capture.isOpened()) {
        tracker->capture.release();
    }

    if (tracker->preview_texture) {
        SDL_DestroyTexture(tracker->preview_texture);
    }

    delete tracker;
}
