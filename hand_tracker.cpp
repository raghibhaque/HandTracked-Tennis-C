#include "hand_tracker.h"

#include <opencv2/opencv.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/video/tracking.hpp>

#include <SDL2/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

struct HandTracker {
    cv::VideoCapture capture;
    cv::Mat frame;
    cv::Mat blurred;
    cv::Mat ycrcb;
    cv::Mat skin_mask;
    cv::Mat motion_mask;
    cv::Mat combined_mask;
    cv::Mat preview_rgb;
    cv::Ptr<cv::BackgroundSubtractorMOG2> bg_sub;
    cv::KalmanFilter kf;
    bool kalman_initialized = false;

    SDL_Renderer *main_renderer = nullptr;
    SDL_Texture  *preview_texture = nullptr;
    int frame_width = 0;
    int frame_height = 0;

    cv::Point2f palm_center = cv::Point2f(-1.0f, -1.0f);
    float palm_radius = 0.0f;
    bool  has_palm = false;
    int   lost_frames = 0;
};

// YCrCb skin bounds — Chai & Ngan (1999), robust across lighting and skin tones
// because luma (Y) is decoupled from chroma (Cr, Cb).
static constexpr int SKIN_CR_MIN = 133;
static constexpr int SKIN_CR_MAX = 173;
static constexpr int SKIN_CB_MIN = 77;
static constexpr int SKIN_CB_MAX = 127;

static constexpr double MIN_CONTOUR_AREA   = 1500.0;
static constexpr double MAX_CONTOUR_AREA   = 90000.0;
static constexpr double MIN_SOLIDITY       = 0.55;
static constexpr double MAX_ASPECT_RATIO   = 3.0;
static constexpr float  MIN_PALM_RADIUS_PX = 14.0f;   // reject fingers-only / arm-only blobs
static constexpr int    GRACE_FRAMES       = 8;
static constexpr double DIST_PENALTY       = 6.0;    // per-pixel penalty vs Kalman prediction
static constexpr double MOTION_BONUS       = 2.5;    // scoring bonus for motion overlap
static constexpr float  SEARCH_RADIUS_MULT = 5.0f;   // ROI = k * palm radius
static constexpr float  MAX_JUMP_MULT      = 6.0f;   // reject palm jumps > k * palm radius

static bool open_camera_index(HandTracker *tracker, int camera_index) {
    if (!tracker->capture.open(camera_index, cv::CAP_ANY)) {
        return false;
    }

    tracker->capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    tracker->capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    tracker->capture.set(cv::CAP_PROP_FPS, 30);
    tracker->capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
    tracker->capture.set(cv::CAP_PROP_FOURCC,
                        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

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

// Constant-velocity 2D Kalman: state [x, y, vx, vy], measurement [x, y].
static void init_kalman(HandTracker *t, const cv::Point2f &seed) {
    t->kf.init(4, 2, 0, CV_32F);

    t->kf.transitionMatrix = (cv::Mat_<float>(4, 4) <<
        1, 0, 1, 0,
        0, 1, 0, 1,
        0, 0, 1, 0,
        0, 0, 0, 1);

    cv::setIdentity(t->kf.measurementMatrix);
    cv::setIdentity(t->kf.processNoiseCov,     cv::Scalar::all(1e-2));
    cv::setIdentity(t->kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    cv::setIdentity(t->kf.errorCovPost,        cv::Scalar::all(1.0f));

    t->kf.statePost.at<float>(0) = seed.x;
    t->kf.statePost.at<float>(1) = seed.y;
    t->kf.statePost.at<float>(2) = 0.0f;
    t->kf.statePost.at<float>(3) = 0.0f;

    t->kalman_initialized = true;
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

    // MOG2 with shadow detection off — we only need a binary motion mask.
    tracker->bg_sub = cv::createBackgroundSubtractorMOG2(500, 25.0, false);

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

    std::printf("Hand tracker initialized: %dx%d (YCrCb + MOG2 + Kalman)\n",
                tracker->frame_width, tracker->frame_height);
    return tracker;
}

bool hand_tracker_detect(HandTracker *tracker, float *x, float *y) {
    if (!tracker || !tracker->capture.isOpened()) return false;

    if (!tracker->capture.read(tracker->frame) || tracker->frame.empty()) {
        return false;
    }

    const int W = tracker->frame_width;
    const int H = tracker->frame_height;

    // Kalman prediction (defines ROI when we already have a lock)
    cv::Point2f predicted(-1.0f, -1.0f);
    if (tracker->kalman_initialized) {
        cv::Mat p = tracker->kf.predict();
        predicted = cv::Point2f(p.at<float>(0), p.at<float>(1));
    }

    // 1. Skin mask in YCrCb (robust to lighting shifts)
    cv::GaussianBlur(tracker->frame, tracker->blurred, cv::Size(5, 5), 0.0);
    cv::cvtColor(tracker->blurred, tracker->ycrcb, cv::COLOR_BGR2YCrCb);
    cv::inRange(tracker->ycrcb,
                cv::Scalar(0,   SKIN_CR_MIN, SKIN_CB_MIN),
                cv::Scalar(255, SKIN_CR_MAX, SKIN_CB_MAX),
                tracker->skin_mask);

    // 2. Motion mask via MOG2 — used only as scoring bonus, so a still hand
    //    is not lost when it pauses.
    tracker->bg_sub->apply(tracker->blurred, tracker->motion_mask, 0.005);
    cv::threshold(tracker->motion_mask, tracker->motion_mask, 200, 255, cv::THRESH_BINARY);

    // 3. Clean skin mask
    cv::morphologyEx(tracker->skin_mask, tracker->skin_mask,
                     cv::MORPH_OPEN,  cv::Mat(), cv::Point(-1, -1), 2);
    cv::morphologyEx(tracker->skin_mask, tracker->skin_mask,
                     cv::MORPH_CLOSE, cv::Mat(), cv::Point(-1, -1), 3);

    // 4. ROI gating: when locked, only search near Kalman prediction.
    cv::Mat search_mask = tracker->skin_mask;
    if (tracker->has_palm && tracker->kalman_initialized) {
        float r = std::max(tracker->palm_radius * SEARCH_RADIUS_MULT, 80.0f);
        cv::Mat roi_mask = cv::Mat::zeros(tracker->skin_mask.size(), CV_8UC1);
        cv::circle(roi_mask, predicted, static_cast<int>(r), 255, cv::FILLED);
        cv::bitwise_and(tracker->skin_mask, roi_mask, tracker->combined_mask);
        search_mask = tracker->combined_mask;
    }

    // 5. Contour scoring
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(search_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool detected = false;
    double best_score = -1e18;
    std::vector<cv::Point> best_contour;

    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area < MIN_CONTOUR_AREA || area > MAX_CONTOUR_AREA) continue;

        cv::Rect rect = cv::boundingRect(contour);
        if (rect.width <= 0 || rect.height <= 0) continue;

        double aspect = static_cast<double>(std::max(rect.width, rect.height)) /
                        static_cast<double>(std::min(rect.width, rect.height));
        if (aspect > MAX_ASPECT_RATIO) continue;

        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        double hull_area = cv::contourArea(hull);
        double solidity  = hull_area > 0.0 ? area / hull_area : 0.0;
        if (solidity < MIN_SOLIDITY) continue;

        cv::Moments m = cv::moments(contour);
        cv::Point2f centroid = (m.m00 > 0.0)
            ? cv::Point2f(static_cast<float>(m.m10 / m.m00),
                          static_cast<float>(m.m01 / m.m00))
            : cv::Point2f(rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);

        // Motion overlap ratio: fraction of contour pixels that moved this frame.
        cv::Mat contour_mask = cv::Mat::zeros(tracker->skin_mask.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> tmp = {contour};
        cv::drawContours(contour_mask, tmp, 0, 255, cv::FILLED);
        cv::Mat motion_hit;
        cv::bitwise_and(contour_mask, tracker->motion_mask, motion_hit);
        double motion_ratio = static_cast<double>(cv::countNonZero(motion_hit)) / area;

        double score = area * (1.0 + MOTION_BONUS * motion_ratio);
        if (tracker->kalman_initialized) {
            double dx = centroid.x - predicted.x;
            double dy = centroid.y - predicted.y;
            score -= std::sqrt(dx * dx + dy * dy) * DIST_PENALTY;
        }

        if (score > best_score) {
            best_score   = score;
            best_contour = contour;
            detected     = true;
        }
    }

    // 6. Palm center via distance transform: pixel with max distance to any
    //    boundary is always inside the palm, never in a finger.
    cv::Point2f palm_center(-1.0f, -1.0f);
    float palm_radius = 0.0f;

    if (detected) {
        cv::Mat palm_mask = cv::Mat::zeros(tracker->skin_mask.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> tmp = {best_contour};
        cv::drawContours(palm_mask, tmp, 0, 255, cv::FILLED);

        cv::Mat dist;
        cv::distanceTransform(palm_mask, dist, cv::DIST_L2, 5);

        cv::Point max_loc;
        double max_val = 0.0;
        cv::minMaxLoc(dist, nullptr, &max_val, nullptr, &max_loc);

        palm_center = cv::Point2f(static_cast<float>(max_loc.x),
                                  static_cast<float>(max_loc.y));
        palm_radius = static_cast<float>(max_val);

        // Reject non-palm blobs (fingers-only, arm-only)
        if (palm_radius < MIN_PALM_RADIUS_PX) {
            detected = false;
        }

        // Reject implausible jumps once locked
        if (detected && tracker->has_palm) {
            float dx = palm_center.x - tracker->palm_center.x;
            float dy = palm_center.y - tracker->palm_center.y;
            float dist_px = std::sqrt(dx * dx + dy * dy);
            float max_jump = std::max(tracker->palm_radius, palm_radius) * MAX_JUMP_MULT;
            if (dist_px > max_jump) {
                detected = false;
            }
        }
    }

    if (detected) {
        // 7. Kalman correction
        if (!tracker->kalman_initialized) {
            init_kalman(tracker, palm_center);
        } else {
            cv::Mat meas = (cv::Mat_<float>(2, 1) << palm_center.x, palm_center.y);
            cv::Mat est  = tracker->kf.correct(meas);
            palm_center = cv::Point2f(est.at<float>(0), est.at<float>(1));
        }

        tracker->palm_center = palm_center;
        tracker->palm_radius = palm_radius;
        tracker->has_palm    = true;
        tracker->lost_frames = 0;

        *x = (palm_center.x / static_cast<float>(W)) * 100.0f;
        *y = (palm_center.y / static_cast<float>(H)) * 100.0f;
    } else {
        tracker->lost_frames++;
        if (tracker->lost_frames > GRACE_FRAMES) {
            tracker->has_palm         = false;
            tracker->kalman_initialized = false;
        }
    }

    // 8. Preview — palm circle only, no finger/arm annotation.
    if (tracker->preview_texture) {
        cv::Mat preview_frame = tracker->frame.clone();
        if (detected) {
            cv::circle(preview_frame, palm_center,
                       static_cast<int>(palm_radius),
                       cv::Scalar(0, 255, 0), 2);
            cv::circle(preview_frame, palm_center, 4,
                       cv::Scalar(0, 255, 255), -1);
            cv::putText(preview_frame, "Palm locked", cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7,
                        cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        } else {
            cv::putText(preview_frame, "Searching...", cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7,
                        cv::Scalar(0, 80, 255), 2, cv::LINE_AA);
        }

        cv::cvtColor(preview_frame, tracker->preview_rgb, cv::COLOR_BGR2RGB);
        SDL_UpdateTexture(tracker->preview_texture, NULL,
                          tracker->preview_rgb.data,
                          static_cast<int>(tracker->preview_rgb.step));
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
