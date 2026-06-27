#include "game.h"
#include <stdlib.h>
#include <math.h>

// Get AI reaction delay based on difficulty
static int get_reaction_delay(Difficulty difficulty) {
    switch (difficulty) {
        case DIFFICULTY_EASY:     return 20;
        case DIFFICULTY_MEDIUM:   return 12;
        case DIFFICULTY_HARD:     return 6;
        case DIFFICULTY_EXTREME:  return 2;
        default:                  return 15;
    }
}

// Get AI prediction lookahead based on difficulty
static float get_prediction_factor(Difficulty difficulty) {
    switch (difficulty) {
        case DIFFICULTY_EASY:     return 0.3f;
        case DIFFICULTY_MEDIUM:   return 0.6f;
        case DIFFICULTY_HARD:     return 0.85f;
        case DIFFICULTY_EXTREME:  return 1.0f;
        default:                  return 0.5f;
    }
}

// Get AI speed multiplier
static float get_speed_multiplier(Difficulty difficulty) {
    switch (difficulty) {
        case DIFFICULTY_EASY:     return 0.7f;
        case DIFFICULTY_MEDIUM:   return 0.9f;
        case DIFFICULTY_HARD:     return 1.1f;
        case DIFFICULTY_EXTREME:  return 1.3f;
        default:                  return 1.0f;
    }
}

void ai_opponent_update(GameState *state, int frame_count) {
    Ball *ball = &state->ball;
    Paddle *opponent = &state->opponent;
    
    // Only think every reaction_delay frames (introduces realistic delay)
    int reaction_delay = get_reaction_delay(state->difficulty);
    if (frame_count % reaction_delay != 0) {
        // Still apply velocity damping
        opponent->vy *= 0.92f;
        return;
    }
    
    // Predict where ball will be
    float prediction_factor = get_prediction_factor(state->difficulty);
    float predicted_y = ball->y + (ball->vy * 30 * prediction_factor);
    
    // Add some uncertainty at easy level
    if (state->difficulty == DIFFICULTY_EASY) {
        int error = (rand() % 40) - 20;
        predicted_y += error;
    }
    
    // Calculate desired center point
    float target_y = predicted_y - (opponent->height / 2.0f);
    
    // Clamp to paddle boundaries
    if (target_y < PADDLE_MIN_Y) target_y = PADDLE_MIN_Y;
    if (target_y > PADDLE_MAX_Y) target_y = PADDLE_MAX_Y;
    
    // Move paddle towards target
    float speed_mult = get_speed_multiplier(state->difficulty);
    float max_speed = PADDLE_SPEED * speed_mult;
    
    float error = target_y - opponent->y;
    if (error > 2.0f) {
        opponent->vy = max_speed;
    } else if (error < -2.0f) {
        opponent->vy = -max_speed;
    } else {
        opponent->vy = error * 0.3f;
    }
}

// Get difficulty name
const char* ai_get_difficulty_name(Difficulty difficulty) {
    switch (difficulty) {
        case DIFFICULTY_EASY:     return "Easy";
        case DIFFICULTY_MEDIUM:   return "Medium";
        case DIFFICULTY_HARD:     return "Hard";
        case DIFFICULTY_EXTREME:  return "Extreme";
        default:                  return "Unknown";
    }
}
