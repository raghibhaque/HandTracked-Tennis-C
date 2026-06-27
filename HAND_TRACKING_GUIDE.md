# Hand Tracking Troubleshooting Guide

## Quick Diagnostics

### Symptom: Hand Tracking Never Turns Green (Not Detecting)

**Step 1: Check Camera Works**
```bash
# Linux
ls -la /dev/video*

# macOS (install imagesnap first)
brew install imagesnap
imagesnap -w 1 test.jpg && ls -la test.jpg

# Windows
# Use Camera app to verify webcam works
```

**Step 2: Run in Easy Mode**
```bash
./hand_tennis 0
```
Easy mode has more forgiving thresholds. If this works, your camera is fine.

**Step 3: Check Lighting**
The #1 cause of hand tracking failure is **poor lighting**.
- Open curtains or turn on overhead lights
- Avoid strong backlighting (sun behind hand)
- Aim for **1000+ lux** (desk lamp level)
- Check for shadows on your hand

**Step 4: Adjust Camera Angle**
- Position camera 12-18 inches (30-45cm) away
- Angle down 30-45° toward hand
- Avoid extreme angles (too low/high makes palm unrecognizable)

**Step 5: Adjust Skin Color Thresholds**

Edit `src/hand_tracker.c` lines 24-28:

```c
// Original (tuned for fair/medium tones)
#define SKIN_H_MIN 0
#define SKIN_H_MAX 20
#define SKIN_S_MIN 10
#define SKIN_S_MAX 255
#define SKIN_V_MIN 60
#define SKIN_V_MAX 255
```

**For darker skin tones** (increase hue range + lower saturation):
```c
#define SKIN_H_MIN 0
#define SKIN_H_MAX 30      // ← Increase to 30
#define SKIN_S_MIN 5       // ← Lower to 5
#define SKIN_S_MAX 255
#define SKIN_V_MIN 50      // ← Lower to 50
#define SKIN_V_MAX 255
```

**For lighter/paler skin** (decrease hue range + increase saturation):
```c
#define SKIN_H_MIN 0
#define SKIN_H_MAX 15      // ← Lower to 15
#define SKIN_S_MIN 15      // ← Raise to 15
#define SKIN_S_MAX 255
#define SKIN_V_MIN 70      // ← Raise to 70
#define SKIN_V_MAX 255
```

After editing, rebuild:
```bash
cd hand_tennis_game/build
cmake ..
make
./hand_tennis 0
```

---

### Symptom: Hand Tracking Jittery (Green, But Paddle Shakes)

**Cause:** Raw camera coordinates are noisy.

**Solution:** Increase smoothing in `src/game.c` line ~89:

```c
// Original (20% raw, 80% smoothed)
state->hand.smoothed_x = state->hand.smoothed_x * 0.8f + hand->x * 0.2f;
state->hand.smoothed_y = state->hand.smoothed_y * 0.8f + hand->y * 0.2f;

// More smoothing (10% raw, 90% smoothed) - feels lag-free but smooth
state->hand.smoothed_x = state->hand.smoothed_x * 0.9f + hand->x * 0.1f;
state->hand.smoothed_y = state->hand.smoothed_y * 0.9f + hand->y * 0.1f;

// Less smoothing (30% raw, 70% smoothed) - more responsive but slightly noisy
state->hand.smoothed_x = state->hand.smoothed_x * 0.7f + hand->x * 0.3f;
state->hand.smoothed_y = state->hand.smoothed_y * 0.7f + hand->y * 0.3f;
```

---

### Symptom: Hand Tracking Detects Random Objects (Walls, Furniture)

**Cause:** Background items have similar color to skin.

**Solution:** Increase size threshold in `src/hand_tracker.c` line ~71:

```c
// Original (500 pixels minimum)
double max_area = 500;

// Only accept larger contours (good for minimizing false positives)
double max_area = 2000;  // ← Increase this

// Rebuild: cd build && make
```

---

### Symptom: Hand Detected, But Paddle Won't Move

**Cause:** Tracking confidence is low.

**Check in console output:**
```
FPS: 60.0 | Player: 0 | Opponent: 0 | Difficulty: Medium
```

**Debug:** Add this to `src/game.c` in `game_update()` function:

```c
printf("Hand confidence: %d/100, Mapped Y: %.1f\n", 
       state->hand.tracking_confidence, 
       state->player.y);
```

**Expected:** Confidence should be 50+ when hand is visible.

**If confidence is low (0-30):**
- Improve lighting
- Move hand more prominently
- Reduce background clutter

---

### Symptom: Paddle Overshoots (Moves Past Optimal Position)

**Cause:** Exponential smoothing lag + fast hand movement.

**Solution A:** Reduce smoothing (make it more responsive):
```c
// In src/game.c, around line 89:
state->hand.smoothed_x = state->hand.smoothed_x * 0.7f + hand->x * 0.3f;
state->hand.smoothed_y = state->hand.smoothed_y * 0.7f + hand->y * 0.3f;
```

**Solution B:** Reduce paddle bounds (prevent overshoot):
```c
// In src/game.c, around line 100-101:
// Instead of:
if (game_y < PADDLE_MIN_Y) game_y = PADDLE_MIN_Y;
if (game_y > PADDLE_MAX_Y) game_y = PADDLE_MAX_Y;

// Do this (clamp tighter):
float margin = 5.0f;
if (game_y - margin < PADDLE_MIN_Y) game_y = PADDLE_MIN_Y + margin;
if (game_y + margin > PADDLE_MAX_Y) game_y = PADDLE_MAX_Y - margin;
```

---

## Advanced Calibration

### Custom Calibration Procedure

1. **Start game with debug output:**
   ```c
   // Add to src/hand_tracker.c after line 108:
   printf("Detected hand at camera coords: (%.1f, %.1f)\n", *x, *y);
   ```

2. **Note hand position when tracking turns green**
   - Run game and move hand slowly
   - Note coordinates when detection starts/stops
   - Example output:
     ```
     Detected hand at camera coords: (150.5, 75.2)
     Detected hand at camera coords: (150.2, 75.8)
     ```

3. **Identify your lighting conditions**
   - Measure room illumination (use phone light meter app)
   - Document threshold settings that worked
   - Save as comment in code for future reference

### Batch Testing Multiple Thresholds

Create `test_thresholds.c`:
```c
#include <stdio.h>
#include "hand_tracker.h"

int main() {
    HandTracker *tracker = hand_tracker_init();
    if (!tracker) return 1;
    
    int detected_count = 0;
    for (int i = 0; i < 100; i++) {
        float x, y;
        if (hand_tracker_detect(tracker, &x, &y)) {
            detected_count++;
            printf("Frame %d: Hand at (%.1f, %.1f)\n", i, x, y);
        } else {
            printf("Frame %d: No hand\n", i);
        }
    }
    
    printf("\nDetection rate: %.1f%%\n", (detected_count / 100.0f) * 100);
    hand_tracker_cleanup(tracker);
    return 0;
}
```

---

## Camera-Specific Tips

### Built-in Laptop Camera
- **Issue:** Often low quality, auto-white-balance is aggressive
- **Fix:** Increase lighting significantly (use two lamps)
- **Tip:** Move hand slower (gives more frames to average)

### USB Webcam
- **Issue:** May have better color reproduction than laptop
- **Advantage:** Can position away from screen glare
- **Tip:** Check camera settings (focus, exposure) in Camera app before running

### Kinect / RealSense (Advanced)
- If you have these: **Way better approach**, but requires different SDK
- Could upgrade project to use Azure Kinect SDK
- Would eliminate hand detection issues entirely

---

## Performance Tuning

### If Hand Tracking is Slow (< 15 FPS)

**Reduce camera resolution** in `src/hand_tracker.c` line ~35-36:
```c
// Original (640x480)
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_WIDTH, 640);
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_HEIGHT, 480);

// Lower resolution (320x240) - much faster
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_WIDTH, 320);
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FRAME_HEIGHT, 240);
```

**Or reduce FPS capture** in `src/hand_tracker.c` line ~37:
```c
// Original (30 FPS from camera)
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FPS, 30);

// Reduce camera FPS (trades latency for speed)
cvSetCaptureProperty(tracker->capture, CV_CAP_PROP_FPS, 15);
```

---

## Debugging Checklist

- [ ] **Camera works** (test with Camera app first)
- [ ] **Lighting is good** (50+ lux, no strong shadows)
- [ ] **Hand is visible** (front of camera, not sides)
- [ ] **Angle is correct** (camera 30-45° downward)
- [ ] **Distance is right** (12-24 inches from camera)
- [ ] **Background is clear** (avoid skin-colored objects behind hand)
- [ ] **Easy mode works** (if yes, thresholds just need tuning)
- [ ] **Confidence > 50** (in console output)
- [ ] **Smoothing is reasonable** (not too jittery or laggy)

---

## Emergency Quick Fixes

| Problem | Quick Fix |
|---------|-----------|
| Never detects | Turn on more lights |
| Detects walls/furniture | Sit in front of blank wall |
| Jittery paddle | Decrease hand speed, increase smoothing |
| No response | Check camera is set to /dev/video0 (Linux) |
| Crashes on startup | Camera is in use by another app (close it) |

---

## Testing Without Hand

If you want to test game mechanics **without** hand tracking, edit `src/game.c` line ~87:

```c
// Original:
if (hand->detected) {

// For testing, force detection:
if (true) {  // ← Simulate always-detected hand
    // Game will use last known position
}
```

This lets you test game logic independently of hand tracking issues.

---

## Advanced: Manual HSV Calibration Tool

Run this to find optimal thresholds for **your skin tone + lighting**:

```bash
# Linux only (requires OpenCV with GUI)
cd hand_tennis_game/src
# Add this temporary program to find thresholds:
```

```c
// Save as test_hsv.c
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <stdio.h>

int main() {
    CvCapture *cap = cvCaptureFromCAM(0);
    IplImage *frame = cvQueryFrame(cap);
    IplImage *hsv = cvCreateImage(cvGetSize(frame), 8, 3);
    
    while (1) {
        frame = cvQueryFrame(cap);
        cvCvtColor(frame, hsv, CV_BGR2HSV);
        cvShowImage("HSV", hsv);
        
        // Click on your hand pixel, print HSV values
        int key = cvWaitKey(1);
        if (key == 27) break;  // ESC to exit
    }
    
    cvReleaseCapture(&cap);
    return 0;
}
```

Then compile with:
```bash
gcc test_hsv.c `pkg-config --cflags --libs opencv` -o test_hsv
./test_hsv
# Use trackbars to find exact H/S/V range for your hand
```

---

## Support

- **Documentation:** See `README.md` for full features
- **Architecture:** See `ARCHITECTURE.md` for system details
- **Quick start:** See `QUICK_START.md` for setup

Still stuck? Add `printf` debugging to `hand_tracker.c` to trace exactly where detection fails!
