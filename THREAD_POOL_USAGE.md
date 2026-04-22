# Fiche d'Utilisation : Thread Pool (dsm\_thread\_pool)

## Vue d'ensemble

Le thread pool (thread\_pool.c / thread\_pool.h) gère un ensemble de threads worker qui exécutent des tâches en attente dans une file circulaire. C'est une implémentation élégante d'un pattern producteur-consommateur. &#x20;

## 1. Structure de base

### Création d'un pool

```c
#include "util/thread_pool.h"

// Créer un pool avec 4 threads et une queue de 16 tâches max
dsm_thread_pool *pool = pool_create(4, 16);

if (pool == NULL) {
    perror("Impossible de créer le pool");
    return 1;
}
```

### Paramètres

* nb\_threads : Nombre de threads workers (typiquement sysconf(\_SC\_NPROCESSORS\_ONLN))
* size\_q : Capacité max de la file d'attente (queue circulaire)

***

## 2. Ajouter des tâches

### Fonction : pool\_add\_task()

```c
// Une tâche est une fonction void (*func)(void *) + ses arguments
void my_task(void *arg) {
    int *value = (int *)arg;
    printf("Exécution avec value=%d\n", *value);
    free(value);
}

int main() {
    dsm_thread_pool *pool = pool_create(4, 16);
    
    int *data = malloc(sizeof(int));
    *data = 42;
    
    // Ajouter la tâche
    int ret = pool_add_task(pool, my_task, data);
    
    if (ret == 0) {
        printf("✓ Tâche ajoutée avec succès\n");
    } else if (ret == THREAD_FULL_QUEUE) {
        printf("✗ Queue pleine, retentez plus tard\n");
    } else if (ret == THREAD_SHUTING_DOWN) {
        printf("✗ Le pool est en cours de fermeture\n");
    }
    
    return 0;
}
```

***

## 3. Codes d'erreur

| Code                  | Signification                                |
| --------------------- | -------------------------------------------- |
| 0                     | ✓ Succès                                     |
| THREAD\_INVALID\_ARG  | Arguments invalides (pool=NULL ou func=NULL) |
| THREAD\_SHUTING\_DOWN | Impossible d'ajouter, le pool ferme          |
| THREAD\_FULL\_QUEUE   | Queue pleine, impossible d'ajouter           |
| THREAD\_MUTEX\_ERR    | Erreur d'opération sur mutex                 |
| THREAD\_COND\_ERR     | Erreur d'opération sur condition variable    |
| THREAD\_JOIN\_ERR     | Erreur lors du join des threads              |
| THREAD\_ERR           | Erreur générique                             |

***

## 4. Destruction du pool

Il existe deux modes de fermeture :

### 4.1 Graceful shutdown

```c
// Les threads terminent les tâches en attente, puis s'arrêtent
int ret = pool_destroy(pool, graceful_shutdown);

if (ret == 0) {
    printf("✓ Pool fermé proprement\n");
}
```

* Les threads existants terminent complètement
* Les tâches dans la queue sont exécutées
* Les nouvelles tâches sont refusées

### 4.2 Brutal shutdown

```c
// Les threads s'arrêtent immédiatement, les tâches en attente sont perdues
int ret = pool_destroy(pool, brutal_shutdown);
```

* Les threads s'arrêtent dès que possible
* Les tâches en attente ne sont pas exécutées
* À utiliser si vous ne tenez pas aux tâches en cours

***

## 5. Exemple complet

```c
#include "util/thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Tâche simple : écrire un nombre
void print_task(void *arg) {
    int *id = (int *)arg;
    printf("Tâche %d en cours d'exécution sur thread %ld\n", *id, pthread_self());
    sleep(1);
    free(id);
}

int main() {
    // 1. Créer le pool : 4 threads, queue de 32 tâches
    dsm_thread_pool *pool = pool_create(4, 32);
    if (!pool) {
        perror("pool_create");
        return 1;
    }

    // 2. Ajouter 20 tâches
    for (int i = 0; i < 20; i++) {
        int *task_id = malloc(sizeof(int));
        *task_id = i;

        // Retry si queue pleine
        while (pool_add_task(pool, print_task, task_id) == THREAD_FULL_QUEUE) {
            usleep(100000); // Attendre 100ms
        }
    }

    // 3. Attendre la fin de toutes les tâches
    printf("\n✓ Tâches soumises, attente de la fin...\n");

    // 4. Destruction gracieuse
    int ret = pool_destroy(pool, graceful_shutdown);
    if (ret == 0) {
        printf("✓ Pool détruit avec succès\n");
    }

    return 0;
}
```

***

## 6. Bonnes pratiques

### ✅ À faire

```c
// Gérer les retours d'erreur
int ret = pool_add_task(pool, func, arg);
if (ret != 0) {
    // Gérer l'erreur
    if (ret == THREAD_FULL_QUEUE) {
        // Retenter après un petit délai
        usleep(100);
    }
}

// Utiliser graceful_shutdown pour laisser finir les tâches
pool_destroy(pool, graceful_shutdown);

// Allouer de la mémoire pour les arguments (elle sera responsabilité du handler)
int *data = malloc(sizeof(int));
*data = 42;
pool_add_task(pool, my_task, data);
// my_task doit free(data)
```

### ❌ À éviter

```c
// Ne pas passer des pointeurs de stack comme argument
int local_var = 42;
pool_add_task(pool, my_task, &local_var);  // ⚠️ Danger ! local_var disparaît

// Ne pas ajouter de tâches après un shutdown sans vérifier THREAD_SHUTING_DOWN
pool_destroy(pool, graceful_shutdown);
pool_add_task(pool, func, arg);  // ⚠️ Retournera THREAD_SHUTING_DOWN

// Ne pas faire pool_free() manuellement après pool_destroy()
// pool_destroy() l'appelle automatiquement
```

***

## 7. Utilisation avec les variables globales ou partagées

### Compteur atomique

```c
#include <stdatomic.h>

atomic_int counter = 0;

void increment_task(void *arg) {
    atomic_fetch_add(&counter, 1);
}

// Dans main():
dsm_thread_pool *pool = pool_create(4, 32);
for (int i = 0; i < 100; i++) {
    pool_add_task(pool, increment_task, NULL);
}
pool_destroy(pool, graceful_shutdown);
printf("Valeur finale : %d\n", atomic_load(&counter));
```

### Avec mutex

```c
typedef struct {
    pthread_mutex_t lock;
    int value;
} shared_data_t;

void modify_task(void *arg) {
    shared_data_t *data = (shared_data_t *)arg;
    pthread_mutex_lock(&data->lock);
    data->value++;
    pthread_mutex_unlock(&data->lock);
}
```

***

## 8. Constantes utiles

```c
#define DSM_DEFAULT_NB_THREADS sysconf(_SC_NPROCESSORS_ONLN)  // Nombre de CPU
#define DSM_DEFAULT_QUEUE_SIZE 128                             // Queue par défaut
```

***

## Résumé des fonctions

| Fonction                           | Retour              | Description                           |
| ---------------------------------- | ------------------- | ------------------------------------- |
| pool\_create(nb\_threads, size\_q) | dsm\_thread\_pool\* | Crée un pool                          |
| pool\_add\_task(pool, func, arg)   | int (code erreur)   | Ajoute une tâche                      |
| pool\_destroy(pool, shutdown)      | int (code erreur)   | Ferme le pool                         |
| pool\_free(pool)                   | int (code erreur)   | Libère manuellement (rarement appelé) |
| pool\_slave\_routine(pool)         | interne             | Fonction exécutée par chaque thread   |

​
