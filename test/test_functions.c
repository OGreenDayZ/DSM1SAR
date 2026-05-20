#include "util/test_functions.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "dsm.h"
#include "util/test_commun.h"

void *test_simple_read(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    LOG_THREAD(tid, "Tentative de LECTURE à %p", data->addr);
    int val = *(int *)data->addr;

    LOG_THREAD(tid, BG_SUCCESS "LECTURE réussie" RESET " → Valeur lue = %d", val);
    data->ret = 0;
    return NULL;
}

void *test_simple_write(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    int node_id = dsm_get_node_id();
    int new_val = 100 + node_id;
    int sauvegarde;
    LOG_THREAD(tid, "Tentative d'ÉCRITURE (val=%d) à %p", new_val, data->addr);
    *(int *)data->addr = new_val;
    sauvegarde = *(int *)data->addr;
    LOG_THREAD(tid, BG_SUCCESS "ÉCRITURE réussie" RESET " → Nouvelle valeur = %d",
               new_val);
    if (sauvegarde == new_val) {
        LOG_SUCCESS("Verification de la valeur : OK");
        data->ret = 0;
    } else {
        LOG_ERROR("Verification de la valeur : FAIL");
        data->ret = -1;
        return NULL;
    }
    return NULL;
}

void *test_concurrent_writes(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    int final = 0;
    int debut = *(int *)data->addr;
    for (int i = 0; i < 3; i++) {
        int val = debut + i;
        LOG_THREAD(tid, "Écriture concurrente #%d (val=%d)", i, val);
        *(int *)data->addr = val;
    }
    final = *(int *)data->addr;
    if (final > debut) {
        LOG_INFO("Écritures concurrentes terminées avec succés (Debut - %d, Final = %d)",
                 debut, final);
        data->ret = 0;
    } else {
        LOG_INFO("Écritures concurrentes fail (Debut - %d, Final = %d)", debut, final);
        data->ret = -1;
        return NULL;
    }
    return NULL;
}

void *test_lock_unlock(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    LOG_THREAD(tid, "Demande de dsm_lock() sur %p", data->addr);

    if (dsm_lock(data->addr) == 0) {
        LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
        sleep(1);
        dsm_unlock(data->addr);
        LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
        data->ret = 0;
    } else {
        LOG_ERROR("Thread %lu : Échec du lock !", tid);
        data->ret = -1;
        return NULL;
    }
    return NULL;
}

void *test_concurrent_write_with_locks(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    if (dsm_lock(data->addr) == 0) {
        LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
        int val = *(int *)data->addr;
        LOG_THREAD(tid, "Incrémentation sous verrou (ancienne val=%d)", val);
        *(int *)data->addr = val + 1;
        LOG_THREAD(tid, "Écriture concurrente (val=%d)", val + 1);
        dsm_unlock(data->addr);
        LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
        data->ret = 0;
    } else {
        LOG_ERROR("Thread %lu : Échec du lock !", tid);
        data->ret = -1;
        return NULL;
    }
    LOG_THREAD(tid, BG_SUCCESS "Écriture concurrente sous locks terminée" RESET);
    return NULL;
}

void *test_concurrent_writes_with_locks(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    for (int i = 0; i < 3; i++) {
        if (dsm_lock(data->addr) == 0) {
            LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
            int val = *(int *)data->addr;
            LOG_THREAD(tid, "Incrémentation sous verrou (ancienne val=%d)", val);
            *(int *)data->addr = val + 1;
            LOG_THREAD(tid, "Écriture concurrente #%d (val=%d)", i, val + 1);
            dsm_unlock(data->addr);
            LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
            data->ret = 0;
        } else {
            LOG_ERROR("Thread %lu : Échec du lock !", tid);
            data->ret = -1;
            return NULL;
        }
    }
    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes sous locks terminées" RESET);
    return NULL;
}

void *test_concurrent_reads_with_locks(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    unsigned long tid = (unsigned long)pthread_self();
    for (int i = 0; i < 3; i++) {
        if (dsm_lock(data->addr) == 0) {
            LOG_THREAD(tid, BOLD MAGENTA "VERROU ACQUIS" RESET " - Section critique");
            LOG_THREAD(tid, "Tentative de LECTURE à %p", data->addr);
            int val = *(int *)data->addr;
            LOG_THREAD(tid, BG_SUCCESS "LECTURE réussie" RESET " → Valeur = %d", val);
            dsm_unlock(data->addr);
            LOG_THREAD(tid, CYAN "VERROU LIBÉRÉ (dsm_unlock)" RESET);
        } else {
            LOG_ERROR("Thread %lu : Échec du lock !", tid);
        }
    }
    LOG_THREAD(tid, BG_SUCCESS "Écritures concurrentes sous locks terminées" RESET);
    return NULL;
}

void *test_coherence_read_write(void *arg) {
    thread_test_arg_t *data = (thread_test_arg_t *)arg;
    int node_id = dsm_get_node_id();
    int *val_ptr = (int *)data->addr;

    // Le Nœud 1 écrit la valeur magique
    if (node_id == 1) {
        int magic_value = 4242;
        *val_ptr = magic_value;
        LOG_INFO("[Nœud %d] Écriture de la valeur magique: %d ", node_id, magic_value);
    }

    // Synchronisation globale de la mémoire
    dsm_barrier();

    // Les autres nœuds lisent et valident la cohérence
    if (node_id != 1) {
        int received = *val_ptr;
        if (received == 4242) {
            LOG_INFO("[Nœud %d] Succès : Cohérence validée (%d == %d) ", node_id,
                     received, 4242);
        } else {
            LOG_ERROR("[ERREUR] [Nœud %d] Incohérence : Reçu %d au lieu de 4242 ",
                      node_id, received);
        }
    }
    return NULL;
}

void *test_barrier_thread(void *arg) {
    barrier_test_arg_t *t_args = (barrier_test_arg_t *)arg;
    int tid = t_args->thread_id;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, "Test #%d : Arrive à la barrière (en attente)", tid);
    dsm_barrier();
    LOG_THREAD(ptid, BG_SUCCESS "Barrière franchie !" RESET);

    return NULL;
}

void *test_lock_unlock_barrier_thread(void *arg) {
    test_lock_unlock(arg);
    unsigned long ptid = (unsigned long)pthread_self();
    LOG_THREAD(ptid, "Arrive à la barrière (en attente)");
    dsm_barrier();
    LOG_THREAD(ptid, BG_SUCCESS "Barrière franchie !" RESET);

    return NULL;
}

void *test_barrier_advanced_thread(void *arg) {
    thread_test_arg_t *t_args = (thread_test_arg_t *)arg;
    int tid = t_args->id;
    int *data = t_args->addr;
    unsigned long ptid = (unsigned long)pthread_self();

    LOG_THREAD(ptid, BOLD MAGENTA "Arrive à la barrière avancée" RESET);
    dsm_barrier_advanced(3, 0); // Attend 3 threads + le thread principal
    LOG_THREAD(ptid, BG_SUCCESS "Barrière avancée franchie !" RESET);

    // Test du verrou après la barrière
    LOG_THREAD(ptid, "Tentative d'acquisition du verrou...");
    dsm_lock(&data[100]);
    LOG_THREAD(ptid, BOLD MAGENTA "Verrou acquis" RESET " - Modification en cours");
    data[100] += (tid + 1);
    dsm_unlock(&data[100]);
    LOG_THREAD(ptid, BG_SUCCESS "Verrou relâché" RESET);

    LOG_THREAD(ptid, BOLD MAGENTA "Arrive à la barrière avancée" RESET);
    dsm_barrier_advanced(3, 0); // Attend 3 threads + le thread principal
    LOG_THREAD(ptid, BG_SUCCESS "Barrière avancée franchie !" RESET);
    // Finalisation avancée
    LOG_INFO(BG_DEBUG "Appel de dsm_finalize()" RESET);
    dsm_finalize();
    return NULL;
}

int test_init(dsm_config *config, int *node_id) {
    LOG_TITLE("  TEST COMPLET DSM   ");
    // INITIALISATION
    LOG_SECTION("ÉTAPE 1 : Initialisation du contexte DSM");
    dsm_get_default_config(config);

    config->net.system.nb_nodes = 4;
    config->net.system.nb_managers = 2;
    config->net.node.role = DSM_ROLE_MANAGER;

    LOG_INFO("Configuration: 4 nœuds, 2 managers");
    int ret = dsm_init(config);

    if (ret < 0) {
        LOG_ERROR("Erreur fatale : dsm_init a échoué");
        return EXIT_FAILURE;
    } else if (ret == DSM_ROLE_MANAGER) {
        LOG_SUCCESS("Mode MANAGER activé");
        return 2;
    }

    *node_id = dsm_get_node_id();
    LOG_SUCCESS("Mode WORKER activé (Node ID: %d)", *node_id);
    // ALLOCATION
    LOG_SECTION("ÉTAPE 2 : Allocation de mémoire partagée");

    int *shared_data = (int *)dsm_alloc();
    if (!shared_data) {
        LOG_ERROR("Erreur d'allocation DSM");
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Page allouée à l'adresse : %p", (void *)shared_data);
    return EXIT_SUCCESS;
}

#if 0
int main() {
    // Ton ancien code de test temporaire
    return 0;
}
#endif