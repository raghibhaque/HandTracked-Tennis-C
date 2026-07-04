# Hand Tennis Game - Build Guide

## Quick Overview
A real-time hand-tracking tennis game built in C using OpenCV for computer vision and SDL2 for graphics.

### Features
- **Live hand tracking** via webcam (skin color detection + contour analysis)
- **Physics simulation** with ball gravity, paddle collisions, and velocity damping
- **AI opponent** with 4 difficulty levels (Easy → Extreme) with predictive behavior
- **Visual effects** particle system for collision feedback
- **Real-time score tracking** and win conditions (first to 11)
- **Modern glass-style HUD** with a dedicated hand-tracking status panel
- **Configurable frame cap** for 30 FPS or 60 FPS playback

---

## Dependencies

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libopencv-dev \
    libsdl2-dev \
    pkg-config
```

### macOS (Homebrew)
```bash
brew install cmake opencv sdl2 pkg-config
```

### Fedora/RHEL
```bash
sudo dnf install -y \
    gcc \
    cmake \
    opencv-devel \
    SDL2-devel \
    pkg-config
```

### Windows (MSVC or MinGW)
- Download OpenCV prebuilt binaries
- Download SDL2 prebuilt binaries
- Set environment variables or update CMakeLists.txt paths

---

## Building

### Step 1: Create build directory
```bash
cd hand_tennis_game
mkdir -p build
cd build
```

### Step 2: Configure with CMake
```bash
cmake ..
```

### Step 3: Compile
```bash
make -j4
```

### Step 4: Run
```bash
./hand_tennis [difficulty] [fps]
```

#### Difficulty Levels:
- `0` = **Easy** (slow opponent, forgiving)
- `1` = **Medium** (balanced, default)
- `2` = **Hard** (fast, predictive)
- `3` = **Extreme** (godlike AI)

#### FPS Cap:
- `30` = lower CPU usage and a lighter frame cap
- `60` = smoother motion and the default cap

#### Examples:
```bash
./hand_tennis        # Starts on Medium
./hand_tennis 0      # Easy mode
./hand_tennis 3      # Extreme mode
./hand_tennis 1 30   # Medium mode at 30 FPS
./hand_tennis 2 60   # Hard mode at 60 FPS
```

---

## File Structure

```
hand_tennis_game/
├── CMakeLists.txt          # Build configuration
├── include/
│   ├── game.h              # Core game structures & definitions
│   └── hand_tracker.h      # Hand tracking interface
├── src/
│   ├── main.c              # Game loop & entry point
│   ├── game.c              # Game state management
│   ├── hand_tracker.c      # OpenCV hand detection (skin color + contours)
│   ├── physics.c           # Ball physics & collision detection
│   ├── ai_opponent.c       # AI paddle controller with difficulty
│   └── rendering.c         # SDL2 graphics rendering
└── README.md               # This file
```

---

## How It Works

### Hand Tracking (OpenCV)
1. Captures video from default camera (640x480)
2. Converts BGR → HSV color space
3. Applies skin color thresholding (H: 0-20, S: 10-255, V: 60-255)
4. Morphological operations (erode → dilate) for noise reduction
5. Finds largest contour (assumed to be the hand)
6. Returns center coordinates of bounding rectangle
7. Exponential smoothing applied in game loop for stability

**Calibration Tip:** If hand tracking isn't working:
- Ensure good lighting (natural/bright indoor)
- Adjust HSV thresholds in `hand_tracker.c` if needed
- Check the hand-tracking panel on the right side of the window for detection state and confidence

### Game Physics
- **Ball**: Constant velocity + gravity (0.3 px/frame²)
- **Paddles**: Controlled via hand position (player) or AI (opponent)
- **Collisions**: AABB (Axis-Aligned Bounding Box) detection
- **Bounce**: Velocity reversal + damping (0.95x)
- **Spin**: Ball inherits paddle velocity × 0.5
- **Speed cap**: Max speed = 15 px/frame

### AI Opponent
| Difficulty | Reaction Delay | Prediction | Speed | Accuracy |
|-----------|----------------|-----------|-------|----------|
| Easy      | 20 frames      | 30%       | 0.7x  | ±20 px  |
| Medium    | 12 frames      | 60%       | 0.9x  | Accurate |
| Hard      | 6 frames       | 85%       | 1.1x  | Accurate |
| Extreme   | 2 frames       | 100%      | 1.3x  | Perfect  |

---

## Controls

| Action | Control |
|--------|---------|
| Move paddle | Move hand up/down in front of camera |
| Restart (after game over) | Press SPACE |
| Quit | Press ESC or close window |

**Hand Tracking Panel (right side):**
- Detection state shows whether the hand is currently tracked
- Confidence bar shows how stable the tracking is
- Vertical hand-position guide shows the current smoothed hand Y position

---

## Troubleshooting

### "Could not open camera"
- Check camera permissions (especially on Linux/macOS)
- Try `ls -la /dev/video*` to see available cameras
- On macOS: Check System Preferences → Security & Privacy → Camera

### "Hand tracking not working / losing detection"
- **Lighting**: Ensure good lighting (shadows affect skin detection)
- **Camera angle**: Position camera 30-45° downward
- **Contrast**: Avoid uniform background colors
- **Distance**: Keep hand 20-60 cm from camera

### Low frame rate
- Close other GPU-intensive applications
- Reduce camera resolution in `hand_tracker.c` (line ~35)
- Switch to lower difficulty (less AI computation)

### Build errors
- Ensure all dependencies installed: `pkg-config --modversion opencv`
- Check CMake found packages: `cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON`
- On Linux: `sudo ldconfig` to update library cache

---

## Performance Notes

- **Target FPS**: 30 or 60, depending on the runtime cap you pass to `hand_tennis`
- **Hand tracking**: ~30 ms per frame (640x480 with morphological ops)
- **AI prediction**: O(1) per frame after reaction delay
- **Memory**: ~10 MB (minimal allocations in game loop)

---

## Future Enhancements

- [ ] Finger-specific tracking (MediaPipe C++ bindings for precise racquet angle)
- [ ] Sound effects (paddle hit, score, game over)
- [ ] Multiple game modes (classic, zen/practice, tournament)
- [ ] Network multiplayer (player vs player over network)
- [ ] Configurable AI personality (different strategies)
- [ ] Power-ups (speed boost, ball slowmo, paddle size)
- [ ] Statistics tracking (longest rally, fastest ball, etc.)

---

## License

This project is open-source. Feel free to modify and extend for your portfolio!

---

## Contact

Built for portfolio + internship at J&J Vision Care. Questions? Check the code comments—they're detailed!
