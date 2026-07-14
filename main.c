#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "game.h"
#include "hand_tracker.h"

// Forward declarations
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} Renderer;

Renderer* rendering_init(void);
SDL_Renderer* rendering_get_sdl_renderer(Renderer *r);
void rendering_draw_game(Renderer *r, GameState *state, const char *difficulty_name, HandTracker *tracker);
void rendering_cleanup(Renderer *r);

const char* ai_get_difficulty_name(Difficulty difficulty);

void print_usage(void) {
    printf("\n=== Hand Tennis Game ===\n");
    printf("Usage: hand_tennis [difficulty] [fps]\n\n");
    printf("Difficulty levels:\n");
    printf("  0 = Easy (slow opponent, forgiving)\n");
    printf("  1 = Medium (balanced gameplay)\n");
    printf("  2 = Hard (fast, predictive)\n");
    printf("  3 = Extreme (godlike AI)\n\n");
    printf("FPS caps:\n");
    printf("  30 = lower CPU usage\n");
    printf("  60 = smoother motion (default)\n\n");
    printf("Controls:\n");
    printf("  - Position your hand in front of the camera\n");
    printf("  - Move your hand up/down to move the paddle\n");
    printf("  - Green indicator = hand detected\n");
    printf("  - Red indicator = hand not detected\n\n");
    printf("Press ESC to quit, SPACE to restart after game over\n\n");
}

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *title_font;
    TTF_Font *body_font;
    TTF_Font *button_font;
    bool retry_clicked;
    bool quit_clicked;
} CameraPrompt;

static const char *PROMPT_FONT_PATH = "C:\\Windows\\Fonts\\segoeui.ttf";

static SDL_Color rgb(Uint8 red, Uint8 green, Uint8 blue) {
    SDL_Color color = {red, green, blue, 255};
    return color;
}

static void draw_filled_round_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
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
    int text_width = 0;
    int text_height = 0;
    if (TTF_SizeUTF8(font, text, &text_width, &text_height) != 0) {
        return;
    }

    int x = bounds.x + (bounds.w - text_width) / 2;
    int y = bounds.y + (bounds.h - text_height) / 2;
    draw_text(renderer, font, text, x, y, color);
}

CameraPrompt* camera_prompt_init(void) {
    CameraPrompt *prompt = (CameraPrompt *)malloc(sizeof(CameraPrompt));
    if (!prompt) {
        return NULL;
    }

    prompt->window = SDL_CreateWindow(
        "Hand Tennis - Camera Access",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        720,
        420,
        SDL_WINDOW_SHOWN
    );
    if (!prompt->window) {
        free(prompt);
        return NULL;
    }

    prompt->renderer = SDL_CreateRenderer(prompt->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!prompt->renderer) {
        SDL_DestroyWindow(prompt->window);
        free(prompt);
        return NULL;
    }

    prompt->title_font = TTF_OpenFont(PROMPT_FONT_PATH, 30);
    prompt->body_font = TTF_OpenFont(PROMPT_FONT_PATH, 18);
    prompt->button_font = TTF_OpenFont(PROMPT_FONT_PATH, 20);
    if (!prompt->title_font || !prompt->body_font || !prompt->button_font) {
        if (prompt->title_font) TTF_CloseFont(prompt->title_font);
        if (prompt->body_font) TTF_CloseFont(prompt->body_font);
        if (prompt->button_font) TTF_CloseFont(prompt->button_font);
        SDL_DestroyRenderer(prompt->renderer);
        SDL_DestroyWindow(prompt->window);
        free(prompt);
        return NULL;
    }

    SDL_SetRenderDrawBlendMode(prompt->renderer, SDL_BLENDMODE_BLEND);
    prompt->retry_clicked = false;
    prompt->quit_clicked = false;
    return prompt;
}

void camera_prompt_cleanup(CameraPrompt *prompt) {
    if (!prompt) return;

    if (prompt->title_font) TTF_CloseFont(prompt->title_font);
    if (prompt->body_font) TTF_CloseFont(prompt->body_font);
    if (prompt->button_font) TTF_CloseFont(prompt->button_font);
    if (prompt->renderer) SDL_DestroyRenderer(prompt->renderer);
    if (prompt->window) SDL_DestroyWindow(prompt->window);
    free(prompt);
}

void camera_prompt_draw(CameraPrompt *prompt) {
    SDL_SetRenderDrawColor(prompt->renderer, 13, 18, 30, 255);
    SDL_RenderClear(prompt->renderer);

    SDL_Rect glow = {48, 36, 624, 286};
    draw_filled_round_rect(prompt->renderer, glow, rgb(24, 30, 48));

    SDL_Rect top_bar = {48, 36, 624, 60};
    draw_filled_round_rect(prompt->renderer, top_bar, rgb(92, 149, 230));

    SDL_Rect content = {48, 96, 624, 226};
    draw_filled_round_rect(prompt->renderer, content, rgb(34, 43, 70));

    SDL_Rect badge = {76, 136, 72, 72};
    draw_filled_round_rect(prompt->renderer, badge, rgb(255, 208, 92));

    SDL_SetRenderDrawColor(prompt->renderer, 255, 235, 170, 255);
    SDL_Rect lens = {98, 158, 28, 28};
    SDL_RenderFillRect(prompt->renderer, &lens);
    SDL_Rect lens2 = {126, 170, 18, 6};
    SDL_RenderFillRect(prompt->renderer, &lens2);

    SDL_Color text_primary = rgb(245, 247, 255);
    SDL_Color text_secondary = rgb(185, 195, 220);
    SDL_Color text_muted = rgb(140, 154, 185);
    SDL_Color button_text = rgb(255, 255, 255);

    draw_text(prompt->renderer, prompt->title_font, "Camera Access", 82, 47, rgb(16, 24, 40));
    draw_text(prompt->renderer, prompt->title_font, "Allow webcam access to continue", 170, 130, text_primary);
    draw_text(prompt->renderer, prompt->body_font, "Windows may ask for permission to use your camera.", 170, 170, text_secondary);
    draw_text(prompt->renderer, prompt->body_font, "Choose Allow, then press Retry to reopen the webcam.", 170, 198, text_secondary);
    draw_text(prompt->renderer, prompt->body_font, "Tip: also check Settings > Privacy & security > Camera.", 170, 226, text_muted);

    SDL_Rect retryButton = {160, 316, 176, 54};
    SDL_Rect quitButton = {384, 316, 176, 54};
    draw_filled_round_rect(prompt->renderer, retryButton, rgb(72, 197, 127));
    draw_filled_round_rect(prompt->renderer, quitButton, rgb(229, 94, 94));

    SDL_Rect retryGlow = {160, 316, 176, 54};
    SDL_Rect quitGlow = {384, 316, 176, 54};
    SDL_SetRenderDrawColor(prompt->renderer, 255, 255, 255, 30);
    SDL_RenderDrawRect(prompt->renderer, &retryGlow);
    SDL_RenderDrawRect(prompt->renderer, &quitGlow);

    SDL_Rect retryTextBox = {160, 316, 176, 54};
    SDL_Rect quitTextBox = {384, 316, 176, 54};
    draw_text_centered(prompt->renderer, prompt->button_font, "Retry", retryTextBox, button_text);
    draw_text_centered(prompt->renderer, prompt->button_font, "Quit", quitTextBox, button_text);

    draw_text(prompt->renderer, prompt->body_font, "Press R or click Retry", 256, 382, text_muted);
    draw_text(prompt->renderer, prompt->body_font, "Press Esc or click Quit", 456, 382, text_muted);

    SDL_RenderPresent(prompt->renderer);
}

bool camera_prompt_run(CameraPrompt *prompt) {
    while (!prompt->retry_clicked && !prompt->quit_clicked) {
        camera_prompt_draw(prompt);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                prompt->quit_clicked = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    prompt->quit_clicked = true;
                } else if (event.key.keysym.sym == SDLK_r) {
                    prompt->retry_clicked = true;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int mouse_x = event.button.x;
                int mouse_y = event.button.y;

                // Retry button: drawn at {160, 316, 176, 54} → x 160-336, y 316-370
                if (mouse_x >= 160 && mouse_x <= 336 && mouse_y >= 316 && mouse_y <= 370) {
                    prompt->retry_clicked = true;
                // Quit button: drawn at {384, 316, 176, 54} → x 384-560, y 316-370
                } else if (mouse_x >= 384 && mouse_x <= 560 && mouse_y >= 316 && mouse_y <= 370) {
                    prompt->quit_clicked = true;
                }
            }
        }

        SDL_Delay(16);
    }

    return prompt->retry_clicked && !prompt->quit_clicked;
}

Difficulty parse_difficulty(const char *arg) {
    int level = atoi(arg);
    if (level < 0 || level > 3) {
        fprintf(stderr, "Invalid difficulty level: %d. Using Medium (1).\n", level);
        return DIFFICULTY_MEDIUM;
    }
    return (Difficulty)level;
}

static int parse_frame_cap(const char *arg) {
    int cap = atoi(arg);
    if (cap != 30 && cap != 60) {
        fprintf(stderr, "Invalid FPS cap: %d. Using 60 FPS.\n", cap);
        return 60;
    }
    return cap;
}

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "SDL_ttf initialization failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }
    
    print_usage();
    
    // Parse difficulty from command line
    Difficulty difficulty = DIFFICULTY_MEDIUM;
    int frame_cap = 60;
    if (argc > 1) {
        difficulty = parse_difficulty(argv[1]);
    }
    if (argc > 2) {
        frame_cap = parse_frame_cap(argv[2]);
    }
    
    printf("Starting Hand Tennis - Difficulty: %s\n\n", 
           difficulty == DIFFICULTY_EASY ? "Easy" :
           difficulty == DIFFICULTY_MEDIUM ? "Medium" :
           difficulty == DIFFICULTY_HARD ? "Hard" : "Extreme");
    printf("Frame cap: %d FPS\n\n", frame_cap);

    printf("[1/4] Initializing graphics...\n");
    Renderer *renderer = rendering_init();
    if (!renderer) {
        fprintf(stderr, "Failed to initialize renderer\n");
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    printf("      ✓ Graphics ready\n");

    CameraPrompt *camera_prompt = camera_prompt_init();
    if (!camera_prompt) {
        fprintf(stderr, "Failed to open camera prompt window\n");
        rendering_cleanup(renderer);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    printf("[2/4] Camera access window open.\n");
    printf("      If Windows asks for camera permission, allow it.\n");
    printf("      Press R or click Retry after enabling camera access.\n");

    bool camera_ready = false;
    HandTracker *hand_tracker = NULL;
    while (!camera_ready) {
        if (!camera_prompt_run(camera_prompt)) {
            camera_prompt_cleanup(camera_prompt);
            rendering_cleanup(renderer);
            TTF_Quit();
            SDL_Quit();
            return 1;
        }

        printf("[3/4] Initializing hand tracker...\n");
        hand_tracker = hand_tracker_init(rendering_get_sdl_renderer(renderer));
        if (hand_tracker) {
            camera_ready = true;
            printf("      ✓ Hand tracker ready\n");
        } else {
            printf("      Camera still unavailable. Check Windows privacy settings and click Retry.\n");
            camera_prompt->retry_clicked = false;
        }
    }

    camera_prompt_cleanup(camera_prompt);

    printf("[4/4] Initializing game state...\n");
    GameState *game_state = game_init(difficulty);
    if (!game_state) {
        fprintf(stderr, "Failed to initialize game\n");
        hand_tracker_cleanup(hand_tracker);
        rendering_cleanup(renderer);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    printf("      ✓ Game state ready\n");
    
    printf("Starting game loop...\n");
    printf("      ✓ Ready to play!\n\n");
    
    // Main game loop
    bool running = true;
    uint32_t last_time = SDL_GetTicks();
    uint32_t frame_time = 0;
    uint32_t frame_count_fps = 0;
    uint32_t fps_timer = 0;
    uint32_t target_frame_ms = (frame_cap == 30) ? 33u : 16u;
    
    while (running) {
        uint32_t current_time = SDL_GetTicks();
        frame_time = current_time - last_time;
        last_time = current_time;
        
        // Cap at either 30 FPS or 60 FPS.
        if (frame_time < target_frame_ms) {
            SDL_Delay(target_frame_ms - frame_time);
        }
        
        // Update FPS counter
        frame_count_fps++;
        fps_timer += frame_time;
        if (fps_timer >= 1000) {
            game_state->fps = frame_count_fps;
            frame_count_fps = 0;
            fps_timer = 0;
            printf("FPS: %.1f | Player: %d | Opponent: %d | Difficulty: %s\n",
                   game_state->fps,
                   game_state->player_score,
                   game_state->opponent_score,
                   difficulty == DIFFICULTY_EASY ? "Easy" :
                   difficulty == DIFFICULTY_MEDIUM ? "Medium" :
                   difficulty == DIFFICULTY_HARD ? "Hard" : "Extreme");
        }
        
        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_SPACE && game_state->game_over) {
                        // Restart game
                        game_cleanup(game_state);
                        game_state = game_init(difficulty);
                    }
                    break;
            }
        }
        
        // Update hand tracking
        Hand hand_input = {0};
        hand_input.detected = hand_tracker_detect(hand_tracker, &hand_input.x, &hand_input.y);
        
        // Update game
        game_update(game_state, &hand_input);
        
        // Render
        rendering_draw_game(renderer, game_state,
                           difficulty == DIFFICULTY_EASY ? "Easy" :
                           difficulty == DIFFICULTY_MEDIUM ? "Medium" :
                           difficulty == DIFFICULTY_HARD ? "Hard" : "Extreme",
                           hand_tracker);
    }
    
    printf("\n[Cleanup] Shutting down...\n");
    game_cleanup(game_state);
    rendering_cleanup(renderer);
    hand_tracker_cleanup(hand_tracker);
    TTF_Quit();
    SDL_Quit();
    printf("✓ Goodbye!\n");
    
    return 0;
}
