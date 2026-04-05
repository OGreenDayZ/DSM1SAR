#ifndef DSM_CONTAINER_OF_H
#define DSM_CONTAINER_OF_H


/**
 * Récupère la structure parente à partir d'un membre
 * @ptr:    le pointeur vers le membre
 * @type:   le type de la structure parente
 * @member: le nom du membre dans la structure parente
 */
#define dsm_container_of(ptr, type, member)                                         \
    ({                                                                                   \
        const typeof(((type *)0)->member) *__mptr = (ptr);                               \
        (type *)((char *)__mptr - offsetof(type, member));                               \
    })

#endif // DSM_CONTAINER_OF_H