#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <SDL2/SDL.h>
#include "game.h"
#include "hand_tracker.h"

// Forward declarations
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} Renderer;

Renderer* rendering_init(void);
void rendering_draw_game(Renderer *r, GameState *state, const char *difficulty_name);
void rendering_cleanup(Renderer *r);

const char* ai_get_difficulty_name(Difficulty difficulty);

void print_usage(void) {
    printf("\n=== Hand Tennis Game ===\n");
    printf("Usage: hand_tennis [difficulty]\n\n");
    printf("Difficulty levels:\n");
    printf("  0 = Easy (slow opponent, forgiving)\n");
    printf("  1 = Medium (balanced gameplay)\n");
    printf("  2 = Hard (fast, predictive)\n");
    printf("  3 = Extreme (godlike AI)\n\n");
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
    bool retry_clicked;
    bool quit_clicked;
} CameraPrompt;

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

    SDL_SetRenderDrawBlendMode(prompt->renderer, SDL_BLENDMODE_BLEND);
    prompt->retry_clicked = false;
    prompt->quit_clicked = false;
    return prompt;
}

void camera_prompt_cleanup(CameraPrompt *prompt) {
    if (!prompt) return;

    if (prompt->renderer) SDL_DestroyRenderer(prompt->renderer);
    if (prompt->window) SDL_DestroyWindow(prompt->window);
    free(prompt);
}

void camera_prompt_draw(CameraPrompt *prompt) {
    SDL_SetRenderDrawColor(prompt->renderer, 18, 22, 35, 255);
    SDL_RenderClear(prompt->renderer);

    SDL_SetRenderDrawColor(prompt->renderer, 40, 55, 90, 255);
    SDL_Rect panel = {40, 40, 640, 220};
    SDL_RenderFillRect(prompt->renderer, &panel);

    SDL_SetRenderDrawColor(prompt->renderer, 120, 180, 255, 255);
    SDL_Rect title = {40, 40, 640, 44};
    SDL_RenderFillRect(prompt->renderer, &title);

    SDL_SetRenderDrawColor(prompt->renderer, 255, 220, 120, 255);
    SDL_Rect camIcon = {78, 112, 72, 48};
    SDL_RenderFillRect(prompt->renderer, &camIcon);

    SDL_SetRenderDrawColor(prompt->renderer, 235, 235, 235, 255);
    SDL_Rect body = {170, 112, 430, 18};
    SDL_RenderFillRect(prompt->renderer, &body);
    SDL_Rect line2 = {170, 142, 350, 18};
    SDL_RenderFillRect(prompt->renderer, &line2);
    SDL_Rect line3 = {170, 172, 400, 18};
    SDL_RenderFillRect(prompt->renderer, &line3);

    SDL_SetRenderDrawColor(prompt->renderer, 70, 200, 120, 255);
    SDL_Rect retryButton = {160, 300, 160, 54};
    SDL_RenderFillRect(prompt->renderer, &retryButton);

    SDL_SetRenderDrawColor(prompt->renderer, 220, 80, 80, 255);
    SDL_Rect quitButton = {400, 300, 160, 54};
    SDL_RenderFillRect(prompt->renderer, &quitButton);

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

                if (mouse_x >= 160 && mouse_x <= 320 && mouse_y >= 300 && mouse_y <= 354) {
                    prompt->retry_clicked = true;
                } else if (mouse_x >= 400 && mouse_x <= 560 && mouse_y >= 300 && mouse_y <= 354) {
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

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    
    print_usage();
    
    // Parse difficulty from command line
    Difficulty difficulty = DIFFICULTY_MEDIUM;
    if (argc > 1) {
        difficulty = parse_difficulty(argv[1]);
    }
    
    printf("Starting Hand Tennis - Difficulty: %s\n\n", 
           difficulty == DIFFICULTY_EASY ? "Easy" :
           difficulty == DIFFICULTY_MEDIUM ? "Medium" :
           difficulty == DIFFICULTY_HARD ? "Hard" : "Extreme");

    CameraPrompt *camera_prompt = camera_prompt_init();
    if (!camera_prompt) {
        fprintf(stderr, "Failed to open camera prompt window\n");
        SDL_Quit();
        return 1;
    }

    printf("[1/4] Camera access window open.\n");
    printf("      If Windows asks for camera permission, allow it.\n");
    printf("      Press R or click Retry after enabling camera access.\n");

    bool camera_ready = false;
    HandTracker *hand_tracker = NULL;
    while (!camera_ready) {
        if (!camera_prompt_run(camera_prompt)) {
            camera_prompt_cleanup(camera_prompt);
            SDL_Quit();
            return 1;
        }

        printf("[2/4] Initializing hand tracker...\n");
        hand_tracker = hand_tracker_init();
        if (hand_tracker) {
            camera_ready = true;
            printf("      ✓ Hand tracker ready\n");
        } else {
            printf("      Camera still unavailable. Check Windows privacy settings and click Retry.\n");
            camera_prompt->retry_clicked = false;
        }
    }

    camera_prompt_cleanup(camera_prompt);
    
    printf("[3/4] Initializing graphics...\n");
    Renderer *renderer = rendering_init();
    if (!renderer) {
        fprintf(stderr, "Failed to initialize renderer\n");
        hand_tracker_cleanup(hand_tracker);
        SDL_Quit();
        return 1;
    }
    printf("      ✓ Graphics ready\n");
    
    printf("[4/4] Initializing game state...\n");
    GameState *game_state = game_init(difficulty);
    if (!game_state) {
        fprintf(stderr, "Failed to initialize game\n");
        rendering_cleanup(renderer);
        hand_tracker_cleanup(hand_tracker);
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
    
    while (running) {
        uint32_t current_time = SDL_GetTicks();
        frame_time = current_time - last_time;
        last_time = current_time;
        
        // Cap at 60 FPS
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
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
                           difficulty == DIFFICULTY_HARD ? "Hard" : "Extreme");
    }
    
    printf("\n[Cleanup] Shutting down...\n");
    game_cleanup(game_state);
    rendering_cleanup(renderer);
    hand_tracker_cleanup(hand_tracker);
    SDL_Quit();
    printf("✓ Goodbye!\n");
    
    return 0;
}
