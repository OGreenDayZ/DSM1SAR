// #ifdef DSM_HASHMAP_H 
#define DSM_HASHMAP_H 

#include "hlist.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>


/** 
 * @brief Donne le nombre de buckets dans la hashmap qui est une puissance de 2
 * 
 * @param name : nom de la hashtable 
*/
#define DSM_HASH_SIZE(name)  (sizeof(name) / sizeof((name)[0]))

/**
 * @brief Donne l'exposant k du nombre de buckets qui est égale a 2^k 
 * 
 * @param name : nom de la hashtable
*/
#define DSM_HASH_BITS(name)  (__builtin_ctz(DSM_HASH_SIZE(name)))

/**
 * @brief Fonction de hash qui utilise le nombre d'or qui permet une distribution uniforme sur la map
 * 
 * @param val : la clé a hasher
 * @param bits : le nombre de bits du résultat
 * (viens tout droit du kernel)
*/
static inline uint32_t dsm_hash_32(uint32_t val, unsigned int bits)
{
    /* 0x61C88647 = 2^32 / phi  (phi = nombre d'or) */
    return (val * UINT32_C(0x61C88647)) >> (32 - bits);
}

/**
 * Detection de l'architecture du système (32 et 64 bits)
 * Utilisation de dsm_hash_32 dans le cas d'un système 32 bits 
 * et de dsm_hash_64 dans le cas d'un système 64 bits
 */
#if defined(__LP64__) || defined(_WIN64)
static inline uint32_t dsm_hash_64(uint64_t val, unsigned int bits)
{
    return (uint32_t)((val * UINT64_C(0x61C8864680B583EB)) >> (64 - bits));
}
#  define dsm_hash_min(val, bits) dsm_hash_64((uint64_t)(val), bits)
#else
#  define dsm_hash_min(val, bits) dsm_hash_32((uint32_t)(val), bits)
#endif


/**
 * @brief La taille de la hashtable est défini par name en binaire décalé de (bits) vers la gauche 
 * La taille de la table est forcément une puissance de 2 
 * Ici le DEFINE est statique, avec une taille non dynamique, et qui déclare + initialise la table 
 * 
 * @param name : nom de la table 
 * @param bits : nombre de bits de décalage 
*/
#define DSM_DEFINE_HASHTABLE(name, bits)						\
	struct dsm_hlist_head name[1 << (bits)] =					\
			{ [0 ... ((1 << (bits)) - 1)] = DSM_HLIST_HEAD_INIT }

/**
 * @brief Supprime le node de la map
 * 
 * @param node le noeud a supprimé de la map
 */
static inline void dsm_hash_del(struct dsm_hlist_node *node)
{
    dsm_hlist_del_init(node);
}

 /**
 * @brief Initialisation dynamique de la hashtable
 * 
 * @param ht la map a initialiser -> un struct dsm_hslist_head 
 * @param sz taille de la table a initialiser
 */
static inline void __dsm_hash_init(struct dsm_hlist_head *ht, unsigned int sz)
{
	unsigned int i;
	for (i = 0; i < sz; i++){
		DSM_INIT_HLIST_HEAD(&ht[i]); 
    }
}

#define dsm_hash_init(hashtable) __dsm_hash_init(hashtable, DSM_HASH_SIZE(hashtable))
/**
 * @brief Ajoute un élement à la map, ne renvoie rien.
 * 
 * @param hashtable la map dans laquelle ajouté l'élement
 * @param node le noeud a ajouter
 * @param key la clé du noeud 
 */
#define dsm_hash_add(hashtable, node, key)						\
	dsm_hlist_add_head(node, &hashtable[dsm_hash_min(key, DSM_HASH_BITS(hashtable))])

static inline bool __dsm_hash_empty(struct dsm_hlist_head *ht, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++)
        if (!dsm_hlist_empty(&ht[i]))
            return false;
    return true;
}
/**
 * @brief Renvoie true si la map est vide et false sinon. 
 * 
 * @param hashtable la map à vérifier
 */
#define dsm_hash_empty(hashtable) __dsm_hash_empty(hashtable, DSM_HASH_SIZE(hashtable))


/** Pas safe pour la suppression */

/**
 * @brief Itere sur la map. 
 * N'est pas safe pour la suppression -> ne pas supprimer des elements lors de l'itération.  
 * 
 * @param name la map a itérer 
 * @param bkt un int qui est utilisé comme curseur de l'itération, doit être déclarer précedemment par l'appelant.
 * @param obj un pointeur vers un objet pour stocké l'entrée itérer, doit être déclarer précedemment par l'appelant.
 * @param member nom de l'attribut dsm_hlist_node dans la structure stocké dans la map
 * 
 * @example 
 * int bkt;
 * struct item *obj;
 * dsm_hash_for_each(table, bkt, obj, node) {
 *  printf("%d\n", obj->id);
 * }
 */
#define dsm_hash_for_each(name, bkt, obj, member)				\
	for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < DSM_HASH_SIZE(name);\
			(bkt)++)\
		dsm_hlist_for_each_entry(obj, &name[bkt], member)
/** Safe pour la suppression */
#define dsm_hash_for_each_safe(name, bkt, tmp, obj, member)			\
	for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < DSM_HASH_SIZE(name);\
			(bkt)++)\
		dsm_hlist_for_each_entry_safe(obj, tmp, &name[bkt], member)    

/** Macro de parcours qui itère uniquement sur la bucket de l'element et non sur toute la map */
#define dsm_hash_for_each_possible(name, obj, member, key)          \
    dsm_hlist_for_each_entry(obj,                                   \
        &(name)[dsm_hash_min(key, DSM_HASH_BITS(name))], member)

#define dsm_hash_for_each_possible_safe(name, obj, tmp, member, key) \
    dsm_hlist_for_each_entry_safe(obj, tmp,                          \
        &(name)[dsm_hash_min(key, DSM_HASH_BITS(name))], member)

// #endif // DSM_HASHMAP_H 