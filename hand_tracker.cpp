#include "hand_tracker.h"
#include "calib.h"

#include <opencv2/opencv.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect.hpp>

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
    cv::Mat hsv;
    cv::Mat skin_mask_y;
    cv::Mat skin_mask_h;
    cv::Mat skin_mask;
    cv::Mat motion_mask;
    cv::Mat combined_mask;
    cv::Mat preview_rgb;
    cv::Mat bg_skin_mask;        // permanent static skin-colored background (curtain, desk)
    cv::Mat bg_hit_count;        // int32 accumulator during calibration
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

    int  calib_frames_done = 0;
    bool calib_ready = false;

    // Stale-lock detection: if palm sits still too long, drop and re-acquire.
    cv::Point2f last_moved_center = cv::Point2f(-1.0f, -1.0f);
    int stale_frames = 0;

    // Tier 1: DNN palm detector.
    cv::dnn::Net net;
    bool net_loaded = false;
    int  net_input_size = 640;

    // Tier 1b: MediaPipe-style hand-landmark model.
    // 21 landmarks in a 224x224 crop around the palm bbox. When loaded, the
    // landmark centroid overrides the coarse bbox center and enables pinch
    // gesture detection (thumb tip vs index tip).
    cv::dnn::Net landmark_net;
    bool landmark_loaded = false;
    int  landmark_input_size = 224;
    std::vector<cv::Point2f> landmarks;   // 21 points in frame pixel coords
    bool  landmarks_valid = false;
    bool  pinching = false;
    float pinch_ratio = 1.0f;             // 0.0 = fully pinched, 1.0+ = open

    // Tier 2: haar face cascade. Used both to mask the face out of the color
    // skin mask (color path) and to veto DNN palm bboxes that overlap a face
    // (DNN path). Face rects are refreshed every FACE_REFRESH frames to keep
    // per-frame cost low.
    cv::CascadeClassifier face_cascade;
    bool face_loaded = false;
    std::vector<cv::Rect> face_rects;
    int face_frame_counter = 0;
    static constexpr int FACE_REFRESH = 5;

    // Tier 2: adaptive per-user skin bounds in YCrCb (mean +- 3sigma per
    // channel). Cheap to apply (single inRange) and tighter than the global
    // Chai-Ngan bounds. Sampled from confirmed palm pixels once locked.
    cv::Scalar adaptive_low  = cv::Scalar(0, 0, 0);
    cv::Scalar adaptive_high = cv::Scalar(255, 255, 255);
    bool adaptive_ready = false;
    int  adaptive_samples = 0;
    static constexpr int ADAPTIVE_TARGET_SAMPLES = 1500;

    // Recalibrate request from main thread.
    bool recalibrate_pending = false;
};

// YCrCb skin bounds — Chai & Ngan (1999).
static constexpr int SKIN_CR_MIN = 133;
static constexpr int SKIN_CR_MAX = 173;
static constexpr int SKIN_CB_MIN = 77;
static constexpr int SKIN_CB_MAX = 127;

// HSV skin bounds: two hue ranges (0-20 and 160-180), and a hard SATURATION CAP
// at 150. Pure red curtains / saturated red objects sit at S > 180 and are
// rejected here — skin virtually never exceeds S=150.
static constexpr int SKIN_H1_MIN = 0;
static constexpr int SKIN_H1_MAX = 20;
static constexpr int SKIN_H2_MIN = 160;
static constexpr int SKIN_H2_MAX = 180;
static constexpr int SKIN_S_MIN  = 30;
static constexpr int SKIN_S_MAX  = 150;
static constexpr int SKIN_V_MIN  = 60;
static constexpr int SKIN_V_MAX  = 255;

static constexpr double MIN_CONTOUR_AREA   = 1500.0;
static constexpr double MAX_CONTOUR_AREA   = 250000.0;   // allow palm-over-camera
static constexpr double MIN_SOLIDITY       = 0.55;
static constexpr double MAX_ASPECT_RATIO   = 3.0;
static constexpr float  MIN_PALM_RADIUS_PX = 14.0f;
static constexpr int    GRACE_FRAMES       = 8;
static constexpr double DIST_PENALTY       = 6.0;
static constexpr double MOTION_BONUS       = 5.0;        // stronger tiebreaker
static constexpr float  SEARCH_RADIUS_MULT = 5.0f;
static constexpr float  MAX_JUMP_MULT      = 6.0f;

// Static-skin-background calibration.
static constexpr int    CALIB_FRAMES       = 45;
static constexpr double CALIB_HIT_RATIO    = 0.70;

// Stale-lock escape. If the "palm" sits within ~4 px for 45 frames it is
// almost certainly a static false positive (lamp, curtain, forehead patch);
// drop the lock so the next frame can re-acquire. Uniform threshold whether
// adaptive skin is ready or not — the relaxed variant let face/curtain locks
// linger for seconds instead of ~1.5 s.
static constexpr int    STALE_MAX_FRAMES   = 45;
static constexpr float  STALE_MOVE_EPS_PX  = 4.0f;

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

    // Lock camera auto-exposure and auto-white-balance. Auto modes drift skin
    // hue frame-to-frame under changing light, breaking every downstream color
    // threshold. Values are backend-dependent; try both DirectShow (0.25=manual,
    // 0.75=auto) and V4L2 (1=manual, 3=auto) conventions. Failures are silent —
    // some backends reject the set but the property is a no-op, not fatal.
    tracker->capture.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);
    tracker->capture.set(cv::CAP_PROP_AUTO_WB, 0);
    // Nudge exposure slightly bright so palm silhouette is well-lit even in
    // dim rooms. Manual value range varies wildly by camera; -6 is a common
    // mid-bright value on UVC webcams.
    tracker->capture.set(cv::CAP_PROP_EXPOSURE, -6);

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
    // Process noise raised from 1e-2 → 5e-2: the palm can accelerate fast, and
    // over-smoothing manifests as visible paddle lag during quick swipes.
    // Measurement noise slightly raised (1e-1 → 2e-1) to reject single-frame
    // DNN outliers while still trusting the trend.
    cv::setIdentity(t->kf.processNoiseCov,     cv::Scalar::all(5e-2));
    cv::setIdentity(t->kf.measurementNoiseCov, cv::Scalar::all(2e-1));
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

    // Allocate calibration accumulators sized to the actual frame.
    tracker->bg_hit_count = cv::Mat::zeros(tracker->frame_height,
                                          tracker->frame_width, CV_32SC1);
    tracker->bg_skin_mask = cv::Mat::zeros(tracker->frame_height,
                                          tracker->frame_width, CV_8UC1);

    // Tier 1: try loading YOLOv8n-hand ONNX. Path lookup order:
    //   $HAND_TENNIS_MODEL, ./models/palm.onnx, ./palm.onnx
    // If none found or load fails, tracker falls back to color pipeline.
    {
        std::vector<std::string> candidates;
        if (const char *env = std::getenv("HAND_TENNIS_MODEL")) {
            candidates.emplace_back(env);
        }
        candidates.emplace_back("models/palm.onnx");
        candidates.emplace_back("palm.onnx");

        for (const auto &path : candidates) {
            try {
                cv::dnn::Net n = cv::dnn::readNetFromONNX(path);
                if (!n.empty()) {
                    tracker->net = n;
                    tracker->net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    tracker->net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    tracker->net_loaded = true;
                    std::printf("DNN palm detector loaded: %s\n", path.c_str());
                    break;
                }
            } catch (const cv::Exception &) {
                // Try next candidate silently.
            }
        }
        if (!tracker->net_loaded) {
            std::printf("DNN palm model not found — using color fallback pipeline.\n");
            std::printf("  Place YOLOv8n-hand ONNX at 'models/palm.onnx' to enable DNN mode.\n");
        }
    }

    // Tier 1b: try loading MediaPipe hand-landmark ONNX. Path lookup order:
    //   $HAND_TENNIS_LANDMARK_MODEL, ./models/hand_landmarks.onnx
    // Optional — palm detector alone still works without it.
    {
        std::vector<std::string> candidates;
        if (const char *env = std::getenv("HAND_TENNIS_LANDMARK_MODEL")) {
            candidates.emplace_back(env);
        }
        candidates.emplace_back("models/hand_landmarks.onnx");
        candidates.emplace_back("hand_landmarks.onnx");

        for (const auto &path : candidates) {
            try {
                cv::dnn::Net n = cv::dnn::readNetFromONNX(path);
                if (!n.empty()) {
                    tracker->landmark_net = n;
                    tracker->landmark_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    tracker->landmark_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    tracker->landmark_loaded = true;
                    std::printf("Hand-landmark model loaded: %s\n", path.c_str());
                    break;
                }
            } catch (const cv::Exception &) {
                // Try next candidate silently.
            }
        }
        if (!tracker->landmark_loaded) {
            std::printf("Hand-landmark model not found — palm bbox center only.\n");
            std::printf("  Place MediaPipe hand-landmark ONNX at 'models/hand_landmarks.onnx' to enable pinch gesture.\n");
        }
    }

    // When DNN mode is active, static skin-color background calibration is
    // unnecessary (background pixels are outside every palm bbox). Skip it.
    if (tracker->net_loaded) {
        tracker->calib_ready = true;
    }

    // Tier 2: try loading Haar frontal-face cascade (used to mask face out of
    // skin mask in fallback color pipeline). Silently skipped if missing.
    {
        std::vector<std::string> candidates;
        if (const char *env = std::getenv("HAND_TENNIS_HAAR")) {
            candidates.emplace_back(env);
        }
        candidates.emplace_back("models/haarcascade_frontalface_default.xml");
        candidates.emplace_back("haarcascade_frontalface_default.xml");
        // Common install locations shipped with OpenCV builds. Lets the face
        // veto work out of the box on typical Linux / MSYS2 setups without
        // the user having to copy the XML.
        candidates.emplace_back("C:/msys64/ucrt64/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");
        candidates.emplace_back("C:/msys64/mingw64/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");
        candidates.emplace_back("/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");
        candidates.emplace_back("/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");

        for (const auto &path : candidates) {
            if (tracker->face_cascade.load(path)) {
                tracker->face_loaded = true;
                std::printf("Face cascade loaded: %s\n", path.c_str());
                break;
            }
        }
        if (!tracker->face_loaded) {
            std::printf("Face cascade not found — face-veto disabled.\n");
            std::printf("  Copy haarcascade_frontalface_default.xml to 'models/' to reject false locks on faces.\n");
        }
    }

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

    // Attempt to load persisted YCrCb calibration bounds so the user does not
    // need to recalibrate every launch.
    hand_tracker_load_calibration(tracker);

    return tracker;
}

bool hand_tracker_load_calibration(HandTracker *tracker) {
    if (!tracker) return false;
    Calibration c;
    if (!calib_load(&c)) return false;
    tracker->adaptive_low  = cv::Scalar(c.y_lo, c.cr_lo, c.cb_lo);
    tracker->adaptive_high = cv::Scalar(c.y_hi, c.cr_hi, c.cb_hi);
    tracker->adaptive_ready = true;
    std::printf("[calib] loaded %s (Y %.0f-%.0f Cr %.0f-%.0f Cb %.0f-%.0f)\n",
                calib_path(),
                tracker->adaptive_low[0], tracker->adaptive_high[0],
                tracker->adaptive_low[1], tracker->adaptive_high[1],
                tracker->adaptive_low[2], tracker->adaptive_high[2]);
    return true;
}

bool hand_tracker_calibrate_from_last_frame(HandTracker *t,
                                            float x_pct, float y_pct,
                                            float w_pct, float h_pct) {
    if (!t || t->ycrcb.empty()) return false;

    int x = (int)(x_pct * t->frame_width);
    int y = (int)(y_pct * t->frame_height);
    int w = (int)(w_pct * t->frame_width);
    int h = (int)(h_pct * t->frame_height);

    cv::Rect roi(x, y, w, h);
    roi &= cv::Rect(0, 0, t->ycrcb.cols, t->ycrcb.rows);
    if (roi.area() < 100) return false;

    // Build a base-skin mask over the ROI so we sample only pixels that already
    // look plausibly skin-colored by Chai-Ngan bounds. This rejects the desk /
    // wall pixels that inevitably sneak into any rectangular hand box and keeps
    // adaptive bounds from being pulled toward background statistics.
    cv::Mat roi_ycrcb = t->ycrcb(roi);
    cv::Mat base_mask;
    cv::inRange(roi_ycrcb,
                cv::Scalar(0,   SKIN_CR_MIN, SKIN_CB_MIN),
                cv::Scalar(255, SKIN_CR_MAX, SKIN_CB_MAX),
                base_mask);

    int base_hits = cv::countNonZero(base_mask);
    bool use_mask = base_hits >= 400;  // require enough skin pixels to trust the mask

    double sum[3]  = {0.0, 0.0, 0.0};
    double sum2[3] = {0.0, 0.0, 0.0};
    long   n       = 0;

    for (int row = 0; row < roi_ycrcb.rows; ++row) {
        const cv::Vec3b *pix = roi_ycrcb.ptr<cv::Vec3b>(row);
        const uchar     *msk = use_mask ? base_mask.ptr<uchar>(row) : nullptr;
        for (int col = 0; col < roi_ycrcb.cols; ++col) {
            if (msk && !msk[col]) continue;
            for (int c = 0; c < 3; ++c) {
                double v = pix[col][c];
                sum[c]  += v;
                sum2[c] += v * v;
            }
            n++;
        }
    }
    if (n < 100) return false;

    // Tighter sigma than the online adaptive pipeline: a single captured frame
    // has far fewer samples than a locked-and-rolling capture, so ±2.5σ is a
    // safer envelope than ±3σ.
    for (int c = 0; c < 3; ++c) {
        double mu    = sum[c] / n;
        double var   = sum2[c] / n - mu * mu;
        double sigma = std::sqrt(std::max(var, 1.0));
        t->adaptive_low[c]  = std::max(0.0,   mu - 2.5 * sigma);
        t->adaptive_high[c] = std::min(255.0, mu + 2.5 * sigma);
    }
    t->adaptive_ready = true;
    std::printf("[calib] captured ROI %dx%d, %ld px%s: Y[%.0f-%.0f] Cr[%.0f-%.0f] Cb[%.0f-%.0f]\n",
                w, h, n, use_mask ? " (skin-filtered)" : "",
                t->adaptive_low[0], t->adaptive_high[0],
                t->adaptive_low[1], t->adaptive_high[1],
                t->adaptive_low[2], t->adaptive_high[2]);

    hand_tracker_save_calibration(t);
    return true;
}

bool hand_tracker_save_calibration(HandTracker *tracker) {
    if (!tracker || !tracker->adaptive_ready) return false;
    Calibration c = {
        (int)tracker->adaptive_low[0],  (int)tracker->adaptive_low[1],  (int)tracker->adaptive_low[2],
        (int)tracker->adaptive_high[0], (int)tracker->adaptive_high[1], (int)tracker->adaptive_high[2],
    };
    if (!calib_save(&c)) return false;
    std::printf("[calib] saved %s\n", calib_path());
    return true;
}

// Letterbox to a square target, preserving aspect, padding with gray.
static cv::Mat letterbox_square(const cv::Mat &src, int size,
                                float &scale, int &pad_x, int &pad_y) {
    int w = src.cols;
    int h = src.rows;
    scale = std::min(static_cast<float>(size) / static_cast<float>(w),
                     static_cast<float>(size) / static_cast<float>(h));
    int new_w = static_cast<int>(std::round(w * scale));
    int new_h = static_cast<int>(std::round(h * scale));
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));
    pad_x = (size - new_w) / 2;
    pad_y = (size - new_h) / 2;
    cv::Mat out(size, size, src.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(out(cv::Rect(pad_x, pad_y, new_w, new_h)));
    return out;
}

struct DnnDetection {
    cv::Rect box;
    float    conf;
};

// Decode YOLOv8 single-class output. Expected shape variants:
//   (1, 5, N)  — Ultralytics default: [cx, cy, w, h, cls_conf]
//   (1, N, 5)  — some exporters transpose
// We normalize to rows-of-detections (N, 5) internally.
static std::vector<DnnDetection> yolov8_decode_single_class(
        const cv::Mat &raw,
        float scale, int pad_x, int pad_y,
        int img_w, int img_h,
        float conf_th, float iou_th) {

    cv::Mat out;
    if (raw.dims == 3) {
        int d1 = raw.size[1];
        int d2 = raw.size[2];
        if (d1 == 5 && d2 > 5) {
            // (1, 5, N) → transpose to (N, 5)
            cv::Mat tmp(d1, d2, CV_32F, const_cast<float *>(raw.ptr<float>()));
            out = tmp.t();
        } else if (d2 == 5 && d1 > 5) {
            // (1, N, 5)
            out = cv::Mat(d1, d2, CV_32F, const_cast<float *>(raw.ptr<float>()));
        } else {
            // Fallback: reshape naively
            out = raw.reshape(1, raw.size[1]);
        }
    } else {
        out = raw;
    }

    std::vector<cv::Rect> boxes;
    std::vector<float>    confs;
    boxes.reserve(out.rows);
    confs.reserve(out.rows);

    for (int i = 0; i < out.rows; ++i) {
        const float *row = out.ptr<float>(i);
        float conf = row[4];
        if (conf < conf_th) continue;

        float cx = row[0];
        float cy = row[1];
        float bw = row[2];
        float bh = row[3];

        // Undo letterbox: back to original image coords.
        cx = (cx - pad_x) / scale;
        cy = (cy - pad_y) / scale;
        bw = bw / scale;
        bh = bh / scale;

        int x0 = static_cast<int>(std::round(cx - bw * 0.5f));
        int y0 = static_cast<int>(std::round(cy - bh * 0.5f));
        int w  = static_cast<int>(std::round(bw));
        int h  = static_cast<int>(std::round(bh));
        boxes.emplace_back(x0, y0, w, h);
        confs.push_back(conf);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, confs, conf_th, iou_th, keep);

    std::vector<DnnDetection> dets;
    dets.reserve(keep.size());
    cv::Rect img_bounds(0, 0, img_w, img_h);
    for (int idx : keep) {
        DnnDetection d;
        d.box  = boxes[idx] & img_bounds;
        d.conf = confs[idx];
        if (d.box.area() > 0) dets.push_back(d);
    }
    return dets;
}

// Run DNN palm detector on frame. Returns best (or Kalman-nearest) bbox.
static bool dnn_detect_palm(HandTracker *t,
                            const cv::Point2f &predicted, bool has_prediction,
                            cv::Rect &out_bbox, float &out_conf) {
    float scale = 1.0f;
    int pad_x = 0, pad_y = 0;
    cv::Mat letter = letterbox_square(t->frame, t->net_input_size,
                                      scale, pad_x, pad_y);

    cv::Mat blob = cv::dnn::blobFromImage(
        letter, 1.0 / 255.0,
        cv::Size(t->net_input_size, t->net_input_size),
        cv::Scalar(), /*swapRB=*/true, /*crop=*/false);
    t->net.setInput(blob);

    cv::Mat output;
    try {
        output = t->net.forward();
    } catch (const cv::Exception &e) {
        std::fprintf(stderr, "DNN forward failed: %s\n", e.what());
        return false;
    }

    // Strict confidence threshold. A relaxed threshold on cold start pulls in
    // false positives (faces, lamps, hand-shaped shadows) before the tracker
    // has any prior to reject them, and the resulting lock is hard to break.
    auto dets = yolov8_decode_single_class(
        output, scale, pad_x, pad_y,
        t->frame.cols, t->frame.rows,
        /*conf_th=*/0.35f, /*iou_th=*/0.45f);

    // Reject any detection that overlaps a detected face by >40% of its area.
    // YOLOv8-palm occasionally fires on chins, cheeks, and forehead patches;
    // the face cascade is much more reliable at spotting those, so we use it
    // as a veto even when the DNN path is otherwise face-unaware.
    if (!dets.empty() && !t->face_rects.empty()) {
        std::vector<DnnDetection> filtered;
        filtered.reserve(dets.size());
        for (const auto &d : dets) {
            bool on_face = false;
            for (const auto &f : t->face_rects) {
                cv::Rect inter = d.box & f;
                if (inter.area() > 0.4 * d.box.area()) { on_face = true; break; }
            }
            if (!on_face) filtered.push_back(d);
        }
        dets.swap(filtered);
    }
    if (dets.empty()) return false;

    // Pick highest confidence, but prefer detections near Kalman prediction
    // when we already have a lock (rejects a second hand or a face-like FP).
    const DnnDetection *best = nullptr;
    float best_score = -1e9f;
    for (const auto &d : dets) {
        float score = d.conf;
        if (has_prediction) {
            cv::Point2f c(d.box.x + d.box.width * 0.5f,
                          d.box.y + d.box.height * 0.5f);
            float dx = c.x - predicted.x;
            float dy = c.y - predicted.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            score -= dist * 0.002f;  // ~200px penalty ≈ 0.4 conf
        }
        if (score > best_score) {
            best_score = score;
            best = &d;
        }
    }
    if (!best) return false;

    out_bbox = best->box;
    out_conf = best->conf;
    return true;
}

// Run the hand-landmark model on a square crop around the palm bbox. On success
// fills t->landmarks (frame-pixel coords), sets landmarks_valid, and computes
// pinch state (thumb tip 4 vs index tip 8, normalized by wrist-to-mid-mcp).
static bool run_landmark_net(HandTracker *t, const cv::Rect &palm_bbox) {
    if (!t->landmark_loaded || t->frame.empty()) return false;

    // Expand palm bbox to a square with 40% margin for finger reach
    int side = std::max(palm_bbox.width, palm_bbox.height);
    int pad  = static_cast<int>(side * 0.4f);
    int cx   = palm_bbox.x + palm_bbox.width  / 2;
    int cy   = palm_bbox.y + palm_bbox.height / 2;
    int half = side / 2 + pad;

    cv::Rect crop(cx - half, cy - half, 2 * half, 2 * half);
    crop &= cv::Rect(0, 0, t->frame.cols, t->frame.rows);
    if (crop.area() < 100) return false;

    cv::Mat patch;
    cv::resize(t->frame(crop), patch,
               cv::Size(t->landmark_input_size, t->landmark_input_size));

    cv::Mat blob = cv::dnn::blobFromImage(patch, 1.0 / 255.0,
                                          cv::Size(t->landmark_input_size, t->landmark_input_size),
                                          cv::Scalar(), /*swapRB=*/true, /*crop=*/false);
    t->landmark_net.setInput(blob);

    cv::Mat out;
    try {
        out = t->landmark_net.forward();
    } catch (const cv::Exception &) {
        return false;
    }

    // Find a 63-element tensor slice (21 landmarks × xyz). MediaPipe hand
    // landmark models expose this as their primary output.
    if (out.total() < 63) return false;
    cv::Mat flat = out.reshape(1, 1);
    const float *data = flat.ptr<float>(0);

    // Auto-detect output space: some ONNX exports return coords in pixels of
    // the input tile (0..input_size), others normalize to 0..1. Sniff the
    // magnitude of a handful of components to decide.
    float max_abs = 0.0f;
    for (int i = 0; i < 63; ++i) {
        float v = std::fabs(data[i]);
        if (v > max_abs) max_abs = v;
    }
    float scale = (max_abs > 2.0f) ? static_cast<float>(t->landmark_input_size) : 1.0f;

    t->landmarks.resize(21);
    for (int i = 0; i < 21; ++i) {
        float nx = data[i * 3 + 0] / scale;
        float ny = data[i * 3 + 1] / scale;
        t->landmarks[i].x = crop.x + nx * crop.width;
        t->landmarks[i].y = crop.y + ny * crop.height;
    }

    // Pinch: distance(thumb tip=4, index tip=8) / distance(wrist=0, middle_mcp=9)
    auto d = [&](int a, int b) {
        float dx = t->landmarks[a].x - t->landmarks[b].x;
        float dy = t->landmarks[a].y - t->landmarks[b].y;
        return std::sqrt(dx * dx + dy * dy);
    };
    float span = d(0, 9);

    // Sanity: wrist-to-MCP must be a substantial fraction of the crop.
    // Broken output tensors produce degenerate spans; reject silently rather
    // than let a garbage centroid override the DNN bbox center.
    float min_span = crop.width * 0.05f;
    float max_span = crop.width * 1.2f;
    if (span < min_span || span > max_span) {
        t->landmarks_valid = false;
        t->pinch_ratio = 1.0f;
        t->pinching = false;
        return false;
    }

    t->landmarks_valid = true;
    t->pinch_ratio = d(4, 8) / span;
    t->pinching = t->pinch_ratio < 0.4f;
    return true;
}

// Compute palm center + inscribed radius inside a bbox, using the skin mask
// as the palm silhouette. Falls back to bbox center if the mask is empty.
static void palm_center_from_bbox(HandTracker *t, const cv::Rect &bbox,
                                  cv::Point2f &palm_center, float &palm_radius) {
    cv::Rect safe = bbox & cv::Rect(0, 0, t->skin_mask.cols, t->skin_mask.rows);
    if (safe.area() <= 0) {
        palm_center = cv::Point2f(bbox.x + bbox.width * 0.5f,
                                  bbox.y + bbox.height * 0.5f);
        palm_radius = std::min(bbox.width, bbox.height) * 0.4f;
        return;
    }

    // If skin mask has good coverage inside bbox, use distance transform for
    // sub-pixel palm center. Otherwise use bbox center — DNN was confident,
    // skin threshold just failed for this user's tone.
    cv::Mat roi_skin = t->skin_mask(safe);
    int skin_px = cv::countNonZero(roi_skin);
    double coverage = static_cast<double>(skin_px) / safe.area();

    if (coverage > 0.15) {
        cv::Mat dist;
        cv::distanceTransform(roi_skin, dist, cv::DIST_L2, 5);
        cv::Point max_loc;
        double max_val = 0.0;
        cv::minMaxLoc(dist, nullptr, &max_val, nullptr, &max_loc);
        palm_center = cv::Point2f(static_cast<float>(max_loc.x + safe.x),
                                  static_cast<float>(max_loc.y + safe.y));
        palm_radius = static_cast<float>(max_val);
    } else {
        palm_center = cv::Point2f(safe.x + safe.width * 0.5f,
                                  safe.y + safe.height * 0.5f);
        palm_radius = std::min(safe.width, safe.height) * 0.4f;
    }
}

// Sample YCrCb pixels from a confirmed palm ROI to build per-user adaptive
// skin bounds: mean +- 3sigma per channel. Applied as a single extra inRange
// call per frame, so cost is negligible.
static void update_adaptive_skin(HandTracker *t, const cv::Point2f &center,
                                 float radius) {
    if (t->adaptive_ready) return;
    if (radius < 8.0f) return;

    int r = static_cast<int>(radius * 0.6f);
    cv::Rect roi(static_cast<int>(center.x) - r,
                 static_cast<int>(center.y) - r,
                 2 * r, 2 * r);
    roi &= cv::Rect(0, 0, t->ycrcb.cols, t->ycrcb.rows);
    if (roi.area() < 100) return;

    cv::Mat patch = t->ycrcb(roi).clone().reshape(1, roi.area());
    patch.convertTo(patch, CV_32F);

    static thread_local std::vector<cv::Vec3f> pool;
    for (int i = 0; i < patch.rows; ++i) {
        pool.emplace_back(patch.at<float>(i, 0),
                          patch.at<float>(i, 1),
                          patch.at<float>(i, 2));
        if (static_cast<int>(pool.size()) >= HandTracker::ADAPTIVE_TARGET_SAMPLES) break;
    }
    t->adaptive_samples = static_cast<int>(pool.size());

    if (t->adaptive_samples >= HandTracker::ADAPTIVE_TARGET_SAMPLES) {
        double sum[3]  = {0, 0, 0};
        double sum2[3] = {0, 0, 0};
        for (const auto &v : pool) {
            for (int c = 0; c < 3; ++c) {
                sum[c]  += v[c];
                sum2[c] += static_cast<double>(v[c]) * v[c];
            }
        }
        double n = static_cast<double>(pool.size());
        double mu[3], sigma[3];
        for (int c = 0; c < 3; ++c) {
            mu[c]    = sum[c] / n;
            double v = sum2[c] / n - mu[c] * mu[c];
            sigma[c] = std::sqrt(std::max(v, 1.0));
        }
        for (int c = 0; c < 3; ++c) {
            double lo = std::max(0.0,   mu[c] - 3.0 * sigma[c]);
            double hi = std::min(255.0, mu[c] + 3.0 * sigma[c]);
            t->adaptive_low[c]  = lo;
            t->adaptive_high[c] = hi;
        }
        t->adaptive_ready = true;
        pool.clear();
        std::printf("Adaptive skin bounds: Y[%.0f-%.0f] Cr[%.0f-%.0f] Cb[%.0f-%.0f]\n",
                    t->adaptive_low[0], t->adaptive_high[0],
                    t->adaptive_low[1], t->adaptive_high[1],
                    t->adaptive_low[2], t->adaptive_high[2]);
        hand_tracker_save_calibration(t);
    }
}

void hand_tracker_recalibrate(HandTracker *tracker) {
    if (!tracker) return;
    tracker->recalibrate_pending = true;
}

static void apply_recalibrate(HandTracker *t) {
    t->calib_frames_done = 0;
    t->calib_ready = false;
    t->bg_hit_count.setTo(0);
    t->bg_skin_mask.setTo(0);
    t->adaptive_ready = false;
    t->adaptive_samples = 0;
    t->has_palm = false;
    t->kalman_initialized = false;
    t->lost_frames = 0;
    t->stale_frames = 0;
    t->last_moved_center = cv::Point2f(-1.0f, -1.0f);
    t->recalibrate_pending = false;

    // Wipe persisted YCrCb bounds so the next launch does not silently reload
    // the stale calibration the user just discarded.
    if (std::remove(calib_path()) == 0) {
        std::printf("[calib] removed cached %s\n", calib_path());
    }

    std::printf("Recalibration triggered — keep hand OUT of view.\n");
}

bool hand_tracker_detect(HandTracker *tracker, float *x, float *y) {
    if (!tracker || !tracker->capture.isOpened()) return false;

    if (!tracker->capture.read(tracker->frame) || tracker->frame.empty()) {
        return false;
    }

    // Selfie flip: users expect their preview to move like a mirror. Applied
    // in-place so every downstream stage (skin mask, DNN, motion, landmarks,
    // preview) works in a consistent flipped frame.
    cv::flip(tracker->frame, tracker->frame, 1);

    if (tracker->recalibrate_pending) {
        apply_recalibrate(tracker);
    }

    const int W = tracker->frame_width;
    const int H = tracker->frame_height;

    // Kalman prediction (defines ROI when we already have a lock)
    cv::Point2f predicted(-1.0f, -1.0f);
    if (tracker->kalman_initialized) {
        cv::Mat p = tracker->kf.predict();
        predicted = cv::Point2f(p.at<float>(0), p.at<float>(1));
    }

    // 1. Skin mask = YCrCb-skin AND HSV-skin. HSV saturation cap kills
    //    pure/saturated red (curtains, red mug) which fools YCrCb alone.
    cv::GaussianBlur(tracker->frame, tracker->blurred, cv::Size(5, 5), 0.0);

    cv::cvtColor(tracker->blurred, tracker->ycrcb, cv::COLOR_BGR2YCrCb);
    cv::inRange(tracker->ycrcb,
                cv::Scalar(0,   SKIN_CR_MIN, SKIN_CB_MIN),
                cv::Scalar(255, SKIN_CR_MAX, SKIN_CB_MAX),
                tracker->skin_mask_y);

    cv::cvtColor(tracker->blurred, tracker->hsv, cv::COLOR_BGR2HSV);
    cv::Mat hsv1, hsv2;
    cv::inRange(tracker->hsv,
                cv::Scalar(SKIN_H1_MIN, SKIN_S_MIN, SKIN_V_MIN),
                cv::Scalar(SKIN_H1_MAX, SKIN_S_MAX, SKIN_V_MAX), hsv1);
    cv::inRange(tracker->hsv,
                cv::Scalar(SKIN_H2_MIN, SKIN_S_MIN, SKIN_V_MIN),
                cv::Scalar(SKIN_H2_MAX, SKIN_S_MAX, SKIN_V_MAX), hsv2);
    cv::bitwise_or(hsv1, hsv2, tracker->skin_mask_h);

    cv::bitwise_and(tracker->skin_mask_y, tracker->skin_mask_h, tracker->skin_mask);

    // 1a. Adaptive per-user skin bounds (once locked long enough to sample).
    //     Extra inRange in YCrCb ANDed onto skin_mask — tighter than the
    //     global Chai-Ngan bounds for the specific user in the current light.
    if (tracker->adaptive_ready) {
        cv::Mat adaptive_mask;
        cv::inRange(tracker->ycrcb,
                    tracker->adaptive_low, tracker->adaptive_high,
                    adaptive_mask);
        cv::bitwise_and(tracker->skin_mask, adaptive_mask, tracker->skin_mask);
    }

    // 1a2. Face haar detection: refresh face_rects every FACE_REFRESH frames.
    //      Used both to mask the face out of the color skin mask and as a veto
    //      for DNN palm bboxes that overlap a face (YOLOv8-palm sometimes
    //      fires on chins/foreheads).
    if (tracker->face_loaded) {
        tracker->face_frame_counter++;
        if (tracker->face_frame_counter >= HandTracker::FACE_REFRESH) {
            cv::Mat gray;
            cv::cvtColor(tracker->frame, gray, cv::COLOR_BGR2GRAY);
            cv::equalizeHist(gray, gray);
            tracker->face_rects.clear();
            tracker->face_cascade.detectMultiScale(
                gray, tracker->face_rects, 1.2, 4, 0, cv::Size(60, 60));
            // Expand each rect to cover neck/hair too.
            for (auto &f : tracker->face_rects) {
                f.x -= f.width  / 6;
                f.y -= f.height / 8;
                f.width  += f.width  / 3;
                f.height += f.height / 3;
                f &= cv::Rect(0, 0, tracker->frame.cols, tracker->frame.rows);
            }
            tracker->face_frame_counter = 0;
        }

        // Only subtract from the color skin mask when we're on the color path.
        if (!tracker->net_loaded) {
            for (const auto &f : tracker->face_rects) {
                cv::Rect fc = f & cv::Rect(0, 0, tracker->skin_mask.cols, tracker->skin_mask.rows);
                if (fc.area() > 0) tracker->skin_mask(fc).setTo(0);
            }
        }
    }

    // 1b. Static skin-color background calibration. First CALIB_FRAMES frames,
    //     tally which pixels are classified skin. Anything that fires >70% of
    //     the time with no hand present is background (curtain, desk, mug) —
    //     mark it permanent and subtract from every future frame.
    if (!tracker->calib_ready) {
        cv::Mat hits;
        tracker->skin_mask.convertTo(hits, CV_32SC1, 1.0 / 255.0);
        tracker->bg_hit_count += hits;
        tracker->calib_frames_done++;
        if (tracker->calib_frames_done >= CALIB_FRAMES) {
            int threshold = static_cast<int>(CALIB_FRAMES * CALIB_HIT_RATIO);
            cv::compare(tracker->bg_hit_count, threshold,
                        tracker->bg_skin_mask, cv::CMP_GE);
            cv::dilate(tracker->bg_skin_mask, tracker->bg_skin_mask,
                       cv::Mat(), cv::Point(-1, -1), 2);
            tracker->calib_ready = true;
            int bg_px = cv::countNonZero(tracker->bg_skin_mask);
            std::printf("Skin-background calibrated: %d px masked out\n", bg_px);
        }
    } else {
        // Permanent subtraction of static skin-colored background.
        cv::Mat inv_bg;
        cv::bitwise_not(tracker->bg_skin_mask, inv_bg);
        cv::bitwise_and(tracker->skin_mask, inv_bg, tracker->skin_mask);
    }

    // 2. Motion mask via MOG2 — only needed by color pipeline scoring.
    if (!tracker->net_loaded) {
        tracker->bg_sub->apply(tracker->blurred, tracker->motion_mask, 0.005);
        cv::threshold(tracker->motion_mask, tracker->motion_mask,
                      200, 255, cv::THRESH_BINARY);
    }

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

    // 5. Detection — DNN path (Tier 1) or color contour path (fallback).
    bool detected = false;
    cv::Point2f palm_center(-1.0f, -1.0f);
    float palm_radius = 0.0f;
    cv::Rect dnn_bbox;
    float dnn_conf = 0.0f;

    if (tracker->net_loaded) {
        detected = dnn_detect_palm(tracker, predicted,
                                   tracker->kalman_initialized,
                                   dnn_bbox, dnn_conf);
        if (detected) {
            palm_center_from_bbox(tracker, dnn_bbox, palm_center, palm_radius);

            // Refine center + compute pinch with landmark model if loaded.
            tracker->landmarks_valid = false;
            if (tracker->landmark_loaded && run_landmark_net(tracker, dnn_bbox)) {
                // Landmark 9 = middle-finger MCP. Blend 65% landmark, 35%
                // bbox center so a single-frame landmark jitter cannot yank
                // the paddle across the screen. Bbox center from the DNN is
                // stable but coarse; landmark is precise but occasionally
                // twitchy on partial occlusion — averaging kills both flaws.
                const cv::Point2f lm = tracker->landmarks[9];
                palm_center.x = lm.x * 0.65f + palm_center.x * 0.35f;
                palm_center.y = lm.y * 0.65f + palm_center.y * 0.35f;
            }

            if (palm_radius < MIN_PALM_RADIUS_PX) {
                detected = false;
            }
            if (detected && tracker->has_palm) {
                float dx = palm_center.x - tracker->palm_center.x;
                float dy = palm_center.y - tracker->palm_center.y;
                float dist_px = std::sqrt(dx * dx + dy * dy);
                float max_jump = std::max(tracker->palm_radius, palm_radius)
                                 * MAX_JUMP_MULT;
                if (dist_px > max_jump) detected = false;
            }
        } else {
            tracker->landmarks_valid = false;
        }
    }

    // 5b. Color contour fallback (only used when DNN unavailable).
    std::vector<std::vector<cv::Point>> contours;
    if (!tracker->net_loaded && tracker->calib_ready) {
        cv::findContours(search_mask, contours,
                         cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    }

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

    // 6. Palm center via distance transform on best contour (color path only).
    //    In DNN path, palm_center / palm_radius are already set from bbox.
    if (!tracker->net_loaded && detected) {
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

        if (palm_radius < MIN_PALM_RADIUS_PX) {
            detected = false;
        }
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

        // Stale-lock detection: if the "palm" hasn't moved for a long time,
        // it's probably a false-positive static object we locked onto. Drop
        // the lock so the next frame can re-acquire from scratch.
        if (tracker->last_moved_center.x < 0.0f) {
            tracker->last_moved_center = palm_center;
            tracker->stale_frames = 0;
        } else {
            float dx = palm_center.x - tracker->last_moved_center.x;
            float dy = palm_center.y - tracker->last_moved_center.y;
            if (std::sqrt(dx * dx + dy * dy) > STALE_MOVE_EPS_PX) {
                tracker->last_moved_center = palm_center;
                tracker->stale_frames = 0;
            } else {
                tracker->stale_frames++;
            }
        }

        if (tracker->stale_frames > STALE_MAX_FRAMES) {
            tracker->has_palm         = false;
            tracker->kalman_initialized = false;
            tracker->stale_frames     = 0;
            tracker->last_moved_center = cv::Point2f(-1.0f, -1.0f);
            detected = false;
            *x = 50.0f;
            *y = 50.0f;
        } else {
            tracker->palm_center = palm_center;
            tracker->palm_radius = palm_radius;
            tracker->has_palm    = true;
            tracker->lost_frames = 0;
            *x = (palm_center.x / static_cast<float>(W)) * 100.0f;
            *y = (palm_center.y / static_cast<float>(H)) * 100.0f;

            // Tier 2: sample palm YCrCb to build per-user adaptive skin bounds.
            update_adaptive_skin(tracker, palm_center, palm_radius);
        }
    } else {
        tracker->lost_frames++;

        // Coast on Kalman prediction during grace period. Game receives a
        // smooth predicted position instead of a dropout, so brief occlusions
        // and single-frame detection misses become invisible to the paddle.
        // Only coast if we already had a lock — never fabricate a first lock.
        if (tracker->has_palm && tracker->kalman_initialized &&
            tracker->lost_frames <= GRACE_FRAMES &&
            predicted.x >= 0.0f && predicted.y >= 0.0f) {

            float px = std::max(0.0f, std::min(static_cast<float>(W - 1), predicted.x));
            float py = std::max(0.0f, std::min(static_cast<float>(H - 1), predicted.y));

            tracker->palm_center = cv::Point2f(px, py);
            palm_center = cv::Point2f(px, py);
            palm_radius = tracker->palm_radius;
            *x = (px / static_cast<float>(W)) * 100.0f;
            *y = (py / static_cast<float>(H)) * 100.0f;
            detected = true;
        }

        if (tracker->lost_frames > GRACE_FRAMES) {
            tracker->has_palm         = false;
            tracker->kalman_initialized = false;
            tracker->stale_frames     = 0;
            tracker->last_moved_center = cv::Point2f(-1.0f, -1.0f);
        }

        // Prolonged loss (~10 s) with adaptive bounds active usually means the
        // scene lighting shifted and our color model no longer matches the
        // user's skin. Drop the adaptive bounds so subsequent frames fall back
        // to the base Chai-Ngan bounds and re-sample from fresh data on the
        // next lock. Cheap self-repair for lighting drift.
        if (tracker->adaptive_ready && tracker->lost_frames > 300) {
            tracker->adaptive_ready = false;
            tracker->adaptive_samples = 0;
            std::printf("[calib] adaptive bounds dropped after prolonged loss — resampling on next lock\n");
        }
    }

    // 8. Preview — palm circle only, no finger/arm annotation.
    if (tracker->preview_texture) {
        cv::Mat preview_frame = tracker->frame.clone();
        if (!tracker->calib_ready) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "Calibrating background %d/%d - keep hand OUT of view",
                          tracker->calib_frames_done, CALIB_FRAMES);
            cv::putText(preview_frame, buf, cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55,
                        cv::Scalar(0, 200, 255), 2, cv::LINE_AA);
        } else if (detected) {
            cv::circle(preview_frame, palm_center,
                       static_cast<int>(palm_radius),
                       cv::Scalar(0, 255, 0), 2);
            cv::circle(preview_frame, palm_center, 4,
                       cv::Scalar(0, 255, 255), -1);

            // Draw 21 landmarks + thumb-index pinch link when available.
            if (tracker->landmarks_valid && tracker->landmarks.size() >= 21) {
                for (const auto &lm : tracker->landmarks) {
                    cv::circle(preview_frame, lm, 3,
                               cv::Scalar(255, 200, 0), -1);
                }
                cv::Scalar link = tracker->pinching
                    ? cv::Scalar(0, 200, 255)   // pinch: warm orange
                    : cv::Scalar(200, 200, 200);
                cv::line(preview_frame, tracker->landmarks[4],
                         tracker->landmarks[8], link, 2, cv::LINE_AA);
            }

            char buf[96];
            if (tracker->landmark_loaded && tracker->landmarks_valid) {
                std::snprintf(buf, sizeof(buf), "Palm+LM  pinch %.2f%s",
                              tracker->pinch_ratio,
                              tracker->pinching ? "  [PINCH]" : "");
            } else if (tracker->net_loaded) {
                std::snprintf(buf, sizeof(buf), "Palm locked  DNN %.2f", dnn_conf);
            } else {
                std::snprintf(buf, sizeof(buf), "Palm locked  COLOR%s",
                              tracker->adaptive_ready ? "+adapt" : "");
            }
            cv::putText(preview_frame, buf, cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        } else {
            const char *msg = tracker->net_loaded
                ? "Searching... (DNN)"
                : "Searching... (COLOR)";
            cv::putText(preview_frame, msg, cv::Point(10, 26),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
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

bool hand_tracker_is_pinching(HandTracker *tracker) {
    return tracker && tracker->landmarks_valid && tracker->pinching;
}

bool hand_tracker_has_landmarks(HandTracker *tracker) {
    return tracker && tracker->landmark_loaded;
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
