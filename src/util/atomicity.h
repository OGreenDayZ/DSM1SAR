#ifndef DSM_ATOMICITY_H
#define DSM_ATOMICITY_H

#include <stdatomic.h>
#include <stdbool.h>

/*
 * @note : Pour les compteurs type atomic_uint
 * @brief : Utilisé pour du CAS (Compare And Swap)
 * @note : Si atomic_compare_exchange_weak fail, current est màj avec la valeur actuelle
 * de counter
 */
static inline bool increment_if_less(atomic_uint *counter, unsigned int limit) {
    unsigned int current = atomic_load(counter);
    while (current < limit) {
        if (atomic_compare_exchange_weak(counter, &current, current + 1)) {
            return true;
        }
    }
    return false;
}

#endif // DSM_ATOMICITY_H