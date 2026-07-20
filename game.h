#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

// Game constants
#define GAME_WIDTH 1520
#define GAME_HEIGHT 720
#define COURT_WIDTH 960
#define COURT_HEIGHT 600
#define COURT_X 10
#define COURT_Y 60

// Ball constants
#define BALL_RADIUS 8
#define BALL_SPEED_X 8
#define BALL_SPEED_Y 4
#define MAX_BALL_SPEED 15

// Paddle constants
#define PADDLE_WIDTH 20
#define PADDLE_HEIGHT 120
#define PADDLE_SPEED 12
#define PADDLE_MIN_Y COURT_Y
#define PADDLE_MAX_Y (COURT_Y + COURT_HEIGHT - PADDLE_HEIGHT)

// Difficulty levels
typedef enum {
    DIFFICULTY_EASY = 0,
    DIFFICULTY_MEDIUM = 1,
    DIFFICULTY_HARD = 2,
    DIFFICULTY_EXTREME = 3
} Difficulty;

// Particle for visual effects
typedef struct {
    float x, y;
    float vx, vy;
    uint8_t r, g, b;
    int lifetime;
    int max_lifetime;
} Particle;

// Ball structure
typedef struct {
    float x, y;
    float vx, vy;
    int radius;
} Ball;

// Paddle structure
typedef struct {
    float x, y;
    int width, height;
    float vy;  // velocity
} Paddle;

// Hand tracking structure
typedef struct {
    float x, y;
    bool detected;
    float smoothed_x, smoothed_y;
    int tracking_confidence;
} Hand;

// Game state structure
typedef struct {
    Ball ball;
    Paddle player;
    Paddle opponent;
    Hand hand;
    
    int player_score;
    int opponent_score;
    int max_score;
    
    Difficulty difficulty;
    bool game_over;
    bool game_won;  // true if player won, false if opponent won
    
    // Visual effects
    Particle particles[256];
    int particle_count;
    
    // Game timing
    int frame_count;
    float fps;

    // Rally combo tracking
    int rally_count;      // Consecutive hits this rally
    int rally_best;       // Best rally this session
    int rally_flash;      // Countdown frames for HUD flash after milestone
} GameState;

// Function declarations
GameState* game_init(Difficulty difficulty);
void game_update(GameState *state, Hand *hand);
void game_reset_ball(GameState *state);
void game_cleanup(GameState *state);

#endif
