#ifndef DSM1SAR_H
#define DSM1SAR_H

#include <arpa/inet.h>
#include <stddef.h>

// API Publique (seul fichier inclus par l'utilisateur)

/* --- Memoire --- */
typedef struct {
    unsigned long nb_pages; // Nombre de pages
    unsigned int page_size; // Taille d'une page en octets
    //+
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
int dsm_init(const dsm_config *config);

#endif // DSM1SAR_H