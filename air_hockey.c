#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>

#define WIDTH 20
#define HEIGHT 10
#define WIN_SCORE 3
#define NAME_LEN 32

// Structure optimized for cache locality and L1/L2 layout (hot fields grouped)
typedef struct {
    int left_p_x;
    int left_p_y;
    int right_p_x;
    int right_p_y;
    int ball_x;
    int ball_y;
    int ball_dx;
    int ball_dy;
    int left_score;
    int right_score;
    char left_name[NAME_LEN];
    char right_name[NAME_LEN];
} GameState;

// Shared Synchronization Context Object
typedef struct {
    GameState *state;
    pthread_mutex_t state_mutex;
    pthread_cond_t render_cond;
    int frame_ready; // Guard predicate for condition variable
} EngineContext;

// Cross-thread shutdown signaling via software-level atomic flag pattern
static volatile sig_atomic_t shutdown_flag = 0;

// Helper function to non-blockingly configure terminal input
void configure_terminal(struct termios *orig_termios) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, orig_termios);
    new_termios = *orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO); // Disable buffering & echoing
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void restore_terminal(struct termios *orig_termios) {
    tcsetattr(STDIN_FILENO, TCSANOW, orig_termios);
}

void reset_ball(GameState *state) {
    state->ball_x = WIDTH / 2;
    state->ball_y = HEIGHT / 2;
    state->ball_dx = (state->ball_dx > 0) ? -1 : 1;
    state->ball_dy = (state->ball_dy > 0) ? -1 : 1;
}

// --- WORKER THREAD 1: ASYNCHRONOUS KEYBOARD EVENT INGESTION ---
void* input_thread_func(void *arg) {
    EngineContext *ctx = (EngineContext*)arg;
    char ch;

    while (!shutdown_flag) {
        // Asynchronous non-blocking char ingestion
        int bytes_read = read(STDIN_FILENO, &ch, 1);
        if (bytes_read > 0) {
            if (ch == 'q' || ch == 'Q') {
                shutdown_flag = 1;
                break;
            }

            // Enforce critical section boundary around game state mutations
            pthread_mutex_lock(&ctx->state_mutex);
            
            // Left paddle processing
            if ((ch == 'w' || ch == 'W') && ctx->state->left_p_y > 1) ctx->state->left_p_y--;
            else if ((ch == 's' || ch == 'S') && ctx->state->left_p_y < HEIGHT - 2) ctx->state->left_p_y++;
            else if ((ch == 'a' || ch == 'A') && ctx->state->left_p_x > 1) ctx->state->left_p_x--;
            else if ((ch == 'd' || ch == 'D') && ctx->state->left_p_x < (WIDTH / 2 - 1)) ctx->state->left_p_x++;

            // Right paddle processing
            if ((ch == 'i' || ch == 'I') && ctx->state->right_p_y > 1) ctx->state->right_p_y--;
            else if ((ch == 'k' || ch == 'K') && ctx->state->right_p_y < HEIGHT - 2) ctx->state->right_p_y++;
            else if ((ch == 'j' || ch == 'J') && ctx->state->right_p_x > (WIDTH / 2 + 1)) ctx->state->right_p_x--;
            else if ((ch == 'l' || ch == 'L') && ctx
