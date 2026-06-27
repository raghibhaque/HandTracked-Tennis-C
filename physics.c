#include "game.h"
#include <math.h>
#include <stdlib.h>

// Clamp a value between min and max
static float clamp(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// Check collision between ball and paddle
static bool check_ball_paddle_collision(Ball *ball, Paddle *paddle) {
    // Ball rectangle
    float ball_left = ball->x - ball->radius;
    float ball_right = ball->x + ball->radius;
    float ball_top = ball->y - ball->radius;
    float ball_bottom = ball->y + ball->radius;
    
    // Paddle rectangle
    float paddle_left = paddle->x;
    float paddle_right = paddle->x + paddle->width;
    float paddle_top = paddle->y;
    float paddle_bottom = paddle->y + paddle->height;
    
    // AABB collision
    return !(ball_right < paddle_left || 
             ball_left > paddle_right || 
             ball_bottom < paddle_top || 
             ball_top > paddle_bottom);
}

// Update ball physics
void physics_update_ball(GameState *state) {
    Ball *ball = &state->ball;
    
    // Apply gravity (subtle)
    ball->vy += 0.3f;
    
    // Update position
    ball->x += ball->vx;
    ball->y += ball->vy;
    
    // Wall collisions (top/bottom)
    if (ball->y - ball->radius <= COURT_Y) {
        ball->y = COURT_Y + ball->radius;
        ball->vy = -ball->vy * 0.95f;  // Damping
    }
    if (ball->y + ball->radius >= COURT_Y + COURT_HEIGHT) {
        ball->y = COURT_Y + COURT_HEIGHT - ball->radius;
        ball->vy = -ball->vy * 0.95f;
    }
    
    // Ball out of bounds (left/right)
    if (ball->x < COURT_X - 50) {
        state->opponent_score++;
        game_reset_ball(state);
        return;
    }
    if (ball->x > COURT_X + COURT_WIDTH + 50) {
        state->player_score++;
        game_reset_ball(state);
        return;
    }
}

// Check and resolve paddle collisions
void physics_check_paddle_collisions(GameState *state, Particle *particles, int *particle_count) {
    Ball *ball = &state->ball;
    
    // Player paddle collision
    if (check_ball_paddle_collision(ball, &state->player)) {
        ball->x = state->player.x + state->player.width + ball->radius;
        ball->vx = -ball->vx;
        
        // Add spin based on paddle velocity
        ball->vy += state->player.vy * 0.5f;
        
        // Increase speed slightly
        float speed = sqrt(ball->vx * ball->vx + ball->vy * ball->vy);
        if (speed < MAX_BALL_SPEED) {
            ball->vx *= 1.05f;
            ball->vy *= 1.05f;
        }
        
        // Create particle effect
        if (*particle_count < 256) {
            for (int i = 0; i < 8; i++) {
                particles[*particle_count].x = ball->x;
                particles[*particle_count].y = ball->y;
                particles[*particle_count].vx = (rand() % 5 - 2) * 0.5f;
                particles[*particle_count].vy = (rand() % 5 - 2) * 0.5f;
                particles[*particle_count].r = 255;
                particles[*particle_count].g = 200;
                particles[*particle_count].b = 0;
                particles[*particle_count].lifetime = 30;
                particles[*particle_count].max_lifetime = 30;
                (*particle_count)++;
            }
        }
    }
    
    // Opponent paddle collision
    if (check_ball_paddle_collision(ball, &state->opponent)) {
        ball->x = state->opponent.x - ball->radius;
        ball->vx = -ball->vx;
        
        // Add spin based on paddle velocity
        ball->vy += state->opponent.vy * 0.5f;
        
        // Increase speed slightly
        float speed = sqrt(ball->vx * ball->vx + ball->vy * ball->vy);
        if (speed < MAX_BALL_SPEED) {
            ball->vx *= 1.05f;
            ball->vy *= 1.05f;
        }
        
        // Create particle effect
        if (*particle_count < 256) {
            for (int i = 0; i < 8; i++) {
                particles[*particle_count].x = ball->x;
                particles[*particle_count].y = ball->y;
                particles[*particle_count].vx = (rand() % 5 - 2) * 0.5f;
                particles[*particle_count].vy = (rand() % 5 - 2) * 0.5f;
                particles[*particle_count].r = 0;
                particles[*particle_count].g = 200;
                particles[*particle_count].b = 255;
                particles[*particle_count].lifetime = 30;
                particles[*particle_count].max_lifetime = 30;
                (*particle_count)++;
            }
        }
    }
}

// Update particles (visual effects)
void physics_update_particles(Particle *particles, int *particle_count) {
    for (int i = 0; i < *particle_count; i++) {
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy += 0.2f;  // Gravity
        particles[i].lifetime--;
    }
    
    // Remove dead particles
    int write_idx = 0;
    for (int i = 0; i < *particle_count; i++) {
        if (particles[i].lifetime > 0) {
            particles[write_idx++] = particles[i];
        }
    }
    *particle_count = write_idx;
}
