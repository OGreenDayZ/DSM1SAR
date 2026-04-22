# Guide d'utilisation de `dsm_safe_map`

## Vue d'ensemble

`dsm_safe_map` est une **table de hachage thread-safe** basée sur des listes de chaînage (`hlist`) avec des verrous par bucket pour synchroniser les accès concurrents. Elle est optimisée pour les lecteurs multiples avec des verrous en lecture/écriture (rwlock).

### Caractéristiques
- ✅ Accès concurrent sécurisé (rwlock par bucket)
- ✅ Nombre de buckets = 2^nbits (puissance de 2)
- ✅ Distribution uniforme avec fonction hash basée sur le nombre d'or
- ✅ Support pour itération sécurisée et suppression d'éléments

---

## Structure

```c
typedef struct {
    struct dsm_hlist_head *heads;    // Tableaux des têtes de listes (buckets)
    pthread_rwlock_t *locks;         // Un verrou par bucket
    unsigned int nbits;              // Exposant : 2^nbits buckets
} dsm_safe_map;
```

---

## Initialisation et destruction

### Initialiser une map

```c
dsm_safe_map map;
unsigned int bits = 10;  // 2^10 = 1024 buckets

int rc = dsm_safe_map_init(&map, bits);
if (rc != 0) {
    perror("Erreur lors de l'initialisation de la map");
}
```

**Paramètres :**
- `bits` : Exposant pour le nombre de buckets (2^bits)
  - Exemple : `bits=10` → 1024 buckets
  - Choix : Balance entre taille mémoire et contentions

**Retour :**
- `0` : Succès
- `-1` : Allocation mémoire échouée
- Autre : Erreur lors de l'initialisation des rwlock

### Détruire une map

```c
dsm_safe_map_destroy(&map);
```

⚠️ **IMPORTANT** : Vous **DEVEZ** vider la map avant de la détruire !
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

---

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

**Macro :** `dsm_hash_safe_add(m, node, key)`
- `m` : La map
- `node` : Le champ `dsm_hlist_node` de votre structure
- `key` : La clé (utilisée pour calculer le bucket)

### Supprimer un élément

```c
dsm_hash_safe_del(&map, &item->node, item->key);
free(item);
```

**Macro :** `dsm_hash_safe_del(m, node, key)`

---

## Recherche (lecture simple)

### Chercher un élément avec `dsm_hash_safe_get`

```c
struct my_item *found = NULL;
int search_key = 42;

struct my_item *obj;
dsm_hash_safe_get(&map, obj, node, search_key) {
    if (obj->key == search_key) {
        found = obj;
        break;  // Important : toujours faire break quand trouvé
    }
}
dsm_hash_safe_get_end(&map, search_key);

if (found) {
    printf("Trouvé : %s\n", found->value);
}
```

⚠️ **RÈGLES IMPORTANTES** :
1. **Toujours appeler `dsm_hash_safe_get_end()` après la boucle**, même si rien n'est trouvé
2. Ne pas faire de `return` qui échappe à la boucle sans passer par `get_end`
3. Toujours faire `break` quand vous trouvez votre élément
4. La map est verrouillée en **lecture** pendant la boucle

---

## Itération sur tous les éléments

### Itération simple (lecture seule)

```c
unsigned int bkt;
struct my_item *item;

dsm_hash_safe_for_each(&map, bkt, item, node) {
    printf("Clé : %d, Valeur : %s\n", item->key, item->value);
}
```

**Macro :** `dsm_hash_safe_for_each(m, bkt, obj, member)`

**Caractéristiques :**
- ✅ Thread-safe (verrous en lecture)
- ✅ Itère sur TOUS les buckets
- ❌ Ne peut pas supprimer d'éléments pendant l'itération

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

⚠️ **Lors de la suppression** :
- Le verrou du bucket est toujours tenu (wrlock)
- Appeler `dsm_hlist_del_init()` directement
- **NE PAS** appeler `dsm_hash_safe_del()` → deadlock !

---

## Utilitaires

### Vérifier si la map est vide

```c
if (dsm_safe_map_empty(&map)) {
    printf("Map vide\n");
}
```

⚠️ **Attention** : Le résultat peut être périmé dès le retour (concurrence)

### Compter les éléments

```c
size_t count = dsm_safe_map_count(&map);
printf("Total d'éléments : %zu\n", count);
```

⚠️ **Attention** : Le résultat peut être périmé dès le retour (ne pas utiliser pour du CAS)

---

## Exemple complet

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/concurrent/safe_map.h"

typedef struct {
    struct dsm_hlist_node node;
    int user_id;
    char name[50];
} User;

int main() {
    dsm_safe_map users;
    
    // Initialisation
    if (dsm_safe_map_init(&users, 5) != 0) {
        perror("init");
        return 1;
    }
    
    // Ajouter des utilisateurs
    User *u1 = malloc(sizeof(User));
    u1->user_id = 1;
    strcpy(u1->name, "Alice");
    dsm_hash_safe_add(&users, &u1->node, u1->user_id);
    
    User *u2 = malloc(sizeof(User));
    u2->user_id = 2;
    strcpy(u2->name, "Bob");
    dsm_hash_safe_add(&users, &u2->node, u2->user_id);
    
    // Rechercher
    User *found = NULL;
    User *obj;
    dsm_hash_safe_get(&users, obj, node, 1) {
        if (obj->user_id == 1) {
            found = obj;
            break;
        }
    }
    dsm_hash_safe_get_end(&users, 1);
    
    if (found) {
        printf("Utilisateur trouvé : %s\n", found->name);
    }
    
    // Itérer sur tous
    unsigned int bkt;
    User *user;
    printf("Tous les utilisateurs :\n");
    dsm_hash_safe_for_each(&users, bkt, user, node) {
        printf("  - ID %d : %s\n", user->user_id, user->name);
    }
    
    // Nettoyer
    struct dsm_hlist_node *tmp;
    dsm_hash_safe_for_each_safe(&users, bkt, tmp, user, node) {
        dsm_hlist_del_init(&user->node);
        free(user);
    }
    
    dsm_safe_map_destroy(&users);
    return 0;
}
```

---

## Pièges courants

| ⚠️ Piège | 🔧 Solution |
|---------|-----------|
| Oublier `dsm_hash_safe_get_end()` | Toujours l'appeler, même sans résultat |
| Utiliser `dsm_hash_safe_del()` dans `for_each_safe` | Utiliser `dsm_hlist_del_init()` directement |
| Détruire la map avant de vider les éléments | Vider avec `for_each_safe` puis `destroy` |
| Faire `return` dans la boucle `get` | Faire `break` et sortir après `get_end` |
| Ajouter/supprimer des éléments dans `for_each` | Utiliser `for_each_safe` avec wrlock |

---

## Macros Helper

### Accéder à un macro interne (rarement utile)

```c
// Nombre de buckets
unsigned int nbuckets = __DSM_SAFE_SIZE(&map);

// Bucket pour une clé
unsigned int bucket = __DSM_SAFE_BKT(&map, key);

// Tête du bucket
struct dsm_hlist_head *head = __DSM_SAFE_HEAD(&map, bucket);

// Verrou du bucket
pthread_rwlock_t *lock = __DSM_SAFE_LOCK(&map, bucket);
```

### Déblocage manuel (rare)

```c
// Si vous avez besoin de sortir d'une boucle sans break
dsm_hash_safe_unlock(&map, bucket);
```

---

## Performances

- **Insertion/Suppression** : O(1) moyenne avec nbits bien choisi
- **Recherche** : O(1) moyenne
- **Itération complète** : O(n) où n = nombre d'éléments
- **Contentions** : Minimisées par rwlock (plusieurs lecteurs possibles)

**Recommandations pour nbits :**
- Peu d'éléments (<100) : `bits = 5-7`
- Moyennement (100-10k) : `bits = 8-10`
- Beaucoup (>10k) : `bits = 12-16`

