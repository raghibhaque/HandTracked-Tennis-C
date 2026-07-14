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
    SDL_Window *preview_window = nullptr;
    SDL_Renderer *preview_renderer = nullptr;
    SDL_Texture *preview_texture = nullptr;
    int frame_width = 0;
    int frame_height = 0;
    // Last known center used for temporal continuity scoring (not smoothed output).
    cv::Point2f last_center = cv::Point2f(-1.0f, -1.0f);
    bool has_last_center = false;
    int lost_frames = 0;       // frames since last detection
    bool preview_window_open = false;
};

// Primary skin range (hue 0-25 covers fair to medium-dark tones in OpenCV 0-180 scale)
static constexpr int SKIN_H_MIN  = 0;
static constexpr int SKIN_H_MAX  = 25;
static constexpr int SKIN_S_MIN  = 10;
static constexpr int SKIN_S_MAX  = 255;
static constexpr int SKIN_V_MIN  = 50;
static constexpr int SKIN_V_MAX  = 255;
// Second skin range: hue wraps near 180 (red-side skin tones, H 160-180 in OpenCV)
static constexpr int SKIN_H2_MIN = 160;
static constexpr int SKIN_H2_MAX = 180;
static constexpr double MIN_CONTOUR_AREA = 900.0;
// True solidity (area / convex-hull area) — hands are ~0.7+ when fist, lower when spread
static constexpr double MIN_SOLIDITY = 0.45;
static constexpr double MAX_ASPECT_RATIO = 3.0;
// After this many frames with no detection, continuity tracking resets
static constexpr int GRACE_FRAMES = 5;
static const char *PREVIEW_WINDOW_NAME = "Hand Tennis - Camera Preview";

static void update_preview_window(HandTracker *tracker, const cv::Mat &preview_frame) {
    if (!tracker->preview_window_open || !tracker->preview_renderer || !tracker->preview_texture) {
        return;
    }

    cv::cvtColor(preview_frame, tracker->preview_rgb, cv::COLOR_BGR2RGB);

    SDL_UpdateTexture(
        tracker->preview_texture,
        NULL,
        tracker->preview_rgb.data,
        static_cast<int>(tracker->preview_rgb.step)
    );

    SDL_RenderClear(tracker->preview_renderer);
    SDL_RenderCopy(tracker->preview_renderer, tracker->preview_texture, NULL, NULL);
    SDL_RenderPresent(tracker->preview_renderer);
}

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

    tracker->preview_window = SDL_CreateWindow(
        PREVIEW_WINDOW_NAME,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800,
        600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!tracker->preview_window) {
        std::fprintf(stderr, "Warning: Could not create camera preview window: %s\n", SDL_GetError());
    } else {
        tracker->preview_renderer = SDL_CreateRenderer(
            tracker->preview_window,
            -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        );
        if (!tracker->preview_renderer) {
            std::fprintf(stderr, "Warning: Could not create camera preview renderer: %s\n", SDL_GetError());
            SDL_DestroyWindow(tracker->preview_window);
            tracker->preview_window = nullptr;
        } else {
            tracker->preview_texture = SDL_CreateTexture(
                tracker->preview_renderer,
                SDL_PIXELFORMAT_RGB24,
                SDL_TEXTUREACCESS_STREAMING,
                tracker->frame_width,
                tracker->frame_height
            );
            if (!tracker->preview_texture) {
                std::fprintf(stderr, "Warning: Could not create camera preview texture: %s\n", SDL_GetError());
                SDL_DestroyRenderer(tracker->preview_renderer);
                SDL_DestroyWindow(tracker->preview_window);
                tracker->preview_renderer = nullptr;
                tracker->preview_window = nullptr;
            } else {
                tracker->preview_window_open = true;
            }
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

    // Poll preview window events to keep it responsive on Windows
    if (tracker->preview_window_open) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                // User closed preview window — hide it but continue tracking
                if (tracker->preview_texture)  SDL_DestroyTexture(tracker->preview_texture);
                if (tracker->preview_renderer) SDL_DestroyRenderer(tracker->preview_renderer);
                if (tracker->preview_window)   SDL_DestroyWindow(tracker->preview_window);
                tracker->preview_texture  = nullptr;
                tracker->preview_renderer = nullptr;
                tracker->preview_window   = nullptr;
                tracker->preview_window_open = false;
            }
        }
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

    cv::morphologyEx(tracker->mask, tracker->eroded, cv::MORPH_OPEN,  cv::Mat(), cv::Point(-1, -1), 2);
    cv::morphologyEx(tracker->eroded, tracker->dilated, cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(tracker->dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool detected = false;
    double best_score = MIN_CONTOUR_AREA;
    std::vector<cv::Point> best_contour;
    cv::Point2f best_center(-1.0f, -1.0f);

    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area < MIN_CONTOUR_AREA) continue;

        cv::Rect rect = cv::boundingRect(contour);
        if (rect.width <= 0 || rect.height <= 0) continue;

        double aspect_ratio = static_cast<double>(std::max(rect.width, rect.height)) /
                              static_cast<double>(std::min(rect.width, rect.height));
        if (aspect_ratio > MAX_ASPECT_RATIO) continue;

        // True solidity: contour area / convex hull area (filters non-hand blobs)
        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        double hull_area = cv::contourArea(hull);
        double solidity = hull_area > 0.0 ? area / hull_area : 0.0;
        if (solidity < MIN_SOLIDITY) continue;

        // Use image moments for accurate centroid instead of bounding-rect midpoint
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
            score -= std::sqrt(dx * dx + dy * dy) * 2.0;
        }

        if (score > best_score) {
            best_score = score;
            best_contour = contour;
            best_center  = centroid;
            detected = true;
        }
    }

    if (detected) {
        // Output raw centroid — game.c applies its own exponential smoothing.
        // Avoid double-smoothing here.
        *x = (best_center.x / tracker->frame_width)  * 100.0f;
        *y = (best_center.y / tracker->frame_height) * 100.0f;

        tracker->last_center    = best_center;
        tracker->has_last_center = true;
        tracker->lost_frames    = 0;
    } else {
        tracker->lost_frames++;
        if (tracker->lost_frames > GRACE_FRAMES) {
            // Truly lost — reset continuity so stale position doesn't bias next detection
            tracker->has_last_center = false;
        }
    }

    // Update preview window
    cv::Mat preview_frame = tracker->frame.clone();
    if (detected) {
        cv::Rect rect = cv::boundingRect(best_contour);
        cv::rectangle(preview_frame, rect, cv::Scalar(0, 255, 0), 2);
        cv::circle(preview_frame, best_center, 6, cv::Scalar(0, 255, 255), -1);
        cv::putText(preview_frame, "Hand tracked", cv::Point(16, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    } else {
        cv::putText(preview_frame, "Searching for hand...", cv::Point(16, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    }

    update_preview_window(tracker, preview_frame);

    return detected;
}

void hand_tracker_cleanup(HandTracker *tracker) {
    if (!tracker) return;

    if (tracker->capture.isOpened()) {
        tracker->capture.release();
    }

    if (tracker->preview_window_open) {
        if (tracker->preview_texture)  SDL_DestroyTexture(tracker->preview_texture);
        if (tracker->preview_renderer) SDL_DestroyRenderer(tracker->preview_renderer);
        if (tracker->preview_window)   SDL_DestroyWindow(tracker->preview_window);
    }

    delete tracker;
}