#ifndef DSM_WORKER_H
#define DSM_WORKER_H

#include <stdatomic.h>
#include <stdint.h>

#include "internal.h"
#include "pager/pager.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"
#include "util/hlist.h"

#define DSM_WKR_NODE_HASH_BITS 4
#define DSM_WKR_PAGE_HASH_BITS 12

typedef struct {

    unsigned int manager_id; // key : id local (connu des workers)
    unsigned int node_id;    // value : id global réel

    dsm_node_content *to_node_content; // raccourci vers la struct de noeud
    struct dsm_hlist_node node;

} dsm_mgr_to_node_content; // Pour managerID_to_nodeID

typedef struct {

    pthread_mutex_t lock;

    unsigned int targ_mgr_id; // Le responsable de la barrière, 0 par défaut

    unsigned int curr_count;
    unsigned int targ_count; // 0 non initialisé, sinon le nombre de threads qui dorment
                             // sur wait_cond

    pthread_cond_t wait_cond;

} sync_barrier; // Pour dsm_mgr_sync

typedef struct {

    pthread_mutex_t lock;

    unsigned int curr_count;
    unsigned int targ_count; // 0 non initialisé, sinon le nombre de threads qui dorment
                             // sur wait_cond

    pthread_cond_t wait_cond;

} sync_finalize; // Pour dsm_mgr_sync

typedef struct {

    sync_barrier barr;
    sync_finalize fina;
} dsm_wkr_sync; // Pour sync


typedef enum {
    NONE, 
    READ_SENT, 
    WRITE_SENT, 
} ReqsState; 
typedef struct {
    unsigned int page_id; // clé
    ReqsState state; 
    pthread_mutex_t mutex; 
    struct dsm_hlist_node node; // pour la map requests_reg
} dsm_requests_reg_entry;       // entrée de requests_reg


typedef struct {
    int locked; // 0 : la page n'est pas encore locked & 1 : la page est locked
    // Le mutex est la car c'est la page qu'on protège
    pthread_mutex_t mutex; // mutex pour protéger l'accès à locked
    pthread_cond_t cond;
    
    dsm_list_head node; // locks_list
} dsm_locks_list_entry; 
typedef struct {
    unsigned int page_id;
    dsm_safe_list locks_list; 
    struct dsm_hlist_node node; // locks_reg 
} dsm_locks_reg_entry;

typedef struct {
    /**
     * Descripteur userfaultfd du worker courant.
     * Initialisé par @ref userfaultfd_init, utilisé dans toute la boucle worker.
     */
    int uffd;
    uint64_t range_start;
    int page_counter;
    // Pas de lock pour page_counter psq on considere que l'user est
    // eduqué et qu'il fera ses allocation avant de crée des threads
    // map des managers : manager_id -> node_id
    dsm_safe_map managerID_to_nodeID;
    dsm_safe_map requests_reg;
    dsm_safe_map locks_reg;
   dsm_wkr_sync sync;

    pthread_t daemon;

} dsm_worker_ctx;

// J'ai besoin d'une

extern dsm_worker_ctx *dsm_wkr_ctx;

/**
 * @brief
 *
 * @return int
 */
int dsm_wkr_ctx_init();

/**
 * @brief
 *
 */
void dsm_wkr_ctx_cleanup();

/**
 * @brief Retourne l'index de la page associé a cette adresse
 *
 * @param addr
 * @return long l'index de la page qui contient addr
 */
long addr_to_page_id(uint64_t addr);

long page_id_to_addr(long page_id);
/**
 * @brief Recupere le node_id du manager qui gere page_id
 *
 * @param page_id page dont on veut le manager
 * @return int node_id du manager, -1 en cas d'erreur
 */
int get_mgr_node_id_for_page(unsigned long page_id);

/**
 * @brief Recupere le fd de la socket du manager qui gere la page
 *
 * @param page_id la page dont on veut recupere le manager
 * @return int fd de la socket du manager, ou -1 en cas d'erreur
 */
int get_mgr_node_socket_for_page(unsigned long page_id);
/**
 * @brief
 *
 * @param arg
 */
void dispatch_call_uffd(void *arg);

/**
 * @brief
 *
 * @param arg
 */
void dispatch_call_wkr(void *arg);

dsm_requests_reg_entry *get_request_entry(unsigned long page_id); 
pthread_mutex_t get_mgr_send_lock_for_page(unsigned long page_id); 

#endif // DSM_WORKER_H