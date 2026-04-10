#ifndef DSM_THREAD_POOL_H
#define DSM_THREAD_POOL_H
#include <pthread.h>

typedef struct {
    void (*func)(void* arg);  // fonction exécuter par le thread
    void* args; // arguments de la fonctions               
} dsm_thread_task;

// structure (ou pas) pour la file d'attente 
// mutex 
typedef struct{
dsm_thread_task* tasks; 
    // Pour que la liste fonctionne comme un tampon circulaire
    int capacity;      // Taille max du tableau
    int count;         // Nombre d'éléments actuels
    int head;          // Indice du prochain élément à sortir
    int tail;          // Indice du prochain emplacement libre
    
    pthread_mutex_t mutex; 
    pthread_cond_t not_empty; // Y'a une tache a consommer
    pthread_cond_t not_full;  // Possible d'ajouter une nouvelle tâche
} dsm_task_queue; 

typedef struct {
    pthread_t* threads; // pas besoin de mutex ici car pas accéder en concurrence 
    int thread_count;
    dsm_task_queue* queue; // pointeur (pas tableau) car faut pas copier mutex
    volatile int shutdown; // flag pour arrêter proprement les threads
} dsm_thread_pool; 

dsm_thread_pool* pool_create(int num_threads, int queue_size); 
int pool_destroy(dsm_thread_pool* pool); 
int pool_add_task(dsm_thread_pool* pool, void (*func)(void*), void* arg); 
void* worker_routine(void* arg); 

#endif // DSM_THREAD_POOL_H