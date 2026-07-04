#include "game.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *game_texture;
    SDL_Surface *surface;
    uint32_t *pixels;
    TTF_Font *ui_font;
    TTF_Font *ui_font_bold;
    int width, height;
} Renderer;

static const char *HUD_FONT_PATH = "C:\\Windows\\Fonts\\segoeui.ttf";

static SDL_Color color_rgba(Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
    SDL_Color color = {red, green, blue, alpha};
    return color;
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void draw_filled_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_outlined_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!font || !text) {
        return;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) {
        return;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void draw_text_centered(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect bounds, SDL_Color color) {
    if (!font || !text) {
        return;
    }

    int text_width = 0;
    int text_height = 0;
    if (TTF_SizeUTF8(font, text, &text_width, &text_height) != 0) {
        return;
    }

    int x = bounds.x + (bounds.w - text_width) / 2;
    int y = bounds.y + (bounds.h - text_height) / 2;
    draw_text(renderer, font, text, x, y, color);
}

static void draw_shadowed_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color fill, SDL_Color shadow) {
    SDL_Rect shadow_rect = {rect.x + 6, rect.y + 6, rect.w, rect.h};
    draw_filled_rect(renderer, shadow_rect, shadow);
    draw_filled_rect(renderer, rect, fill);
}

static void draw_panel(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color fill, SDL_Color outline) {
    draw_shadowed_rect(renderer, rect, fill, color_rgba(0, 0, 0, 70));
    draw_outlined_rect(renderer, rect, outline);
}

static void draw_gradient_background(Renderer *r) {
    for (int y = 0; y < r->height; y += 4) {
        Uint8 red = (Uint8)(11 + (y * 10) / r->height);
        Uint8 green = (Uint8)(16 + (y * 22) / r->height);
        Uint8 blue = (Uint8)(28 + (y * 34) / r->height);
        draw_filled_rect(r->renderer, (SDL_Rect){0, y, r->width, 4}, color_rgba(red, green, blue, 255));
    }

    draw_filled_rect(r->renderer, (SDL_Rect){0, 0, r->width, 120}, color_rgba(18, 30, 52, 80));
    draw_filled_rect(r->renderer, (SDL_Rect){0, 540, r->width, 180}, color_rgba(4, 8, 16, 120));
}

static void draw_glow_circle(Renderer *r, int cx, int cy, int radius, SDL_Color inner, SDL_Color outer) {
    for (int ring = radius + 10; ring >= radius; ring--) {
        Uint8 alpha = (Uint8)(outer.a * (radius + 10 - ring) / 10);
        SDL_SetRenderDrawColor(r->renderer, outer.r, outer.g, outer.b, alpha);
        for (int y = -ring; y <= ring; y++) {
            for (int x = -ring; x <= ring; x++) {
                if (x * x + y * y <= ring * ring) {
                    SDL_RenderDrawPoint(r->renderer, cx + x, cy + y);
                }
            }
        }
    }

    SDL_SetRenderDrawColor(r->renderer, inner.r, inner.g, inner.b, inner.a);
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                SDL_RenderDrawPoint(r->renderer, cx + x, cy + y);
            }
        }
    }
}

static void draw_glow_rect(Renderer *r, SDL_Rect rect, SDL_Color fill, SDL_Color glow) {
    SDL_Rect halo = {rect.x - 6, rect.y - 6, rect.w + 12, rect.h + 12};
    draw_filled_rect(r->renderer, halo, glow);
    draw_filled_rect(r->renderer, rect, fill);
}

static TTF_Font* open_font_or_null(const char *path, int size) {
    TTF_Font *font = TTF_OpenFont(path, size);
    if (!font) {
        fprintf(stderr, "Font load failed (%s, %d): %s\n", path, size, TTF_GetError());
    }
    return font;
}

Renderer* rendering_init(void) {
    Renderer *r = (Renderer *)malloc(sizeof(Renderer));
    if (!r) {
        return NULL;
    }
    
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
    r->ui_font = open_font_or_null(HUD_FONT_PATH, 18);
    r->ui_font_bold = open_font_or_null(HUD_FONT_PATH, 24);

    if (!r->ui_font || !r->ui_font_bold) {
        if (r->ui_font) TTF_CloseFont(r->ui_font);
        if (r->ui_font_bold) TTF_CloseFont(r->ui_font_bold);
        SDL_DestroyRenderer(r->renderer);
        SDL_DestroyWindow(r->window);
        SDL_Quit();
        free(r);
        return NULL;
    }
    
    return r;
}

void rendering_clear(Renderer *r) {
    SDL_SetRenderDrawColor(r->renderer, 8, 12, 20, 255);
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
    (void)r;
    (void)text;
    (void)x;
    (void)y;
    SDL_SetRenderDrawColor(r->renderer, r_val, g_val, b_val, 255);
}

void rendering_draw_game(Renderer *r, GameState *state, const char *difficulty_name) {
    rendering_clear(r);
    draw_gradient_background(r);

    SDL_Rect court_shadow = {COURT_X - 4, COURT_Y - 4, COURT_WIDTH + 8, COURT_HEIGHT + 8};
    draw_filled_rect(r->renderer, court_shadow, color_rgba(0, 0, 0, 90));
    
    // Draw court background
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT}, color_rgba(15, 28, 42, 255));
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, 3}, color_rgba(67, 220, 255, 180));
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y + COURT_HEIGHT - 3, COURT_WIDTH, 3}, color_rgba(67, 220, 255, 140));
    
    // Draw court lines
    SDL_SetRenderDrawColor(r->renderer, 120, 235, 255, 220);
    
    // Center line
    SDL_RenderDrawLine(r->renderer, COURT_X + COURT_WIDTH/2, COURT_Y, COURT_X + COURT_WIDTH/2, COURT_Y + COURT_HEIGHT);
    
    // Net
    for (int i = 0; i < 20; i++) {
        SDL_RenderDrawLine(r->renderer, 
                           COURT_X + COURT_WIDTH/2, COURT_Y + i*30,
                           COURT_X + COURT_WIDTH/2 + 8, COURT_Y + i*30);
    }
    
    // Boundary lines
    draw_outlined_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT}, color_rgba(120, 235, 255, 220));
    draw_outlined_rect(r->renderer, (SDL_Rect){COURT_X + 2, COURT_Y + 2, COURT_WIDTH - 4, COURT_HEIGHT - 4}, color_rgba(255, 255, 255, 24));
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X + COURT_WIDTH/2 - 1, COURT_Y + 30, 2, COURT_HEIGHT - 60}, color_rgba(67, 220, 255, 90));
    
    // Draw player paddle (right side) - GREEN
    draw_glow_rect(r,
                   (SDL_Rect){(int)state->player.x, (int)state->player.y, state->player.width, state->player.height},
                   color_rgba(41, 247, 154, 255),
                   color_rgba(41, 247, 154, 40));
    
    // Draw opponent paddle (left side) - BLUE
    draw_glow_rect(r,
                   (SDL_Rect){(int)state->opponent.x, (int)state->opponent.y, state->opponent.width, state->opponent.height},
                   color_rgba(76, 146, 255, 255),
                   color_rgba(76, 146, 255, 40));
    
    // Draw ball - YELLOW
    draw_glow_circle(r, (int)state->ball.x, (int)state->ball.y, state->ball.radius,
                     color_rgba(255, 232, 120, 255),
                     color_rgba(255, 232, 120, 50));
    
    // Draw particles
    for (int i = 0; i < state->particle_count; i++) {
        Particle *p = &state->particles[i];
        uint8_t alpha = (uint8_t)((p->lifetime / (float)p->max_lifetime) * 255);
        SDL_SetRenderDrawColor(r->renderer, p->r, p->g, p->b, alpha);
        SDL_RenderDrawPoint(r->renderer, (int)p->x, (int)p->y);
    }

    SDL_Rect top_bar = {40, 18, GAME_WIDTH - 80, 92};
    draw_panel(r->renderer, top_bar, color_rgba(15, 22, 36, 190), color_rgba(255, 255, 255, 24));
    draw_text(r->renderer, r->ui_font_bold, "Hand Tennis", 66, 32, color_rgba(247, 250, 255, 255));
    draw_text(r->renderer, r->ui_font, difficulty_name, 66, 64, color_rgba(146, 170, 206, 255));

    char score_text[128];
    snprintf(score_text, sizeof(score_text), "You %d  -  %d CPU", state->player_score, state->opponent_score);
    SDL_Rect score_chip = {520, 34, 260, 52};
    draw_panel(r->renderer, score_chip, color_rgba(22, 35, 56, 210), color_rgba(67, 220, 255, 50));
    draw_text_centered(r->renderer, r->ui_font_bold, score_text, score_chip, color_rgba(247, 250, 255, 255));

    char stat_text[96];
    snprintf(stat_text, sizeof(stat_text), "FPS %.0f", state->fps);
    SDL_Rect fps_chip = {808, 34, 120, 52};
    draw_panel(r->renderer, fps_chip, color_rgba(22, 35, 56, 210), color_rgba(255, 255, 255, 28));
    draw_text_centered(r->renderer, r->ui_font_bold, stat_text, fps_chip, color_rgba(247, 250, 255, 255));

    int panel_x = GAME_WIDTH - 364;
    SDL_Rect tracking_panel = {panel_x, 132, 324, 520};
    draw_panel(r->renderer, tracking_panel, color_rgba(14, 20, 32, 210), color_rgba(67, 220, 255, 48));
    draw_text(r->renderer, r->ui_font_bold, "Hand Tracking", tracking_panel.x + 20, tracking_panel.y + 18, color_rgba(247, 250, 255, 255));

    SDL_Rect status_chip = {tracking_panel.x + 20, tracking_panel.y + 58, 128, 34};
    SDL_Color status_fill = state->hand.tracking_confidence > 30 ? color_rgba(28, 84, 58, 255) : color_rgba(86, 34, 42, 255);
    SDL_Color status_outline = state->hand.tracking_confidence > 30 ? color_rgba(41, 247, 154, 96) : color_rgba(255, 92, 120, 96);
    draw_panel(r->renderer, status_chip, status_fill, status_outline);
    draw_text_centered(r->renderer, r->ui_font, state->hand.tracking_confidence > 30 ? "DETECTED" : "SEARCHING", status_chip,
                        state->hand.tracking_confidence > 30 ? color_rgba(179, 255, 218, 255) : color_rgba(255, 190, 198, 255));

    draw_text(r->renderer, r->ui_font, "Confidence", tracking_panel.x + 20, tracking_panel.y + 114, color_rgba(146, 170, 206, 255));
    SDL_Rect confidence_bg = {tracking_panel.x + 20, tracking_panel.y + 140, 284, 16};
    draw_filled_rect(r->renderer, confidence_bg, color_rgba(24, 33, 50, 255));
    int confidence_width = clamp_int((int)(284 * state->hand.tracking_confidence / 100.0f), 0, 284);
    draw_filled_rect(r->renderer, (SDL_Rect){tracking_panel.x + 20, tracking_panel.y + 140, confidence_width, 16},
                     state->hand.tracking_confidence > 30 ? color_rgba(41, 247, 154, 255) : color_rgba(255, 92, 120, 255));
    draw_outlined_rect(r->renderer, confidence_bg, color_rgba(255, 255, 255, 28));

    draw_text(r->renderer, r->ui_font, "Hand position", tracking_panel.x + 20, tracking_panel.y + 182, color_rgba(146, 170, 206, 255));
    SDL_Rect guide = {tracking_panel.x + 20, tracking_panel.y + 214, 284, 214};
    draw_filled_rect(r->renderer, guide, color_rgba(19, 28, 42, 255));
    draw_outlined_rect(r->renderer, guide, color_rgba(255, 255, 255, 22));
    draw_filled_rect(r->renderer, (SDL_Rect){guide.x + 8, guide.y + 8, guide.w - 16, guide.h - 16}, color_rgba(9, 14, 22, 255));
    for (int i = 0; i <= 10; i++) {
        int mark_y = guide.y + 8 + (guide.h - 16) * i / 10;
        SDL_SetRenderDrawColor(r->renderer, 255, 255, 255, i == 5 ? 70 : 18);
        SDL_RenderDrawLine(r->renderer, guide.x + 8, mark_y, guide.x + guide.w - 8, mark_y);
    }

    float normalized_y = (float)clamp_int((int)state->hand.smoothed_y, 0, 100);
    int marker_y = guide.y + 8 + (int)((guide.h - 16) * normalized_y / 100.0f);
    draw_filled_rect(r->renderer, (SDL_Rect){guide.x + 18, marker_y - 10, guide.w - 36, 20},
                     state->hand.tracking_confidence > 30 ? color_rgba(67, 220, 255, 110) : color_rgba(255, 92, 120, 110));
    draw_text_centered(r->renderer, r->ui_font_bold, "HAND", (SDL_Rect){guide.x + 18, marker_y - 10, guide.w - 36, 20},
                       color_rgba(255, 255, 255, 255));

    char hand_x_text[64];
    char hand_y_text[64];
    char smooth_text[64];
    snprintf(hand_x_text, sizeof(hand_x_text), "X %.0f%%", state->hand.x);
    snprintf(hand_y_text, sizeof(hand_y_text), "Y %.0f%%", state->hand.y);
    snprintf(smooth_text, sizeof(smooth_text), "Smooth Y %.0f%%", state->hand.smoothed_y);
    draw_text(r->renderer, r->ui_font, hand_x_text, tracking_panel.x + 20, tracking_panel.y + 446, color_rgba(247, 250, 255, 255));
    draw_text(r->renderer, r->ui_font, hand_y_text, tracking_panel.x + 20, tracking_panel.y + 474, color_rgba(247, 250, 255, 255));
    draw_text(r->renderer, r->ui_font, smooth_text, tracking_panel.x + 20, tracking_panel.y + 502, color_rgba(146, 170, 206, 255));

    SDL_Rect hint = {tracking_panel.x + 20, tracking_panel.y + 536, 284, 44};
    draw_panel(r->renderer, hint, color_rgba(18, 26, 40, 220), color_rgba(255, 255, 255, 18));
    draw_text_centered(r->renderer, r->ui_font, "Move your hand higher or lower", hint, color_rgba(179, 193, 219, 255));
    
    // Game over screen
    if (state->game_over) {
        // Semi-transparent overlay
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 200);
        SDL_Rect overlay = {0, 0, GAME_WIDTH, GAME_HEIGHT};
        SDL_RenderFillRect(r->renderer, &overlay);
        
        // Winner message background
        draw_panel(r->renderer, (SDL_Rect){GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200},
                   color_rgba(18, 26, 40, 240), color_rgba(67, 220, 255, 64));
        
        // Borders
        SDL_SetRenderDrawColor(r->renderer, state->game_won ? 41 : 255, state->game_won ? 247 : 92, state->game_won ? 154 : 120, 255);
        SDL_Rect border = {GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200};
        SDL_RenderDrawRect(r->renderer, &border);

        draw_text_centered(r->renderer, r->ui_font_bold, state->game_won ? "YOU WIN" : "GAME OVER",
                           (SDL_Rect){GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 74, 400, 68},
                           color_rgba(247, 250, 255, 255));
        draw_text_centered(r->renderer, r->ui_font, "Press SPACE to restart",
                           (SDL_Rect){GAME_WIDTH/2 - 200, GAME_HEIGHT/2 + 20, 400, 44},
                           color_rgba(179, 193, 219, 255));
    }
    
    SDL_RenderPresent(r->renderer);
}

void rendering_cleanup(Renderer *r) {
    if (!r) return;
    
    if (r->ui_font) TTF_CloseFont(r->ui_font);
    if (r->ui_font_bold) TTF_CloseFont(r->ui_font_bold);
    if (r->renderer) SDL_DestroyRenderer(r->renderer);
    if (r->window) SDL_DestroyWindow(r->window);
    SDL_Quit();
    
    free(r);
}
