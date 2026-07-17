# HandTracked-Tennis-C

Real-time webcam tennis game written in C/C++ with OpenCV hand detection and SDL2 rendering.

The player paddle is controlled by your hand position in front of a camera. The opponent is AI-driven with four difficulty levels, and the game renders a live HUD showing tracking confidence, hand position, FPS, and score.

## Core Features

- Real-time hand tracking from webcam input: optional YOLOv8 DNN palm detector, YCrCb + HSV skin segmentation, MOG2 motion, Kalman filter with grace-period coast, adaptive per-user skin bounds, face-cascade masking, static skin-background subtraction
- Playable Pong-style tennis loop with gravity, collisions, spin, and particle effects
- Four AI difficulty modes: Easy, Medium, Hard, Extreme
- Modern widescreen HUD with a dedicated hand-tracking diagnostics panel
- Built-in camera preview window rendered through SDL
- Configurable frame cap from command line (30 or 60 FPS)

## Tech Stack

- C11 and C++17
- OpenCV 4 (core, imgproc, videoio)
- SDL2
- SDL2_ttf
- CMake 3.10+

## Project Layout

```text
HandTracked-Tennis-C/
├── main.c              # Startup, camera prompt, game loop, CLI parsing
├── game.h              # Shared game constants, types, API
├── game.c              # Game state init/update/reset and win logic
├── physics.c           # Ball movement, collisions, particles, scoring
├── ai_opponent.c       # AI behavior and difficulty scaling
├── rendering.c         # SDL windowing, court/HUD rendering, game-over view
├── hand_tracker.h      # C interface for tracker module
├── hand_tracker.cpp    # OpenCV capture, hand detection, preview output
├── CMakeLists.txt      # Build configuration
├── run.bat             # Windows helper launcher
├── QUICK_START.md
├── HAND_TRACKING_GUIDE.md
└── ARCHITECTURE.md
```

## Build

### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libopencv-dev libsdl2-dev libsdl2-ttf-dev

cd HandTracked-Tennis-C
cmake -S . -B build
cmake --build build -j
```

Run:

```bash
./build/hand_tennis
```

### Windows (MSYS2 UCRT64 recommended)

Install MSYS2 and then install toolchain/packages in UCRT64 shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-opencv mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_ttf
```

Build:

```bash
cd HandTracked-Tennis-C
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
```

Run with helper script:

```bat
run.bat
```

Or run executable directly:

```bat
build\hand_tennis.exe
```

## Command-Line Usage

```text
hand_tennis [difficulty] [fps]
```

- Difficulty values:
1. 0 = Easy
2. 1 = Medium (default)
3. 2 = Hard
4. 3 = Extreme

- FPS cap values:
1. 30 = lower CPU usage
2. 60 = smoother motion (default)

Examples:

```bash
./hand_tennis
./hand_tennis 0
./hand_tennis 3
./hand_tennis 1 30
```

## Controls

- Move paddle: move your hand up/down in front of the camera
- Restart after game over: Space
- Quit: Esc or close the game window

## Camera and Tracking Behavior

- On startup, the app opens a camera-access prompt window with Retry/Quit actions.
- The tracker can use a specific camera via environment variable:

```bash
HAND_TENNIS_CAMERA_INDEX=1 ./build/hand_tennis
```

- If no camera index is provided, indices 0 through 9 are probed automatically.
- Camera frames are processed at 640x480, then converted to normalized 0-100 coordinates.
- Hand confidence ramps up/down over time and gates paddle control.
- Camera auto-exposure and auto-white-balance are locked at startup to keep skin hue stable frame-to-frame. Some backends silently ignore the request.
- On brief detection misses (up to 8 frames ≈ 0.27 s at 30 FPS), the tracker coasts on the Kalman prediction so the paddle keeps a smooth trajectory instead of dropping out.
- Optional DNN palm detector: drop a YOLOv8n-hand ONNX at `models/palm.onnx` (or point `HAND_TENNIS_MODEL` at it). Falls back to color pipeline if missing.
- Optional face masking: drop `haarcascade_frontalface_default.xml` at `models/` (or point `HAND_TENNIS_HAAR` at it) to prevent face lock-on in the color path.

## Gameplay Model

- Court area: 960x600 inside a 1520x720 window
- Ball dynamics:
1. Gravity each frame
2. Top/bottom bounce damping
3. Paddle collision velocity reversal
4. Spin transfer from paddle vertical velocity
5. Speed growth capped by MAX_BALL_SPEED

- Score to win: first to 11 points

## AI Difficulty Tuning

| Difficulty | Reaction Delay (frames) | Prediction Factor | Speed Multiplier |
| --- | --- | --- | --- |
| Easy | 20 | 0.30 | 0.70 |
| Medium | 12 | 0.60 | 0.90 |
| Hard | 6 | 0.85 | 1.10 |
| Extreme | 2 | 1.00 | 1.30 |

Easy mode also adds random targeting error to feel less robotic.

## Troubleshooting

### Camera does not open

- Verify OS camera permissions for the app/session.
- Try forcing a different camera index via HAND_TENNIS_CAMERA_INDEX.
- Close other apps that may be locking the camera.

### Hand is not detected reliably

- Improve lighting and reduce harsh shadows.
- Keep your hand in frame with clear contrast from background.
- Adjust HSV / YCrCb thresholds in `hand_tracker.cpp` for your environment/skin tone, or trigger the recalibration path so a new static skin-background pass runs with your hand out of view.
- For guaranteed robustness under changing light, install the YOLOv8n-hand ONNX model — the color pipeline is only a fallback.
- If skin hue still drifts frame-to-frame, the camera backend ignored the auto-exposure/auto-WB lock. Force the DirectShow backend or set exposure manually in your camera driver.

### Build cannot find dependencies

- Confirm pkg-config sees modules:

```bash
pkg-config --modversion opencv4 sdl2 SDL2_ttf
```

- Reconfigure with a clean build directory:

```bash
cmake -S . -B build
```

## Notes for Contributors

- Keep hand coordinate handling in normalized 0-100 units across tracker and game logic.
- The camera preview intentionally uses SDL rendering instead of OpenCV highgui windows for better Windows runtime compatibility.
- If you tune gameplay, update AI and physics constants in ai_opponent.c, game.h, and physics.c together.

## Related Docs

- QUICK_START.md for a short setup path
- HAND_TRACKING_GUIDE.md for tracker tuning details
- ARCHITECTURE.md for module-level technical details
