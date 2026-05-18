#ifndef TEST_LOG_H
#define TEST_LOG_H

#include <stdio.h>

typedef struct {
    void *addr;
    int id;
} thread_test_arg_t;


// ═══════════════════════════════════════════════════════════════════════════
// PALETTE DE COULEURS ANSI UNIFIÉE
// ═══════════════════════════════════════════════════════════════════════════
#define RESET "\x1b[0m"
#define BOLD "\x1b[1m"
#define BRIGHT_BLACK "\x1b[90m"

// Couleurs de base
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m"

// Combinaisons
#define BG_TITLE BOLD MAGENTA
#define BG_SECTION BOLD CYAN
#define BG_SUCCESS BOLD GREEN
#define BG_ERROR BOLD RED
#define BG_WARN BOLD YELLOW
#define BG_INFO GREEN
#define BG_DEBUG BLUE
#define BG_THREAD CYAN

// ═══════════════════════════════════════════════════════════════════════════
// MACROS DE LOGGING UNIFIÉES
// ═══════════════════════════════════════════════════════════════════════════
#define LOG_TITLE(fmt, ...)                                                              \
    printf(BG_TITLE "╔════════════════════════════════════════" RESET "\n");             \
    printf(BG_TITLE "║ " fmt RESET BG_TITLE " " RESET "\n", ##__VA_ARGS__);              \
    printf(BG_TITLE "╚════════════════════════════════════════" RESET "\n");

#define LOG_SECTION(fmt, ...)                                                            \
    printf(BG_SECTION "┌─────────────────────────────────────" RESET "\n");              \
    printf(BG_SECTION "│ " fmt RESET BG_SECTION " " RESET "\n", ##__VA_ARGS__);          \
    printf(BG_SECTION "└─────────────────────────────────────" RESET "\n");

#define LOG_INFO(fmt, ...) printf(BG_INFO "[INFO]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_SUCCESS(fmt, ...) printf(BG_SUCCESS "[✓]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) printf(BG_ERROR "[✗]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) printf(BG_WARN "[!]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) printf(BG_DEBUG "[DEBUG]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_THREAD(tid, fmt, ...)                                                        \
    printf(BG_THREAD "[Thread %lu]" RESET " " fmt "\n", tid, ##__VA_ARGS__)


#endif