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

    float slow = (state->slow_mo_frames > 0) ? 0.5f : 1.0f;

    // Apply gravity (subtle)
    ball->vy += 0.3f * slow;

    // Update position (slow-mo scales displacement)
    ball->x += ball->vx * slow;
    ball->y += ball->vy * slow;
    
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
    // Player is on LEFT; opponent is on RIGHT.
    // Ball exits left  = player missed  = opponent scores.
    // Ball exits right = opponent missed = player scores.
    if (ball->x < COURT_X - 50) {
        state->opponent_score++;
        if (state->rally_count > state->rally_best) state->rally_best = state->rally_count;
        state->rally_count = 0;
        game_reset_ball(state);
        return;
    }
    if (ball->x > COURT_X + COURT_WIDTH + 50) {
        state->player_score++;
        if (state->rally_count > state->rally_best) state->rally_best = state->rally_count;
        state->rally_count = 0;
        game_reset_ball(state);
        return;
    }
}

// Check and resolve paddle collisions
void physics_check_paddle_collisions(GameState *state, Particle *particles, int *particle_count) {
    Ball *ball = &state->ball;
    
    // Player paddle collision (player on LEFT — only collide when ball moving LEFT toward player)
    if (check_ball_paddle_collision(ball, &state->player) && ball->vx < 0) {
        // Place ball to the right face of player paddle
        ball->x = state->player.x + state->player.width + ball->radius;
        ball->vx = -ball->vx;

        // Add spin based on paddle velocity
        ball->vy += state->player.vy * 0.5f;

        // Rally combo — every hit advances the meter, milestone flashes HUD
        state->rally_count++;
        if (state->rally_count > 0 && state->rally_count % 5 == 0) {
            state->rally_flash = 30;
        }

        // Increase speed slightly, clamped to MAX_BALL_SPEED. Rally adds bonus.
        float rally_bonus = 1.0f + (state->rally_count * 0.01f);
        if (rally_bonus > 1.15f) rally_bonus = 1.15f;
        float speed = sqrtf(ball->vx * ball->vx + ball->vy * ball->vy);
        if (speed > 0.0f && speed < MAX_BALL_SPEED) {
            float new_speed = speed * 1.05f * rally_bonus;
            if (new_speed > MAX_BALL_SPEED) new_speed = MAX_BALL_SPEED;
            ball->vx = ball->vx * new_speed / speed;
            ball->vy = ball->vy * new_speed / speed;
        }

        // Create particle effect (guard against overflow: need room for 8 more)
        if (*particle_count + 8 <= 256) {
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

    // Opponent paddle collision (opponent on RIGHT — only collide when ball moving RIGHT toward opponent)
    if (check_ball_paddle_collision(ball, &state->opponent) && ball->vx > 0) {
        // Place ball to the left face of opponent paddle
        ball->x = state->opponent.x - ball->radius;
        ball->vx = -ball->vx;

        // Add spin based on paddle velocity
        ball->vy += state->opponent.vy * 0.5f;

        // Rally combo — opponent return also counts
        state->rally_count++;
        if (state->rally_count > 0 && state->rally_count % 5 == 0) {
            state->rally_flash = 30;
        }

        // Increase speed slightly, clamped to MAX_BALL_SPEED. Rally adds bonus.
        float rally_bonus = 1.0f + (state->rally_count * 0.01f);
        if (rally_bonus > 1.15f) rally_bonus = 1.15f;
        float speed = sqrtf(ball->vx * ball->vx + ball->vy * ball->vy);
        if (speed > 0.0f && speed < MAX_BALL_SPEED) {
            float new_speed = speed * 1.05f * rally_bonus;
            if (new_speed > MAX_BALL_SPEED) new_speed = MAX_BALL_SPEED;
            ball->vx = ball->vx * new_speed / speed;
            ball->vy = ball->vy * new_speed / speed;
        }

        // Create particle effect (guard against overflow: need room for 8 more)
        if (*particle_count + 8 <= 256) {
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

// Simulate extra multi-balls: bounce off walls + paddles, no scoring, vanish when past the court edges.
void physics_update_extra_balls(GameState *state) {
    float slow = (state->slow_mo_frames > 0) ? 0.5f : 1.0f;

    int write = 0;
    for (int i = 0; i < state->extra_ball_count; i++) {
        Ball *b = &state->extra_balls[i];

        b->vy += 0.3f * slow;
        b->x  += b->vx * slow;
        b->y  += b->vy * slow;

        // Top/bottom bounce
        if (b->y - b->radius <= COURT_Y) {
            b->y = COURT_Y + b->radius;
            b->vy = -b->vy * 0.95f;
        }
        if (b->y + b->radius >= COURT_Y + COURT_HEIGHT) {
            b->y = COURT_Y + COURT_HEIGHT - b->radius;
            b->vy = -b->vy * 0.95f;
        }

        // Paddle bounces (no scoring bookkeeping)
        if (check_ball_paddle_collision(b, &state->player) && b->vx < 0) {
            b->x = state->player.x + state->player.width + b->radius;
            b->vx = -b->vx;
            b->vy += state->player.vy * 0.5f;
        }
        if (check_ball_paddle_collision(b, &state->opponent) && b->vx > 0) {
            b->x = state->opponent.x - b->radius;
            b->vx = -b->vx;
            b->vy += state->opponent.vy * 0.5f;
        }

        // Kill if it left the court
        if (b->x < COURT_X - 50 || b->x > COURT_X + COURT_WIDTH + 50) {
            continue;
        }

        state->extra_balls[write++] = *b;
    }
    state->extra_ball_count = write;
}

// Spawn a fresh power-up in mid-court
static void spawn_powerup(GameState *state) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!state->powerups[i].active) {
            state->powerups[i].active = true;
            state->powerups[i].x = COURT_X + COURT_WIDTH / 2 + (rand() % 200 - 100);
            state->powerups[i].y = COURT_Y + 60 + (rand() % (COURT_HEIGHT - 120));
            state->powerups[i].type = (PowerUpType)(rand() % POWERUP_TYPE_COUNT);
            state->powerups[i].radius = POWERUP_RADIUS;
            state->powerups[i].spin = 0;
            return;
        }
    }
}

// Check circle-vs-circle overlap
static bool ball_hits_powerup(Ball *b, PowerUp *p) {
    float dx = b->x - p->x;
    float dy = b->y - p->y;
    float r = (float)(b->radius + p->radius);
    return (dx * dx + dy * dy) <= r * r;
}

// Apply a power-up effect triggered by ball contact
static void activate_powerup(GameState *state, PowerUp *p, Ball *trigger) {
    switch (p->type) {
        case POWERUP_MULTIBALL:
            for (int k = 0; k < 2 && state->extra_ball_count < MAX_EXTRA_BALLS; k++) {
                Ball *nb = &state->extra_balls[state->extra_ball_count++];
                nb->x = p->x;
                nb->y = p->y;
                nb->radius = BALL_RADIUS;
                float sign = (k == 0) ? 1.0f : -1.0f;
                nb->vx = trigger->vx * (0.9f + 0.2f * ((rand() % 100) / 100.0f));
                nb->vy = sign * (2.0f + (rand() % 40) / 10.0f);
            }
            break;
        case POWERUP_BIG_PADDLE:
            state->big_paddle_frames = BIG_PADDLE_FRAMES;
            break;
        case POWERUP_SLOW_MO:
            state->slow_mo_frames = SLOW_MO_FRAMES;
            break;
        default:
            break;
    }
}

void physics_update_powerups(GameState *state) {
    // Spawn cooldown
    if (state->powerup_spawn_cooldown > 0) {
        state->powerup_spawn_cooldown--;
    } else {
        // Count active
        int active = 0;
        for (int i = 0; i < MAX_POWERUPS; i++) if (state->powerups[i].active) active++;
        if (active < MAX_POWERUPS) {
            spawn_powerup(state);
        }
        state->powerup_spawn_cooldown = POWERUP_SPAWN_COOLDOWN;
    }

    // Check collision with primary + extra balls
    for (int i = 0; i < MAX_POWERUPS; i++) {
        PowerUp *p = &state->powerups[i];
        if (!p->active) continue;
        p->spin++;

        if (ball_hits_powerup(&state->ball, p)) {
            activate_powerup(state, p, &state->ball);
            p->active = false;
            continue;
        }
        for (int j = 0; j < state->extra_ball_count; j++) {
            if (ball_hits_powerup(&state->extra_balls[j], p)) {
                activate_powerup(state, p, &state->extra_balls[j]);
                p->active = false;
                break;
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
