#include "game.h"
#include "hand_tracker.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

// Right panel layout (derived from game.h constants)
#define PANEL_X    (COURT_X + COURT_WIDTH + 20)
#define PANEL_W    (GAME_WIDTH - PANEL_X - 5)

// Camera preview box inside panel
#define CAM_MARGIN 18
#define CAM_X      (PANEL_X + CAM_MARGIN)
#define CAM_W      (PANEL_W - 2 * CAM_MARGIN)
#define CAM_H      (CAM_W * 3 / 4)
#define CAM_Y      18

// Status chip (below camera)
#define STATUS_Y   (CAM_Y + CAM_H + 8)
#define STATUS_H   38

// Confidence bar
#define CONF_Y     (STATUS_Y + STATUS_H + 6)
#define CONF_H     10

// Score panel
#define SCORE_Y    (CONF_Y + CONF_H + 14)
#define SCORE_H    118

// Difficulty + FPS strip
#define DIFF_Y     (SCORE_Y + SCORE_H + 10)
#define DIFF_H     44

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
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

SDL_Renderer* rendering_get_sdl_renderer(Renderer *r) {
    return r ? r->renderer : NULL;
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

void rendering_draw_game(Renderer *r, GameState *state, const char *difficulty_name, HandTracker *tracker) {
    rendering_clear(r);
    draw_gradient_background(r);

    // ── Left side: tennis court ─────────────────────────────────────

    // Court shadow
    SDL_Rect court_shadow = {COURT_X - 4, COURT_Y - 4, COURT_WIDTH + 8, COURT_HEIGHT + 8};
    draw_filled_rect(r->renderer, court_shadow, color_rgba(0, 0, 0, 90));

    // Court surface
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT},
                     color_rgba(15, 28, 42, 255));
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, 3},
                     color_rgba(67, 220, 255, 180));
    draw_filled_rect(r->renderer, (SDL_Rect){COURT_X, COURT_Y + COURT_HEIGHT - 3, COURT_WIDTH, 3},
                     color_rgba(67, 220, 255, 140));

    // Court lines
    SDL_SetRenderDrawColor(r->renderer, 120, 235, 255, 220);
    SDL_RenderDrawLine(r->renderer, COURT_X + COURT_WIDTH/2, COURT_Y,
                                   COURT_X + COURT_WIDTH/2, COURT_Y + COURT_HEIGHT);
    // Net dashes
    for (int i = 0; i < 20; i++) {
        SDL_RenderDrawLine(r->renderer,
                           COURT_X + COURT_WIDTH/2,     COURT_Y + i * 30,
                           COURT_X + COURT_WIDTH/2 + 8, COURT_Y + i * 30);
    }
    draw_outlined_rect(r->renderer,
                       (SDL_Rect){COURT_X, COURT_Y, COURT_WIDTH, COURT_HEIGHT},
                       color_rgba(120, 235, 255, 220));
    draw_outlined_rect(r->renderer,
                       (SDL_Rect){COURT_X + 2, COURT_Y + 2, COURT_WIDTH - 4, COURT_HEIGHT - 4},
                       color_rgba(255, 255, 255, 24));
    draw_filled_rect(r->renderer,
                     (SDL_Rect){COURT_X + COURT_WIDTH/2 - 1, COURT_Y + 30, 2, COURT_HEIGHT - 60},
                     color_rgba(67, 220, 255, 90));

    // Small title above court
    draw_text(r->renderer, r->ui_font_bold, "Hand Tennis",
              COURT_X + 6, 14, color_rgba(247, 250, 255, 255));
    draw_text(r->renderer, r->ui_font, "ESC: quit   SPACE: restart",
              COURT_X + 6, 38, color_rgba(146, 170, 206, 180));

    // Player paddle (right, GREEN)
    draw_glow_rect(r,
                   (SDL_Rect){(int)state->player.x, (int)state->player.y,
                               state->player.width, state->player.height},
                   color_rgba(41, 247, 154, 255),
                   color_rgba(41, 247, 154, 40));

    // Opponent paddle (left, BLUE)
    draw_glow_rect(r,
                   (SDL_Rect){(int)state->opponent.x, (int)state->opponent.y,
                               state->opponent.width, state->opponent.height},
                   color_rgba(76, 146, 255, 255),
                   color_rgba(76, 146, 255, 40));

    // Ball (YELLOW)
    draw_glow_circle(r, (int)state->ball.x, (int)state->ball.y, state->ball.radius,
                     color_rgba(255, 232, 120, 255),
                     color_rgba(255, 232, 120, 50));

    // Particles
    for (int i = 0; i < state->particle_count; i++) {
        Particle *p = &state->particles[i];
        uint8_t alpha = (uint8_t)((p->lifetime / (float)p->max_lifetime) * 255);
        SDL_SetRenderDrawColor(r->renderer, p->r, p->g, p->b, alpha);
        SDL_RenderDrawPoint(r->renderer, (int)p->x, (int)p->y);
    }

    // ── Vertical divider ────────────────────────────────────────────
    SDL_SetRenderDrawColor(r->renderer, 35, 50, 75, 255);
    SDL_RenderDrawLine(r->renderer, PANEL_X - 8, 0, PANEL_X - 8, GAME_HEIGHT);
    SDL_SetRenderDrawColor(r->renderer, 20, 30, 48, 255);
    SDL_RenderDrawLine(r->renderer, PANEL_X - 7, 0, PANEL_X - 7, GAME_HEIGHT);

    // ── Right panel background ──────────────────────────────────────
    draw_filled_rect(r->renderer, (SDL_Rect){PANEL_X, 0, PANEL_W, GAME_HEIGHT},
                     color_rgba(9, 14, 24, 255));

    // ── Camera preview ──────────────────────────────────────────────
    draw_text(r->renderer, r->ui_font, "CAMERA FEED",
              CAM_X, CAM_Y - 14, color_rgba(120, 150, 190, 200));

    SDL_Rect cam_rect = {CAM_X, CAM_Y, CAM_W, CAM_H};
    SDL_Texture *cam_tex = hand_tracker_get_preview_texture(tracker);
    bool tracked = state->hand.tracking_confidence > 30;

    if (cam_tex) {
        SDL_RenderCopy(r->renderer, cam_tex, NULL, &cam_rect);
    } else {
        draw_filled_rect(r->renderer, cam_rect, color_rgba(16, 22, 36, 255));
        draw_text_centered(r->renderer, r->ui_font_bold, "NO CAMERA", cam_rect,
                           color_rgba(255, 92, 120, 180));
    }
    // Camera border color reflects tracking state
    draw_outlined_rect(r->renderer, cam_rect,
                       tracked ? color_rgba(41, 247, 154, 200) : color_rgba(255, 92, 120, 180));

    // ── Tracking status chip ─────────────────────────────────────────
    SDL_Rect status_rect = {CAM_X, STATUS_Y, CAM_W, STATUS_H};
    SDL_Color status_fill    = tracked ? color_rgba(24, 74, 52, 255) : color_rgba(74, 28, 36, 255);
    SDL_Color status_outline = tracked ? color_rgba(41, 247, 154, 90) : color_rgba(255, 92, 120, 90);
    draw_panel(r->renderer, status_rect, status_fill, status_outline);
    draw_text_centered(r->renderer, r->ui_font_bold,
                       tracked ? "PALM DETECTED" : "SEARCHING FOR PALM...",
                       status_rect,
                       tracked ? color_rgba(160, 255, 205, 255) : color_rgba(255, 185, 195, 255));

    // ── Confidence bar ───────────────────────────────────────────────
    SDL_Rect conf_bg = {CAM_X, CONF_Y, CAM_W, CONF_H};
    draw_filled_rect(r->renderer, conf_bg, color_rgba(20, 28, 44, 255));
    int conf_w = clamp_int((int)(CAM_W * state->hand.tracking_confidence / 100.0f), 0, CAM_W);
    draw_filled_rect(r->renderer,
                     (SDL_Rect){CAM_X, CONF_Y, conf_w, CONF_H},
                     tracked ? color_rgba(41, 247, 154, 255) : color_rgba(255, 92, 120, 255));
    draw_outlined_rect(r->renderer, conf_bg, color_rgba(255, 255, 255, 22));

    // ── Score panel ──────────────────────────────────────────────────
    SDL_Rect score_panel = {CAM_X, SCORE_Y, CAM_W, SCORE_H};
    draw_panel(r->renderer, score_panel, color_rgba(12, 20, 34, 220),
               color_rgba(67, 220, 255, 44));

    draw_text_centered(r->renderer, r->ui_font, "SCORE",
                       (SDL_Rect){CAM_X, SCORE_Y + 8, CAM_W, 22},
                       color_rgba(120, 150, 190, 200));

    char score_text[64];
    snprintf(score_text, sizeof(score_text), "%d  \xe2\x80\x94  %d", state->player_score, state->opponent_score);
    draw_text_centered(r->renderer, r->ui_font_bold, score_text,
                       (SDL_Rect){CAM_X, SCORE_Y + 32, CAM_W, 52},
                       color_rgba(247, 250, 255, 255));

    // YOU / CPU labels aligned under score numbers
    int half = CAM_W / 2;
    draw_text_centered(r->renderer, r->ui_font, "YOU",
                       (SDL_Rect){CAM_X, SCORE_Y + 82, half, 28},
                       color_rgba(41, 247, 154, 255));
    draw_text_centered(r->renderer, r->ui_font, "CPU",
                       (SDL_Rect){CAM_X + half, SCORE_Y + 82, half, 28},
                       color_rgba(76, 146, 255, 255));

    // Win target
    draw_text_centered(r->renderer, r->ui_font, "First to 11",
                       (SDL_Rect){CAM_X, SCORE_Y + SCORE_H - 26, CAM_W, 22},
                       color_rgba(100, 130, 170, 180));

    // ── Difficulty + FPS strip ───────────────────────────────────────
    SDL_Rect diff_panel = {CAM_X, DIFF_Y, CAM_W, DIFF_H};
    draw_panel(r->renderer, diff_panel, color_rgba(12, 20, 34, 200),
               color_rgba(255, 255, 255, 18));

    char diff_fps[64];
    snprintf(diff_fps, sizeof(diff_fps), "%s  |  %.0f FPS", difficulty_name, state->fps);
    draw_text_centered(r->renderer, r->ui_font, diff_fps, diff_panel,
                       color_rgba(170, 188, 215, 255));

    // ── Game over overlay ─────────────────────────────────────────────
    if (state->game_over) {
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 200);
        SDL_Rect overlay = {0, 0, GAME_WIDTH, GAME_HEIGHT};
        SDL_RenderFillRect(r->renderer, &overlay);

        draw_panel(r->renderer,
                   (SDL_Rect){GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200},
                   color_rgba(18, 26, 40, 240), color_rgba(67, 220, 255, 64));

        SDL_SetRenderDrawColor(r->renderer,
                               state->game_won ? 41  : 255,
                               state->game_won ? 247 : 92,
                               state->game_won ? 154 : 120,
                               255);
        SDL_Rect border = {GAME_WIDTH/2 - 200, GAME_HEIGHT/2 - 100, 400, 200};
        SDL_RenderDrawRect(r->renderer, &border);

        draw_text_centered(r->renderer, r->ui_font_bold,
                           state->game_won ? "YOU WIN!" : "GAME OVER",
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
