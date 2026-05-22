#ifndef TEST_LOG_H
#define TEST_LOG_H

#include <stdio.h>



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
#define BRIGHT_CYAN   "\x1b[96m"
#define BRIGHT_GREEN  "\x1b[92m"
#define BRIGHT_BLUE   "\x1b[94m"
#define BRIGHT_YELLOW "\x1b[93m"
#define BRIGHT_WHITE  "\x1b[97m"
#define BRIGHT_CYAN   "\x1b[96m"
#define BRIGHT_MAGENTA "\x1b[95m"
#define DEEP_GREEN    "\x1b[38;5;28m"
#define HIGH_VIS_SUCCESS "\x1b[30;42;1m"

// Combinaisons
#define BG_TITLE BOLD BRIGHT_MAGENTA
#define BG_SECTION BOLD BRIGHT_CYAN
#define BG_SUCCESS BOLD HIGH_VIS_SUCCESS
#define BG_ERROR BOLD RED
#define BG_WARN BOLD YELLOW
#define BG_INFO GREEN
#define BG_DEBUG BLUE
#define BG_THREAD CYAN
#define BG_NODE BOLD BRIGHT_GREEN


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

#define LOG_SUCCESS(fmt, ...) printf(BG_SUCCESS "[SUCCESS]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) printf(BG_ERROR "[ERROR]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) printf(BG_WARN "[!]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) printf(BG_DEBUG "[DEBUG]" RESET " " fmt "\n", ##__VA_ARGS__)

#define LOG_THREAD(tid, fmt, ...)                                                        \
    printf(BG_THREAD "[Thread %lu]" RESET " " fmt "\n", tid, ##__VA_ARGS__)
#define LOG_SUBSECTION(fmt, ...)                                                         \
    printf(BRIGHT_CYAN "  > " RESET BOLD " " fmt RESET "\n",  ##__VA_ARGS__)

#define LOG_NODE(node_id, fmt,...)                                                       \
    printf(BG_NODE "[Noeud %d]" RESET " " fmt "\n", node_id, ##__VA_ARGS__)
#endif