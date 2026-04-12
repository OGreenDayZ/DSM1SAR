// #ifdef DSM_HLIST_H 
#define DSM_HLIST_H 

#include "container_of.h"
#include <stdio.h>

struct dsm_hlist_head {
	struct dsm_hlist_node *first;
};

struct dsm_hlist_node {
	struct dsm_hlist_node *next, **pprev;
}; 


/**
 * @brief DSM_HLIST_HEAD_INIT - initialisation statique d'une tête de liste 
 * @example utilisation : struct dsm_hlist_head my_head = DSM_HLIST_HEAD_INIT;
 * 
 * @brief DSM_HLIST_HEAD - déclare ET initialise une tête de liste en une ligne
 * @param name nom de la variable 
 * @example utilisation : DSM_HLIST_HEAD(my_head) equiv à struct dsm_hlist_head my_head = DSM_HLIST_HEAD_INIT
 * 
 * @brief dsm_init_hlist_node - initialise un nœud à l'état "non inséré"
 * un noeud non inséré est un noeud "unhashed" quand prev == NULL. 
 * @param node noeud à initialiser 
 */
#define DSM_HLIST_HEAD_INIT    { .first = NULL }
#define DSM_INIT_HLIST_HEAD(ptr)  ((ptr)->first = NULL )
#define DSM_HLIST_HEAD(name)   struct dsm_hlist_head name = DSM_HLIST_HEAD_INIT

static inline void dsm_init_hlist_node(struct dsm_hlist_node *node)
{
    node->next  = NULL;
    node->pprev = NULL;
}

/**
 * @brief Verifie si le noeud est un noeud non inséré. 
 * 
 * @param n le noeud a vérifier
 * @return 1 si le noeud n'est pas inséré et 0 sinon. 
 */
static inline int dsm_hlist_unhashed(const struct dsm_hlist_node *n)
{
    return !n->pprev;
}

/**
 * @brief Verifie si la liste est vide
 * 
 * @param h la tête de la liste
 * @return 1 si la liste est vide et 0 sinon. 
 */
static inline int dsm_hlist_empty(const struct dsm_hlist_head *h)
{
    return !h->first;
}

static inline void __dsm_hlist_del(struct dsm_hlist_node *n)
{
    struct dsm_hlist_node  *next  = n->next;
    struct dsm_hlist_node **pprev = n->pprev;

    *pprev = next;                  
    if (next)
        next->pprev = pprev;      
}

/**
 * @brief Supprime un élement de la liste et le remet à l'état unhashed. 
 * 
 * @param n le noeud a supprimé si il est inséré.
 */
static inline void dsm_hlist_del_init(struct dsm_hlist_node *n)
{
    if (!dsm_hlist_unhashed(n)) {
        __dsm_hlist_del(n);
        dsm_init_hlist_node(n);
    }
}

/**
 * @brief Insertion en tête de liste
 * 
 * @param n noeud a ajouter
 * @param h tête de la liste
 */
static inline void dsm_hlist_add_head(struct dsm_hlist_node *n,
                                      struct dsm_hlist_head *h)
{
    struct dsm_hlist_node *first = h->first;

    n->next = first;
    n->pprev = &h->first;          

    if (first)
        first->pprev = &n->next;    

    h->first = n;                
}


/**
 * @brief Récupère le pointeur vers la structure parente à partir d'un nœud de liste. 
 * 
 * @warning comportement indéfini si @ptr est null. 
 * 
 * @param ptr pointeur vers le membre de type dsm_hlist_node
 * @param type type de la structure parente.
 * @param member  nom du champ dsm_hlist_node dans la structure parente.
 * 
 * @return Pointeur vers la structure parente 
 */
#define dsm_hlist_entry(ptr, type, member) dsm_container_of(ptr,type,member)

/**
 * @brief Récupère le pointeur vers la structure parente, avec vérification de NULL.
 */
#define dsm_hlist_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? dsm_hlist_entry(____ptr, type, member) : NULL; \
	})

/**
 * @brief Itère sur une hlist en autorisant la suppression de l'entrée courante.
 * 
 * @warning comportement indéfini lors d'une suppression pendant le parcours
 * 
 * @param pos    Pointeur de type (type *) utilisé comme curseur courant.
 *               Mis à jour automatiquement à chaque itération.
 * @param n      Pointeur vers un dsm_hlist_node servant à sauvegarder
 *               le nœud suivant avant toute modification.
 * @param head   Pointeur vers la tête de liste (dsm_hlist_head).
 * @param member Nom du champ dsm_hlist_node dans la structure parente.
 * 
 */
#define dsm_hlist_for_each_entry_safe(pos, n, head, member) 		\
	for (pos = dsm_hlist_entry_safe((head)->first, typeof(*pos), member);\
	     pos && ({ n = pos->member.next; 1; });			\
	     pos = dsm_hlist_entry_safe(n, typeof(*pos), member))

/**
 * @brief Itère sur une hlist en autorisant la suppression de l'entrée courante, safe pour la suppression. 
 */
#define dsm_hlist_for_each_entry(pos, head, member)				\
	for (pos = dsm_hlist_entry_safe((head)->first, typeof(*(pos)), member);\
	     pos;							\
	     pos = dsm_hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))	
		 	 
// #endif // DSM_HLIST_H