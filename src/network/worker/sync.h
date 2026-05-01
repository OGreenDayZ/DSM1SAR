#ifndef DSM_SYNC_H
#define DSM_SYNC_H

#include <pthread.h>

typedef enum {
    BARR_RESET,    // Prêt pour un nouveau cycle
    BARR_WAITING,  // Les threads dorment
    BARR_TRIGGERED // Le signal socket a été reçu, réveil en cours
} barrier_state;

typedef enum {
    FINA_RESET, // Init, mais pas de cycle
    FINA_WAITING,
    FINA_TRIGGERED
} finalize_state;

typedef struct {

    pthread_mutex_t lock;
    pthread_cond_t wait_cond;
    barrier_state state;

    unsigned int targ_mgr_id; // Le responsable de la barrière, 0 par défaut
    unsigned int targ_count;  // 0 non initialisé, sinon le nombre de threads qui dorment
                              // sur wait_cond
    unsigned int curr_count;

} sync_barrier; // Pour dsm_mgr_sync

typedef struct {

    pthread_mutex_t lock;
    pthread_cond_t wait_cond;
    finalize_state state;

    unsigned int targ_count; // 0 non initialisé, sinon le nombre de threads qui dorment
                             // sur wait_cond
    unsigned int curr_count;

    unsigned int missing_mgr_acks;

} sync_finalize; // Pour dsm_mgr_sync

int send_sync_barr_req(int sock);
int send_sync_fina_req(int sock);

/* *********************************************************************************/
/** @warning: TOUTES les fonctions doivent être appelées APRES acquisition du lock */
/* *********************************************************************************/

/* ******************** */
int barrier_init(sync_barrier *barr);
int barrier_define(sync_barrier *barr, unsigned int threads_count,
                   unsigned int manager_id);
int barrier_wait(sync_barrier *barr);
int barrier_trigger(sync_barrier *barr);

int finalize_init(sync_finalize *fina);
int finalize_define(sync_finalize *fina, unsigned int threads_count);
int finalize_wait(sync_finalize *fina);
int finalize_trigger(sync_finalize *fina);
/* ******************** */

#endif // DSM_SYNC_H