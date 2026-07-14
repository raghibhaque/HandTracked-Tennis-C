#include "game.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Forward declarations
void physics_update_ball(GameState *state);
void physics_check_paddle_collisions(GameState *state, Particle *particles, int *particle_count);
void physics_update_particles(Particle *particles, int *particle_count);
void ai_opponent_update(GameState *state, int frame_count);

GameState* game_init(Difficulty difficulty) {
    GameState *state = (GameState *)malloc(sizeof(GameState));
    
    state->difficulty = difficulty;
    state->player_score = 0;
    state->opponent_score = 0;
    state->max_score = 11;  // First to 11
    state->game_over = false;
    state->game_won = false;
    state->frame_count = 0;
    state->fps = 0.0f;
    state->particle_count = 0;
    
    // Initialize player paddle (right side)
    state->player.x = COURT_X + COURT_WIDTH - PADDLE_WIDTH - 10;
    state->player.y = COURT_Y + COURT_HEIGHT / 2 - PADDLE_HEIGHT / 2;
    state->player.width = PADDLE_WIDTH;
    state->player.height = PADDLE_HEIGHT;
    state->player.vy = 0;
    
    // Initialize opponent paddle (left side)
    state->opponent.x = COURT_X + 10;
    state->opponent.y = COURT_Y + COURT_HEIGHT / 2 - PADDLE_HEIGHT / 2;
    state->opponent.width = PADDLE_WIDTH;
    state->opponent.height = PADDLE_HEIGHT;
    state->opponent.vy = 0;
    
    // Initialize hand tracker
    state->hand.x = 0;
    state->hand.y = 0;
    state->hand.detected = false;
    state->hand.smoothed_x = 50.0f;
    state->hand.smoothed_y = 50.0f;
    state->hand.tracking_confidence = 0;
    
    // Initialize ball
    game_reset_ball(state);
    
    return state;
}

void game_reset_ball(GameState *state) {
    Ball *ball = &state->ball;
    
    // Reset to center
    ball->x = COURT_X + COURT_WIDTH / 2;
    ball->y = COURT_Y + COURT_HEIGHT / 2;
    
    // Random direction
    int direction = rand() % 2 == 0 ? 1 : -1;
    ball->vx = direction * BALL_SPEED_X + (rand() % 4 - 2);
    ball->vy = (rand() % 6 - 3);
    ball->radius = BALL_RADIUS;
}

void game_update(GameState *state, Hand *hand) {
    if (state->game_over) return;
    
    state->frame_count++;
    
    // Update hand tracking with exponential smoothing
    if (hand->detected) {
        state->hand.detected = true;
        state->hand.x = hand->x;
        state->hand.y = hand->y;
        state->hand.tracking_confidence = (state->hand.tracking_confidence + 2) > 100 ? 100 : state->hand.tracking_confidence + 2;

        // Exponential smoothing for hand position
        state->hand.smoothed_x = state->hand.smoothed_x * 0.8f + hand->x * 0.2f;
        state->hand.smoothed_y = state->hand.smoothed_y * 0.8f + hand->y * 0.2f;
    } else {
        state->hand.tracking_confidence = (state->hand.tracking_confidence - 1) < 0 ? 0 : state->hand.tracking_confidence - 1;
    }

    // Map normalized camera position (0-100) to game coordinates.
    if (state->hand.tracking_confidence > 30) {
        // Center paddle on the detected hand position
        float game_y = COURT_Y + (state->hand.smoothed_y / 100.0f) * COURT_HEIGHT - PADDLE_HEIGHT / 2.0f;

        // Clamp to valid range
        if (game_y < PADDLE_MIN_Y) game_y = PADDLE_MIN_Y;
        if (game_y > PADDLE_MAX_Y) game_y = PADDLE_MAX_Y;

        float old_y = state->player.y;
        state->player.y = game_y;
        state->player.vy = state->player.y - old_y;  // Track velocity for spin physics
    } else {
        // No hand detected, apply damping
        state->player.vy *= 0.9f;
    }
    
    // Clamp player paddle to bounds
    if (state->player.y < PADDLE_MIN_Y) state->player.y = PADDLE_MIN_Y;
    if (state->player.y > PADDLE_MAX_Y) state->player.y = PADDLE_MAX_Y;
    
    // Update ball physics
    physics_update_ball(state);
    
    // Check collisions
    physics_check_paddle_collisions(state, state->particles, &state->particle_count);
    
    // Update opponent AI
    ai_opponent_update(state, state->frame_count);
    
    // Apply opponent paddle velocity and clamp
    state->opponent.y += state->opponent.vy;
    if (state->opponent.y < PADDLE_MIN_Y) state->opponent.y = PADDLE_MIN_Y;
    if (state->opponent.y > PADDLE_MAX_Y) state->opponent.y = PADDLE_MAX_Y;
    
    // Update particles
    physics_update_particles(state->particles, &state->particle_count);
    
    // Check win condition
    if (state->player_score >= state->max_score) {
        state->game_over = true;
        state->game_won = true;
    }
    if (state->opponent_score >= state->max_score) {
        state->game_over = true;
        state->game_won = false;
    }
}

void game_cleanup(GameState *state) {
    if (state) free(state);
}
