#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dsm.h"
#include "util/test_commun.h"
#include "util/test_functions.h"

/**
 * Variables globales pour le suivi des tests
 */
int test_ok = 0;
int node_id_vir;
int test = 1;
unsigned long data_msg_count = 0;
unsigned long inva_msg_count = 0;
unsigned long count_uffd = 0;

/**
 * Constantes
 */
#define LOCAL_PAGE_SIZE 4096
#define DEFAULT_NB_PAGES 65536

/**
 * Options de test
 */
typedef struct {
    int run_dsm;
    int run_local;
    int run_locks;
} test_options;

/**
 * Structures pour les résultats de test
 */
typedef struct {
    long time_dsm;
    long time_local;
    long time_dsm_locks;
} matrix_test_result;

/**
 * Affiche l'aide sur les options disponibles
 */
static void print_usage(const char *program_name) {
    printf("\nUsage: %s [OPTIONS]\n\n", program_name);
    printf("Options de test:\n");
    printf("  --dsm          Exécuter les tests DSM uniquement\n");
    printf("  --local        Exécuter les tests LOCAL uniquement\n");
    printf("  --locks        Exécuter les tests DSM WITH LOCKS uniquement\n");
    printf("  --dsm-local    Exécuter DSM et LOCAL (défaut)\n");
    printf("  --dsm-locks    Exécuter DSM et DSM WITH LOCKS\n");
    printf("  --local-locks  Exécuter LOCAL et DSM WITH LOCKS\n");
    printf("\nExemples:\n");
    printf("  %s --dsm\n", program_name);
    printf("  %s --dsm-local\n", program_name);
    printf("\n");
}

/**
 * Parse les options de test depuis les arguments de ligne de commande
 */
static test_options parse_test_options(int argc, char const *argv[]) {
    test_options opts = {0, 0, 0};
    int default_opts = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[i], "--dsm") == 0) {
            opts.run_dsm = 1;
            default_opts = 0;
        } else if (strcmp(argv[i], "--local") == 0) {
            opts.run_local = 1;
            default_opts = 0;
        } else if (strcmp(argv[i], "--locks") == 0) {
            opts.run_locks = 1;
            default_opts = 0;
        } else if (strcmp(argv[i], "--dsm-local") == 0) {
            opts.run_dsm = 1;
            opts.run_local = 1;
            default_opts = 0;
        } else if (strcmp(argv[i], "--dsm-locks") == 0) {
            opts.run_dsm = 1;
            opts.run_locks = 1;
            default_opts = 0;
        } else if (strcmp(argv[i], "--local-locks") == 0) {
            opts.run_local = 1;
            opts.run_locks = 1;
            default_opts = 0;
        }
    }

    // Options par défaut: DSM et LOCAL
    if (default_opts) {
        opts.run_dsm = 1;
        opts.run_local = 1;
    }

    return opts;
}

/**
 * Affiche les options de test actives
 */
static void print_active_test_options(test_options opts) {
    printf("\nTests actifs: ");
    if (opts.run_dsm)
        printf("[DSM] ");
    if (opts.run_local)
        printf("[LOCAL] ");
    if (opts.run_locks)
        printf("[LOCKS] ");
    printf("\n\n");
}

/**
 * Fonctions utilitaires
 */

/**
 * Calcule le nombre de pages nécessaires pour une matrice
 */
static int calculate_pages_needed(int matrix_size, int page_size) {
    int total_bytes = matrix_size * matrix_size * sizeof(int);
    return (total_bytes + page_size - 1) / page_size;
}

/**
 * Calcule les lignes de travail pour un nœud spécifique
 */
static void calculate_work_rows(int matrix_size, int nb_compute_nodes, int worker_idx,
                                int *start_row, int *end_row) {
    int rows_per_node = matrix_size / nb_compute_nodes;
    *start_row = worker_idx * rows_per_node;
    *end_row =
        (worker_idx == nb_compute_nodes - 1) ? matrix_size : *start_row + rows_per_node;
}

/**
 * Valide l'allocation des matrices
 */
static int validate_matrix_allocation(int **matrix_A, int **matrix_B, int **matrix_C) {
    if (!matrix_A || !matrix_B || !matrix_C) {
        LOG_ERROR("Erreur d'allocation des tableaux de pointeurs sur le tas");
        return -1;
    }
    return 0;
}

/**
 * Libère la mémoire allouée pour les matrices
 */
static void free_matrices(int **matrix_A, int **matrix_B, int **matrix_C, int nb_pages) {
    for (int i = 0; i < nb_pages; i++) {
        free(matrix_A[i]);
        free(matrix_B[i]);
        free(matrix_C[i]);
    }
    free(matrix_A);
    free(matrix_B);
    free(matrix_C);
}

/**
 * Calcule le temps écoulé en nanosecondes
 */
static long calculate_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
}

/**
 * Affiche le rapport de comparaison entre deux temps
 */
static void print_comparison_ratio(const char *label, long time1, long time2) {
    if (time2 == 0)
        return;
    int ratio = (int)((time1 - time2) / time2) * 100;
    LOG_SUBSECTION(BOLD BRIGHT_BLUE "%s - %d%%" RESET, label, ratio);
}

/**
 * Fonctions de test (inchangées)
 */

long test_matrix_multiplication_local(int nb_compute_nodes, int matrix_size,
                                      int node_id) {
    int ints_per_page = LOCAL_PAGE_SIZE / sizeof(int);
    int nb_pages_needed = calculate_pages_needed(matrix_size, LOCAL_PAGE_SIZE);

    LOG_SUBSECTION(
        BOLD
        "TEST %d : Speedup - Produit Matriciel %dx%d LOCAL (Noeud virtuel - %d)" RESET,
        test, matrix_size, matrix_size, node_id_vir);

    int **matrix_A = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_B = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_C = malloc(nb_pages_needed * sizeof(int *));

    // Allocation des pages
    for (int i = 0; i < nb_pages_needed; i++) {
        matrix_A[i] = malloc(LOCAL_PAGE_SIZE);
        matrix_B[i] = malloc(LOCAL_PAGE_SIZE);
        matrix_C[i] = malloc(LOCAL_PAGE_SIZE);
    }

    // Initialisation
    for (int i = 0; i < matrix_size * matrix_size; i++) {
        int page_idx = i / ints_per_page;
        int local_idx = i % ints_per_page;
        matrix_A[page_idx][local_idx] = i % 100;
        matrix_B[page_idx][local_idx] = (i * 2) % 100;
        matrix_C[page_idx][local_idx] = 0;
    }

    // Calcul distribué
    int worker_idx = node_id_vir - 1; // 0-basé
    int start_row, end_row;
    calculate_work_rows(matrix_size, nb_compute_nodes, worker_idx, &start_row, &end_row);

    long time_taken = 0;
    if (start_row >= matrix_size) {
        LOG_NODE(node_id, "Pas de travail");
        free_matrices(matrix_A, matrix_B, matrix_C, nb_pages_needed);
        return 0;
    }
    LOG_NODE(node_id, "Lignes %d à %d", start_row, end_row - 1);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int sum = 0;
            for (int k = 0; k < matrix_size; k++) {
                int global_idx_A = i * matrix_size + k;
                int global_idx_B = k * matrix_size + j;
                sum +=
                    matrix_A[global_idx_A / ints_per_page][global_idx_A % ints_per_page] *
                    matrix_B[global_idx_B / ints_per_page][global_idx_B % ints_per_page];
            }
            int global_idx_C = i * matrix_size + j;
            matrix_C[global_idx_C / ints_per_page][global_idx_C % ints_per_page] = sum;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    time_taken = calculate_elapsed_time(start, end);
    LOG_NODE(node_id, "Temps: %.2f ms", time_taken / 1e6);
    if (node_id_vir == 1)
        LOG_SUCCESS("Calcul terminé. C[0] = %d", matrix_C[0][0]);

    free_matrices(matrix_A, matrix_B, matrix_C, nb_pages_needed);
    return time_taken;
}

long test_matrix_multiplication(int nb_compute_nodes, int node_id, int matrix_size,
                                dsm_config config) {
    LOG_SECTION(
        "--- TEST %d : Speedup - Produit Matriciel %dx%d DSM (Noeud virtuel - %d) ---",
        test, matrix_size, matrix_size, node_id_vir);
    int ints_per_page = config.pager.page_size / sizeof(int);
    int nb_pages_needed = calculate_pages_needed(matrix_size, config.pager.page_size);
    int matrix_elements = matrix_size * matrix_size;

    struct timespec start, end;

    int **matrix_A = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_B = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_C = malloc(nb_pages_needed * sizeof(int *));

    if (validate_matrix_allocation(matrix_A, matrix_B, matrix_C) < 0) {
        return -1;
    }

    for (int i = 0; i < nb_pages_needed; i++) {
        if (!(matrix_A[i] = (int *)dsm_alloc()) || !(matrix_B[i] = (int *)dsm_alloc()) ||
            !(matrix_C[i] = (int *)dsm_alloc())) {
            LOG_ERROR("Échec d'allocation DSM à l'index %d", i);
            free(matrix_A);
            free(matrix_B);
            free(matrix_C);
            return -1;
        }
    }

    if (node_id == 1) {
        LOG_NODE(1, "Initialisation des matrices A et B...");
        for (int i = 0; i < matrix_elements; i++) {
            int page_idx = i / ints_per_page;
            int local_idx = i % ints_per_page;
            matrix_A[page_idx][local_idx] = i % 100;
            matrix_B[page_idx][local_idx] = (i * 2) % 100;
            matrix_C[page_idx][local_idx] = 0;
        }
    }
    dsm_barrier();

    int worker_idx = node_id_vir - 1;
    int start_row, end_row;
    calculate_work_rows(matrix_size, nb_compute_nodes, worker_idx, &start_row, &end_row);

    if (start_row >= matrix_size) {
        LOG_NODE(node_id, "Pas de travail (index nœud >= nb_compute_nodes: %d)",
                 nb_compute_nodes);
        free(matrix_A);
        free(matrix_B);
        free(matrix_C);
        return 0;
    }

    LOG_NODE(node_id, "En charge des lignes %d à %d", start_row, end_row - 1);
    dsm_barrier();

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int sum = 0;
            for (int k = 0; k < matrix_size; k++) {
                int global_idx_A = i * matrix_size + k;
                int global_idx_B = k * matrix_size + j;
                sum +=
                    matrix_A[global_idx_A / ints_per_page][global_idx_A % ints_per_page] *
                    matrix_B[global_idx_B / ints_per_page][global_idx_B % ints_per_page];
            }
            int global_idx_C = i * matrix_size + j;
            matrix_C[global_idx_C / ints_per_page][global_idx_C % ints_per_page] = sum;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    dsm_barrier();

    long time_taken = calculate_elapsed_time(start, end);
    LOG_NODE(node_id, "Temps de calcul : %.2f ms", time_taken / 1e6);

    if (node_id_vir == 1)
        LOG_SUCCESS("Calcul terminé. C[0] = %d", matrix_C[0][0]);

    free(matrix_A);
    free(matrix_B);
    free(matrix_C);
    return time_taken;
}

long test_matrix_multiplication_with_locks(int nb_compute_nodes, int node_id,
                                           int matrix_size, dsm_config config) {
    LOG_SUBSECTION("TEST %d : Speedup - Produit Matriciel %dx%d DSM with LOCKS (Noeud "
                   "virtuel - %d)",
                   test, matrix_size, matrix_size, node_id_vir);
    int ints_per_page = config.pager.page_size / sizeof(int);
    int nb_pages_needed = calculate_pages_needed(matrix_size, config.pager.page_size);
    int matrix_elements = matrix_size * matrix_size;

    struct timespec start, end;

    int **matrix_A = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_B = malloc(nb_pages_needed * sizeof(int *));
    int **matrix_C = malloc(nb_pages_needed * sizeof(int *));

    if (validate_matrix_allocation(matrix_A, matrix_B, matrix_C) < 0) {
        return -1;
    }

    for (int i = 0; i < nb_pages_needed; i++) {
        if (!(matrix_A[i] = (int *)dsm_alloc()) || !(matrix_B[i] = (int *)dsm_alloc()) ||
            !(matrix_C[i] = (int *)dsm_alloc())) {
            LOG_ERROR("Échec d'allocation DSM à l'index %d", i);
            free(matrix_A);
            free(matrix_B);
            free(matrix_C);
            return -1;
        }
    }

    if (node_id == 1) {
        LOG_NODE(1, "Initialisation des matrices A et B...");
        for (int i = 0; i < matrix_elements; i++) {
            int page_idx = i / ints_per_page;
            int local_idx = i % ints_per_page;
            matrix_A[page_idx][local_idx] = i % 100;
            matrix_B[page_idx][local_idx] = (i * 2) % 100;
            matrix_C[page_idx][local_idx] = 0;
        }
    }
    dsm_barrier();

    int worker_idx = node_id_vir - 1;
    int start_row, end_row;
    calculate_work_rows(matrix_size, nb_compute_nodes, worker_idx, &start_row, &end_row);

    if (start_row >= matrix_size) {
        LOG_NODE(node_id, "Pas de travail (index nœud >= nb_compute_nodes: %d)",
                 nb_compute_nodes);
        free(matrix_A);
        free(matrix_B);
        free(matrix_C);
        return 0;
    }

    LOG_NODE(node_id, "En charge des lignes %d à %d", start_row, end_row - 1);
    dsm_barrier();

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int sum = 0;
            for (int k = 0; k < matrix_size; k++) {
                int global_idx_A = i * matrix_size + k;
                int global_idx_B = k * matrix_size + j;
                dsm_lock(&matrix_A[global_idx_A / ints_per_page]
                                  [global_idx_A % ints_per_page]);
                dsm_lock(&matrix_B[global_idx_B / ints_per_page]
                                  [global_idx_B % ints_per_page]);
                sum +=
                    matrix_A[global_idx_A / ints_per_page][global_idx_A % ints_per_page] *
                    matrix_B[global_idx_B / ints_per_page][global_idx_B % ints_per_page];
                dsm_unlock(&matrix_A[global_idx_A / ints_per_page]
                                    [global_idx_A % ints_per_page]);
                dsm_unlock(&matrix_B[global_idx_B / ints_per_page]
                                    [global_idx_B % ints_per_page]);
            }
            int global_idx_C = i * matrix_size + j;
            dsm_lock(
                &matrix_C[global_idx_C / ints_per_page][global_idx_C % ints_per_page]);
            matrix_C[global_idx_C / ints_per_page][global_idx_C % ints_per_page] = sum;
            dsm_unlock(
                &matrix_C[global_idx_C / ints_per_page][global_idx_C % ints_per_page]);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    dsm_barrier();

    long time_taken = calculate_elapsed_time(start, end);
    LOG_NODE(node_id, "Temps de calcul : %.2f ms", time_taken / 1e6);

    if (node_id == 1)
        LOG_SUCCESS("Calcul terminé. C[0] = %d", matrix_C[0][0]);

    free(matrix_A);
    free(matrix_B);
    free(matrix_C);
    return time_taken;
}

/**
 * Exécute un cycle de tests complet pour une taille de matrice donnée
 */
static int run_matrix_test_cycle(int nb_compute_nodes, int node_id, int matrix_size,
                                 dsm_config config, matrix_test_result *result,
                                 test_options opts) {
    result->time_dsm = 0;
    result->time_local = 0;
    result->time_dsm_locks = 0;

    if (opts.run_dsm) {
        if ((result->time_dsm = test_matrix_multiplication(nb_compute_nodes, node_id,
                                                           matrix_size, config)) < 0)
            return -1;
        test_ok++;
        dsm_barrier();
    }

    if (opts.run_local) {
        if ((result->time_local = test_matrix_multiplication_local(
                 nb_compute_nodes, matrix_size, node_id)) < 0)
            return -1;
        dsm_barrier();
    }

    if (opts.run_locks) {
        if ((result->time_dsm_locks = test_matrix_multiplication_with_locks(
                 nb_compute_nodes, node_id, matrix_size, config)) < 0)
            return -1;
        dsm_barrier();
    }

    test++;
    return 0;
}

/**
 * Affiche les rapports de performance pour un test
 */
static void print_test_reports(int matrix_size, matrix_test_result result,
                               test_options opts) {
    if (opts.run_dsm && opts.run_local && result.time_local > 0) {
        char label[128];
        snprintf(label, sizeof(label),
                 "Ratio de comparaison Local vs DSM - Matrice %dx%d", matrix_size,
                 matrix_size);
        print_comparison_ratio(label, result.time_dsm, result.time_local);
    }

    if (opts.run_dsm && opts.run_locks && result.time_dsm_locks > 0) {
        char label[128];
        snprintf(label, sizeof(label),
                 "Ratio de comparaison DSM vs DSM WITH LOCKS - Matrice %dx%d",
                 matrix_size, matrix_size);
        print_comparison_ratio(label, result.time_dsm_locks, result.time_dsm);
    }

    if (opts.run_local && opts.run_locks && result.time_dsm_locks > 0) {
        char label[128];
        snprintf(label, sizeof(label),
                 "Ratio de comparaison LOCAL vs DSM WITH LOCKS - Matrice %dx%d",
                 matrix_size, matrix_size);
        print_comparison_ratio(label, result.time_dsm_locks, result.time_local);
    }
}

/**
 * Fonction principale
 */
int main(int argc, char const *argv[]) {
    // Parse les options de test avant config_choice pour ne pas perturber les arguments
    test_options test_opts = parse_test_options(argc, argv);

    dsm_config config;
    dsm_get_default_config(&config);
    config_choice(argc, argv, &config, &node_id_vir);

    unsigned int node_id;
    config.pager.nb_pages = DEFAULT_NB_PAGES;
    int ret = dsm_init(&config, &node_id);
    if (ret < 0) {
        LOG_ERROR("dsm_init a échoué");
        return EXIT_FAILURE;
    }
    if (node_id_vir == -1)
        node_id_vir = node_id;

    int nb_compute_nodes = config.net.system.nb_nodes - config.net.system.nb_managers;
    LOG_TITLE(">>> DSM TEST CONDITION REELLES (Noeud %d) <<<", node_id);
    LOG_SECTION("Configuration - Managers : %d - Workers : %d - Total noeuds : %d - ",
                config.net.system.nb_managers, nb_compute_nodes,
                config.net.system.nb_nodes);

    if (node_id == 1)
        print_active_test_options(test_opts);

    if (ret == DSM_ROLE_MANAGER)
        return EXIT_SUCCESS;

    /**
     * Tailles de matrices à tester
     */
    int matrix_sizes[] = {8, 16, 32, 64, 128, 256, 512};
    int num_sizes = sizeof(matrix_sizes) / sizeof(matrix_sizes[0]);

    /**
     * Boucle de tests pour chaque taille de matrice
     */
    for (int idx = 0; idx < num_sizes; idx++) {
        int matrix_size = matrix_sizes[idx];
        matrix_test_result result;

        if (run_matrix_test_cycle(nb_compute_nodes, node_id, matrix_size, config, &result,
                                  test_opts) < 0)
            goto cleanup;

        print_test_reports(matrix_size, result, test_opts);
        dsm_barrier();
    }

    LOG_TITLE("Tests réussis : %d/%d", test_ok, test);
    dsm_finalize();
    return EXIT_SUCCESS;

cleanup:
    LOG_TITLE("--- Bilan (Nœud %d) ---", node_id);
    LOG_TITLE("Tests réussis : %d/%d", test_ok, test);
    dsm_finalize();
    return EXIT_FAILURE;
}