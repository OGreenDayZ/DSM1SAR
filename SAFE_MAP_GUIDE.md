# Guide d'utilisation de dsm\_safe\_map

## Vue d'ensemble

dsm\_safe\_map est une table de hachage thread-safe basée sur des listes de chaînage (hlist) avec des verrous par bucket pour synchroniser les accès concurrents. Elle est optimisée pour les lecteurs multiples avec des verrous en lecture/écriture (rwlock).

### Caractéristiques

* ✅ Accès concurrent sécurisé (rwlock par bucket)
* ✅ Nombre de buckets = 2^nbits (puissance de 2)
* ✅ Distribution uniforme avec fonction hash basée sur le nombre d'or
* ✅ Support pour itération sécurisée et suppression d'éléments

***

## Structure

```c
typedef struct {
    struct dsm_hlist_head *heads;    // Tableaux des têtes de listes (buckets)
    pthread_rwlock_t *locks;         // Un verrou par bucket
    unsigned int nbits;              // Exposant : 2^nbits buckets
} dsm_safe_map;
```

***

## Initialisation et destruction

### Initialiser une map

```c
dsm_safe_map map;
unsigned int bits = 10;  // 2^10 = 1024 buckets

int rc = dsm_safe_map_init(&map, bits);
if (rc != 0) {
    LOG_ERROR("Erreur lors de l'initialisation de la map");
}
```

Paramètres :

* bits : Exposant pour le nombre de buckets (2^bits)

  * Exemple : bits=10 → 1024 buckets
  * Choix : Balance entre taille mémoire et contentions

Retour :

* 0 : Succès
* -1 : Allocation mémoire échouée
* Autre : Erreur lors de l'initialisation des rwlock

### Détruire une map

```c
dsm_safe_map_destroy(&map);
```

⚠️ IMPORTANT : Vous DEVEZ vider la map avant de la détruire !

```c
// Avant destroy(), vider tous les éléments
unsigned int bkt;
struct dsm_hlist_node *tmp;
struct my_item *item;

dsm_hash_safe_for_each_safe(&map, bkt, tmp, item, node) {
    dsm_hlist_del_init(&item->node);
    free(item);  // Si alloué dynamiquement
}

dsm_safe_map_destroy(&map);
```

***

## Opérations de base

### Ajouter un élément

```c
struct my_item {
    struct dsm_hlist_node node;  // Champ mandatory pour la hashmap
    int key;
    char *value;
};

struct my_item *item = malloc(sizeof(*item));
item->key = 42;
item->value = "hello";

dsm_hash_safe_add(&map, &item->node, item->key);
```

Macro : dsm\_hash\_safe\_add(m, node, key)

* m : La map
* node : Le champ dsm\_hlist\_node de votre structure
* key : La clé (utilisée pour calculer le bucket)

### Supprimer un élément

```c
dsm_hash_safe_del(&map, &item->node, item->key);
free(item);
```

Macro : dsm\_hash\_safe\_del(m, node, key)

***

## Recherche (lecture simple)

### Chercher un élément avec dsm\_hash\_safe\_get

```c
struct my_item *found = NULL;
int search_key = 42;

struct my_item *obj;
dsm_hash_safe_get(&map, obj, node, search_key) {
    if (obj->key == search_key) {
        found = obj;
    }
}
dsm_hash_safe_get_end(&map, search_key);

if (found) {
    printf("Trouvé : %s\n", found->value);
}
```

⚠️ RÈGLES IMPORTANTES :

1. Toujours appeler dsm\_hash\_safe\_get\_end() après la boucle, même si rien n'est trouvé
2. Ne pas faire de return qui échappe à la boucle sans passer par get\_end
3. La map est verrouillée en lecture pendant la boucle

***

## Itération sur tous les éléments

### Itération simple (lecture seule)

```c
unsigned int bkt;
struct my_item *item;

dsm_hash_safe_for_each(&map, bkt, item, node) {
    printf("Clé : %d, Valeur : %s\n", item->key, item->value);
}
```

Macro : dsm\_hash\_safe\_for\_each(m, bkt, obj, member)

Caractéristiques :

* ✅ Thread-safe (verrous en lecture)
* ✅ Itère sur TOUS les buckets
* ❌ Ne peut pas supprimer d'éléments pendant l'itération

### Itération avec suppression possible

```c
unsigned int bkt;
struct dsm_hlist_node *tmp;
struct my_item *item;

dsm_hash_safe_for_each_safe(&map, bkt, tmp, item, node) {
    if (item->key == to_delete) {
        // Important : utiliser dsm_hlist_del_init directement
        // (PAS dsm_hash_safe_del car il essaierait de re-acquérir le wrlock)
        dsm_hlist_del_init(&item->node);
        free(item);
    }
}
```

⚠️ Lors de la suppression :

* Le verrou du bucket est toujours tenu (wrlock)
* Appeler dsm\_hlist\_del\_init() directement
* NE PAS appeler dsm\_hash\_safe\_del() → deadlock !

***

## Utilitaires

### Vérifier si la map est vide

```c
if (dsm_safe_map_empty(&map)) {
    printf("Map vide\n");
}
```

⚠️ Attention : Le résultat peut être périmé dès le retour (concurrence)

### Compter les éléments

```c
size_t count = dsm_safe_map_count(&map);
printf("Total d'éléments : %zu\n", count);
```

⚠️ Attention : Le résultat peut être périmé dès le retour (ne pas utiliser pour du CAS)

***

## Exemple complet

```c
#include 
```

​
