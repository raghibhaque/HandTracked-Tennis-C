#include "game.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *game_texture;
    SDL_Surface *surface;
    uint32_t *pixels;
    int width, height;
} Renderer;

Renderer* rendering_init(void) {
    Renderer *r = (Renderer *)malloc(sizeof(Renderer));
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        free(r);
        return NULL;
    }
    
    r->width = GAME_WIDTH;
    r->height = GAME_HEIGHT;
    
    r->window = SDL_CreateWindow(
        "Hand Tennis - Track Your Hand!",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        r->width, r->height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!r->window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(r);
        return NULL;
    }
    
    r->renderer = SDL_CreateRenderer(r->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r->renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(r->window);
        SDL_Quit();
        free(r);
        return NULL;
    }
    
    SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
    
    return r;
}

void rendering_clear(Renderer *r) {
    SDL_SetRenderDrawColor(r->renderer, 20, 20, 30, 255);
    SDL_RenderClear(r->renderer);
}

void rendering_draw_rect(Renderer *r, int x, int y, int w, int h, uint8_t red, uint8_t green, uint8_t blue) {
    SDL_SetRenderDrawColor(r->renderer, red, green, blue, 255);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r->renderer, &rect);
}

void rendering_draw_circle(Renderer *r, int cx, int cy, int radius, uint8_t red, uint8_t green, uint8_t blue) {
    SDL_SetRenderDrawColor(r->renderer, red, green, blue, 255);
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                SDL_RenderDrawPoint(r->renderer, cx + x, cy + y);
            }
        }
    }
}

void rendering_draw_text_simple(Renderer *r, const char *text, int x, int y, uint8_t r_val, uint8_t g_val, uint8_t b_val) {
    // Simple text rendering (single color pixels)
    // Note: This is a simplified version. For full text support, use SDL_ttf
    SDL_SetRenderDrawColor(r->renderer, r_val, g_val, b_val, 255);
}

void rendering_draw_game(Renderer *r, GameState *state, const char *difficulty_name) {
    rendering_clear(r);
    
    // Draw court background
    rendering_draw_rect(r, COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT, 30, 80, 40);
    
    // Draw court lines
    SDL_SetRenderDrawColor(r->renderer, 100, 150, 120, 255);
    
    // Center line
    SDL_RenderDrawLine(r->renderer, COURT_X + COURT_WIDTH/2, COURT_Y, COURT_X + COURT_WIDTH/2, COURT_Y + COURT_HEIGHT);
    
    // Net
    for (int i = 0; i < 20; i++) {
        SDL_RenderDrawLine(r->renderer, 
                           COURT_X + COURT_WIDTH/2, COURT_Y + i*30,
                           COURT_X + COURT_WIDTH/2 + 5, COURT_Y + i*30);
    }
    
    // Boundary lines
    SDL_RenderDrawRect(r->renderer, &(SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT});
    
    // Draw player paddle (right side) - GREEN
    rendering_draw_rect(r, 
                       (int)state->player.x, (int)state->player.y, 
                       state->player.width, state->player.height, 
                       0, 255, 100);
    
    // Draw opponent paddle (left side) - BLUE
    rendering_draw_rect(r, 
                       (int)state->opponent.x, (int)state->opponent.y, 
                       state->opponent.width, state->opponent.height, 
                       100, 150, 255);
    
    // Draw ball - YELLOW
    rendering_draw_circle(r, (int)state->ball.x, (int)state->ball.y, state->ball.radius, 255, 255, 0);
    
    // Draw particles
    for (int i = 0; i < state->particle_count; i++) {
        Particle *p = &state->particles[i];
        uint8_t alpha = (uint8_t)((p->lifetime / (float)p->max_lifetime) * 255);
        SDL_SetRenderDrawColor(r->renderer, p->r, p->g, p->b, alpha);
        SDL_RenderDrawPoint(r->renderer, (int)p->x, (int)p->y);
    }
    
    // Draw UI elements
    SDL_SetRenderDrawColor(r->renderer, 200, 200, 200, 255);
    
    // Score display (simple rectangles as placeholders for text)
    rendering_draw_rect(r, 100, 20, 200, 30, 50, 50, 80);  // Player score area
    rendering_draw_rect(r, GAME_WIDTH - 300, 20, 200, 30, 50, 50, 80);  // Opponent score area
    
    // Difficulty and hand tracking indicator
    rendering_draw_rect(r, GAME_WIDTH - 280, 60, 260, 25, 40, 40, 50);
    
    if (state->hand.tracking_confidence > 30) {
        rendering_draw_circle(r, GAME_WIDTH - 20, 72, 5, 0, 255, 0);  // Green indicator
    } else {
        rendering_draw_circle(r, GAME_WIDTH - 20, 72, 5, 255, 0, 0);  // Red indicator
    }
    
    // Game over screen
    if (state->game_over) {
        // Semi-transparent overlay
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 200);
        SDL_Rect overlay = {0, 0, GAME_WIDTH, GAME_HEIGHT};
        SDL_RenderFillRect(r->renderer, &overlay);
        
        // Winner message background
        rendering_draw_rect(r, GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200, 40, 40, 80);
        
        // Borders
        SDL_SetRenderDrawColor(r->renderer, state->game_won ? 0 : 255, state->game_won ? 255 : 0, 0, 255);
        SDL_Rect border = {GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200};
        SDL_RenderDrawRect(r->renderer, &border);
    }
    
    SDL_RenderPresent(r->renderer);
}

void rendering_cleanup(Renderer *r) {
    if (!r) return;
    
    if (r->renderer) SDL_DestroyRenderer(r->renderer);
    if (r->window) SDL_DestroyWindow(r->window);
    SDL_Quit();
    
    free(r);
}
