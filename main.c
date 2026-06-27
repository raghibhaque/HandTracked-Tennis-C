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
    
    // Initialize systems
    printf("[1/4] Initializing hand tracker...\n");
    HandTracker *hand_tracker = hand_tracker_init();
    if (!hand_tracker) {
        fprintf(stderr, "Failed to initialize hand tracker\n");
        return 1;
    }
    printf("      ✓ Hand tracker ready\n");
    
    printf("[2/4] Initializing graphics...\n");
    Renderer *renderer = rendering_init();
    if (!renderer) {
        fprintf(stderr, "Failed to initialize renderer\n");
        hand_tracker_cleanup(hand_tracker);
        return 1;
    }
    printf("      ✓ Graphics ready\n");
    
    printf("[3/4] Initializing game state...\n");
    GameState *game_state = game_init(difficulty);
    if (!game_state) {
        fprintf(stderr, "Failed to initialize game\n");
        rendering_cleanup(renderer);
        hand_tracker_cleanup(hand_tracker);
        return 1;
    }
    printf("      ✓ Game state ready\n");
    
    printf("[4/4] Starting game loop...\n");
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
    printf("✓ Goodbye!\n");
    
    return 0;
}
