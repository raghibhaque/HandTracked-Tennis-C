# Quick Start Guide - Hand Tennis Game

## 1. Install Dependencies (Choose Your OS)

### Ubuntu/Debian (Recommended for testing)
```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake libopencv-dev libsdl2-dev pkg-config
```

### macOS
```bash
brew install cmake opencv sdl2 pkg-config
```

### Fedora/CentOS
```bash
sudo dnf install gcc cmake opencv-devel SDL2-devel pkg-config
```

---

## 2. Build the Game

```bash
cd hand_tennis_game
mkdir -p build
cd build
cmake ..
make -j4
```

**Expected output:**
```
[100%] Built target hand_tennis
```

---

## 3. Run It!

### Default (Medium difficulty)
```bash
./hand_tennis
```

### Try other difficulties
```bash
./hand_tennis 0    # Easy (best for testing hand tracking)
./hand_tennis 2    # Hard
./hand_tennis 3    # Extreme (AI is brutal)
```

---

## 4. First Time Setup - Hand Calibration

When you run the game:
1. **Position your webcam** 30-45° downward looking at you
2. **Ensure good lighting** (natural light or desk lamp)
3. **Move your hand slowly** in front of the camera
4. **Watch the indicator** (bottom-right corner):
   - 🟢 Green = hand detected
   - 🔴 Red = not detected

**If hand tracking isn't working:**
- Try **Easy mode** (more forgiving)
- Move hand **slower and bigger**
- Increase room brightness
- Try a different camera angle (lower/higher)

---

## 5. Gameplay

| Element | Control |
|---------|---------|
| **Move paddle** | Move your hand up/down in front of camera |
| **Track hand** | Green indicator = tracking active |
| **Restart** | Press SPACE after game ends |
| **Quit** | Press ESC |

**Winning:** First to 11 points wins!

---

## 6. Monitor Performance

The console will print FPS and scores every second:
```
FPS: 59.0 | Player: 3 | Opponent: 2 | Difficulty: Medium
```

**Good performance:** 50-60 FPS
**Acceptable:** 30-50 FPS  
**Need optimization:** <30 FPS

---

## 7. Troubleshooting

### Camera Not Found
```
Error: Could not open camera
```
**Fix:**
```bash
# Check available cameras
ls /dev/video*

# Try camera 1 instead (edit hand_tracker.c line ~26, change cvCaptureFromCAM(0) to cvCaptureFromCAM(1))
```

### Hand Tracking Always Red
- **Solution 1:** Better lighting (open curtains or use lamp)
- **Solution 2:** Edit `hand_tracker.c` lines 24-28 to adjust HSV thresholds:
  ```c
  #define SKIN_H_MIN 0
  #define SKIN_H_MAX 20
  #define SKIN_S_MIN 10
  ```
  Try increasing SKIN_H_MAX to 30 or lowering SKIN_S_MIN to 5 if your skin tone is different

### Low FPS (<30)
- Close other apps
- Try `./hand_tennis 2` (lower difficulty = less AI work)
- Reduce camera resolution in `hand_tracker.c` line ~36

---

## 8. Code Overview (What You Got)

| File | Purpose |
|------|---------|
| `main.c` | Game loop, event handling, initialization |
| `game.c` | Game state, scoring, update logic |
| `hand_tracker.c` | OpenCV camera + skin detection |
| `physics.c` | Ball physics, collision detection |
| `ai_opponent.c` | 4-level AI with prediction |
| `rendering.c` | SDL2 graphics, UI drawing |

**Key systems:**
- ✅ Hand tracking (OpenCV contour detection)
- ✅ Physics (gravity, bouncing, velocity)
- ✅ AI opponent (prediction-based, difficulty scales)
- ✅ Real-time rendering (60 FPS target)
- ✅ Particle effects (collision feedback)

---

## 9. Next Steps (Optional Enhancements)

### Easy Wins:
- Add sound effects (use SDL_mixer)
- Add score display (use SDL_ttf for text)
- Save high scores to file

### Medium Difficulty:
- Finger tracking for accurate racquet angle
- Power-ups (speed boost, ball slowmo)
- Network multiplayer

### Hard:
- Machine learning-based hand tracking (MediaPipe)
- Advanced AI strategies (different playstyles)
- Tournament mode with multiple opponents

---

## 10. Portfolio Tips

**This project is great for:**
- ✅ Demonstrates real-time systems (60 FPS game loop)
- ✅ Computer vision (OpenCV hand detection)
- ✅ Game physics (collision, velocity, damping)
- ✅ AI systems (prediction algorithm)
- ✅ Low-level C (memory management, modularity)
- ✅ Problem-solving (hand tracking calibration, edge cases)

**For your J&J internship:**
- Mention **real-time optimization** (frame budgeting)
- Discuss **sensor input handling** (camera stream processing)
- Explain **AI prediction** vs simple reactions
- Show **modular code design** (separate physics/AI/rendering)

---

## Quick Cheat Sheet

```bash
# Install + build + run (one-liner for Ubuntu)
sudo apt-get install -y build-essential cmake libopencv-dev libsdl2-dev pkg-config && cd hand_tennis_game && mkdir build && cd build && cmake .. && make && ./hand_tennis 1

# Rebuild after code changes
cd hand_tennis_game/build && make && ./hand_tennis

# Debug (print more info)
# Edit main.c and add printf() statements, then rebuild
```

---

## Questions?

- **Hand tracking not working?** → Check lighting + camera angle
- **AI too easy/hard?** → Try different difficulty levels
- **Compile errors?** → Run `cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON` for details
- **Want to extend?** → Code is modular, add features to each `.c` file

Good luck! 🎾
