#ifndef DSM1SAR_H
#define DSM1SAR_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>

// API Publique (seul fichier inclus par l'utilisateur)

/* --- Memoire --- */
typedef struct {
    unsigned int nb_pages;  // Nombre de pages
    unsigned int page_size; // Taille d'une page en octets
} dsm_pager_config;

/* --- Réseau --- */
typedef struct {
    unsigned short system_id;
    unsigned int nb_managers;
    unsigned int nb_nodes;
    char multicast_ip[INET_ADDRSTRLEN];
    unsigned short multicast_port;
} dsm_system_config;

typedef enum {
    DSM_ROLE_UNDEFINED,
    DSM_ROLE_WORKER,
    DSM_ROLE_MANAGER
} dsm_node_role;

/* Les ports concernent uniquement les ports de réception*/
typedef struct {
    dsm_node_role role;
    unsigned short tcp_port;
    unsigned short udp_port;
    unsigned long pool_nb_threads; // Pour les pools de threads, local au noeud
    unsigned long pool_queue_size; // Pour les pools de threads, local au noeud
} dsm_node_config;

typedef struct {
    dsm_system_config system;
    dsm_node_config node;
} dsm_net_config;

/* --- Configuration globale --- */
typedef struct {
    dsm_pager_config pager;
    dsm_net_config net;
} dsm_config;

/* --- Erreurs --- */
typedef enum {
    DSM_SUCCESS = 0, // Eviter de retourner 0 pour les erreurs, réservé pour succès
    /* Init & Context */
    DSM_ERROR_INIT_ARG_NB_PAGES,
    DSM_ERROR_INIT_ARG_PAGE_SIZE,
    DSM_ERROR_INIT_ARG_NB_NODES,
    DSM_ERROR_INIT_ARG_NB_MANAGERS,
    DSM_ERROR_ALREADY_INIT,
    DSM_ERROR_CONTEXT_ALLOC,

    /* Discover */
    DSM_ERROR_SYSTEM_FULL

    /* ... */
} dsm_exit_code;

/* --- Fonctions --- */
void dsm_get_default_config(dsm_config *config);

/**
 * @return Le rôle du nœud (manager ou worker)
 * @warning DSM_ROLE_UNDEFINED ne peut être jamais retourné par dsm_init, il est retourné
 * en tant que -1
 */
dsm_node_role dsm_init(const dsm_config *config);

/**
 * @brief Alloue un segment de mémoire dans l'espace d'adressage global partagé.
 *
 * @warning Cette fonction doit être appelée de manière déterministe avant toute
 * divergence du flux d'exécution (fork, création de threads ou logique spécifique aux
 * nœuds). Pour garantir la cohérence de l'espace global, chaque nœud doit effectuer les
 * mêmes appels d'allocation dans le même ordre.
 *
 * @param len Taille de l'allocation en octets.
 * @return void* Un pointeur vers la zone allouée, ou NULL en cas d'erreur.
 */
void *dsm_alloc(void);

/**
 * @brief Envoie une requete de LOCK sur la page qui contient addr
 *
 * @param addr
 * @return int : 0 en cas de succès, -1 en cas d'erreur
 */
int dsm_lock(void *addr);

/**
 * @brief Envoie une requete de UNLOCK sur la page qui contient addr
 *
 * @param addr
 * @return int : 0 en cas de succès, -1 en cas d'erreur
 */
int dsm_unlock(void *addr);
int dsm_barrier(void);
int dsm_barrier_advanced(unsigned int threads_count, unsigned int manager_id);
int dsm_finalize(void);
int dsm_finalize_advanced(unsigned int threads_count);
int dsm_get_node_id();

bool dsm_get_uffd_count(unsigned long *out);
bool dsm_get_data_count(unsigned long *out);
bool dsm_get_inva_count(unsigned long *out);

bool dsm_reset_uffd_count();
bool dsm_reset_data_count();
bool dsm_reset_inva_count();

#endif // DSM1SAR_H