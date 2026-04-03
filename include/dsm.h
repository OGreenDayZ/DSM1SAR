#ifndef DSM1SAR_H
#define DSM1SAR_H

#include <arpa/inet.h>
#include <stdio.h>

// API Publique (seul fichier inclus par l'utilisateur)

/* --- Memoire --- */
typedef struct {
    size_t nb_pages; // Nombre de pages
    //+
} dsm_pager_config_t;

/* --- Réseau --- */
typedef struct {
    int system_id;
    int nb_managers;
    int nb_nodes;
    char multicast_ip[INET_ADDRSTRLEN];
    int multicast_port;
} dsm_system_config_t;

typedef enum {
    DSM_ROLE_UNDEFINED,
    DSM_ROLE_WORKER,
    DSM_ROLE_MANAGER
} dsm_node_role_t;

typedef struct {
    dsm_node_role_t role;
    int tcp_port;
    int udp_port;
} dsm_node_config_t;

typedef struct {
    dsm_system_config_t system;
    dsm_node_config_t node;
} dsm_net_config_t;

/* --- Configuration globale --- */
typedef struct {
    dsm_pager_config_t pager;
    dsm_net_config_t net;
} dsm_config_t;

/* --- Erreurs --- */
typedef enum {
    /* Init */
    DSM_ERROR_ALREADY_INIT,
    DSM_ERROR_CONTEXT_ALLOC,
    /* Discover */
    DSM_ERROR_SYSTEM_FULL

    /* ... */
} dsm_error_code_t;

/* --- Fonctions --- */
void dsm_get_default_config(dsm_config_t *config);
int dsm_init(const dsm_config_t *config);

#endif // DSM1SAR_H