/**
 * @ref : https://github.com/mbrossard/threadpool/blob/master/src/threadpool.c
 */

#ifndef DSM_THREAD_POOL_H
#define DSM_THREAD_POOL_H
#include <pthread.h>

#define NB_THREADS 12

/**
 * @brief Structure des types d'erreurs possibles sur les fonctions lié au pool de threads
 */
typedef enum {
    THREAD_POOL_EMPTY = -2, 
    THREAD_POOL_FULL = -3, 
    THREAD_MUTEX_ERR = -4,
    THREAD_COND_ERR = -5,
    THREAD_JOIN_ERR = -6, 
    THREAD_INVALID_ARG = -7, 
    THREAD_SHUTING_DOWN = -8, 
    THREAD_FULL_QUEUE = -9, 
    THREAD_ERR = -10, 
    THREAD_FREE_ERR = -11
} dsm_threadpool_errors; 

typedef enum {
    brutal_shutdown = 1,
    graceful_shutdown  = 2
} dsm_pool_shutdown; 

/**
 * @brief Structure représentant une tâche
 * 
 */
typedef struct {
    void (*func)(void* arg);  /**< fonction exécuter par le thread */
    void* args; /**< arguments de la fonctions */    
} dsm_thread_task;

/**
 * @brief Structure contenant la queue de tâche
 * Qui est ici une liste circulaire. 
 * 
 */
typedef struct{ 
    dsm_thread_task*  tasks; /**< tableau circulaire des tâches en attente */
    int capacity;      /**< capacité max du tableau */
    int count;         /**< nombre d'element dans le tableau */
    int head;          /**< indice du prochaine element a sortir de la liste */
    int tail;          /**< indice du prochaine emplacement libre dans la liste */
    
    pthread_mutex_t mutex; /**< mutex qui protège tous les champs de cette structure. */
    pthread_cond_t notify; /**< var condition a signalé quand une tâche est disponible. */
} dsm_task_queue; 

/**
 * @brief Structre gerant le pool de threads
 */
typedef struct {
    pthread_t* threads; /**< tableau de threads */
    int thread_count; /**< nombre max de threads dans le tableau */
    int thread_started; /**< nombre de threads commencé */
    dsm_task_queue* queue; /**< pointeur vers la file d'attente */
    volatile int shutdown; /**< flag qui demande la fin/fermeture  */
} dsm_thread_pool; 

/**
 * @brief Crée un thread pool.
 * 
 * @param nb_threads nombre de thread dans le thread pool 
 * @param size_q taille de la file d'attente
 * @return dsm_thread_pool* retourn NULL si pas possible de crée le pool, sinon retourne le pool crée
 */
dsm_thread_pool* pool_create(int nb_threads, int size_q); 
/**
 * @brief fonction appelé par les threads qui vas leur assigné des tâches (tasks) exécuter par les threads esclaves.  
 * 
 * @param threadpool pointeur vers un dsm_thread_pool
 * @return void* valeur de retour du thread esclave
 */
void* pool_slave_routine(void* pool); 
/**
 * @brief Ajoute une tâche dans la file d'attente
 * 
 * @param pool le pool de threads
 * @param func la fonction de tâche a ajouté
 * @param arg les arguments de la fonction
 * @return int code d'erreur ou 0 en cas de succés
 */
int pool_add_task(dsm_thread_pool* pool, void (*func)(void*), void* arg); 

/**
 * @brief Détruit le pool de threads
 * 
 * @param pool pool de thread a détruire
 * @param shutdown mode de deestruction : 
 *        graceful_shutdown - attends la fin de toute les tâches en cours, avant de détruire le pool
 *        brutal_shutdown - n'attends pas la fin de toute les tâches et détruit directement le pool
 * @return int code d'erreur ou 0 en cas de succés
 */
int pool_destroy(dsm_thread_pool *pool, dsm_pool_shutdown shutdown); 
// désalloue toute les ressources alloué avec un malloc + détruit le mutex
/**
 * @brief Libere toute les ressources alloué par le pool
 * 
 * @param pool pool de thread a liberer 
 * @return int code d'erreur ou 0 en cas de succés, 
 */
int pool_free(dsm_thread_pool* pool); 

#endif // DSM_THREAD_POOL_H