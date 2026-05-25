#ifndef DSM1SAR_H
#define DSM1SAR_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>

// API Publique (seul fichier inclus par l'utilisateur)

/* --- Memoire --- */
typedef struct {
    unsigned int nb_pages;  // Nombre de pages
    unsigned int page_size; // Taille d'une page en octets
} dsm_pager_config;

/* --- Réseau --- */
typedef struct {
    unsigned short system_id;
    unsigned int nb_managers;
    unsigned int nb_nodes;
    char multicast_ip[INET_ADDRSTRLEN];
    unsigned short multicast_port;
} dsm_system_config;

typedef enum {
    DSM_ROLE_UNDEFINED,
    DSM_ROLE_WORKER,
    DSM_ROLE_MANAGER
} dsm_node_role;

/* Les ports concernent uniquement les ports de réception*/
typedef struct {
    dsm_node_role role;
    unsigned short tcp_port;
    unsigned short udp_port;
    unsigned long pool_nb_threads; // Pour les pools de threads, local au noeud
    unsigned long pool_queue_size; // Pour les pools de threads, local au noeud
} dsm_node_config;

typedef struct {
    dsm_system_config system;
    dsm_node_config node;
} dsm_net_config;

/* --- Configuration globale --- */
typedef struct {
    dsm_pager_config pager;
    dsm_net_config net;
} dsm_config;

/* --- Erreurs --- */
typedef enum {
    DSM_SUCCESS = 0, // Eviter de retourner 0 pour les erreurs, réservé pour succès
    /* Init & Context */
    DSM_ERROR_INIT_ARG_NB_PAGES,
    DSM_ERROR_INIT_ARG_PAGE_SIZE,
    DSM_ERROR_INIT_ARG_NB_NODES,
    DSM_ERROR_INIT_ARG_NB_MANAGERS,
    DSM_ERROR_INIT_ARG_POOL_NB_THREADS,
    DSM_ERROR_INIT_ARG_POOL_QUEUE_SIZE,
    DSM_ERROR_ALREADY_INIT,
    DSM_ERROR_CONTEXT_ALLOC,

    /* Discover */
    DSM_ERROR_SYSTEM_FULL

    /* ... */
} dsm_exit_code;

/* --- Fonctions --- */
/**
 * @brief Obtient la configuration par défaut du système
 * 
 * Initialise une structure de configuration avec des valeurs par défaut
 * raisonnables. Ces valeurs peuvent être modifiées avant l'appel à dsm_init().
 * 
 * @param[out] config Pointeur vers la structure de configuration à initialiser
 * 
 * @return Void
 * 
 * @note Cette fonction doit être appelée avant dsm_init() pour initialiser
 * les valeurs par défaut.
 * 
 * @see dsm_init, dsm_config
 */
void dsm_get_default_config(dsm_config *config);


 /**
 * @brief Initialise le système DSM et détermine le rôle du nœud
 * 
 * Initialise le contexte global DSM selon la configuration fournie.
 * Cette fonction effectue :
 * - Validation de la configuration
 * - Allocation des structures internes
 * - Découverte des autres nœuds (multicast)
 * - Attribution du rôle (manager ou worker)
 * - Mise en place de la communication
 * 
 * @param[in] config Configuration DSM validée
 * @param[out] out_node_id Identifiant unique attribué au nœud courant
 * 
 * @return Le rôle du nœud : DSM_ROLE_WORKER ou DSM_ROLE_MANAGER
 * @retval DSM_ROLE_WORKER Le nœud a été assigné comme worker
 * @retval DSM_ROLE_MANAGER Le nœud a été assigné comme manager
 * @retval -1 En cas d'erreur (équivalent à DSM_ROLE_UNDEFINED)
 * 
 * @warning
 * - DSM_ROLE_UNDEFINED ne peut JAMAIS être retourné par cette fonction.
 * - Cette fonction doit être appelée une seule fois par processus.
 * - Les allocations mémoire doivent être faites APRÈS dsm_init() pour assurer
 *   la cohérence entre les nœuds.
 * 
 * @note L'identifiant de nœud est unique au sein du système.
 * 
 * @see dsm_get_default_config, dsm_config, dsm_node_role
 */
dsm_node_role dsm_init(const dsm_config *config, unsigned int *out_node_id);


 /**
 * @brief Alloue un segment de mémoire dans l'espace d'adressage global partagé
 * 
 * Alloue une nouvelle page de mémoire partagée accessible par tous les nœuds.
 * Cette allocation est créée à la même adresse virtuelle sur tous les nœuds
 * pour faciliter la programmation distribuée.
 * 
 * @param[in] len Taille de l'allocation en octets
 * 
 * @return Pointeur vers la zone allouée avec adresse identique sur tous les nœuds,
 *         ou NULL en cas d'erreur (quota atteint, taille invalide, etc.)
 * 
 * @warning
 * - Cette fonction doit être appelée de manière **strictement déterministe** 
 *   AVANT toute divergence du flux d'exécution (fork, création de threads, 
 *   branches conditionnelles basées sur l'ID du nœud).
 * - Pour garantir la cohérence, chaque nœud doit effectuer les mêmes appels 
 *   d'allocation dans le même ordre.
 * - Ne pas faire d'allocation conditionnelle selon le rôle/ID du nœud.
 * 
 * @note 
 * - La taille est ignorée : chaque allocation crée exactement une page.
 * - La mémoire est initialisée à zéro.
 * 
 * @see dsm_lock, dsm_unlock
 */
void *dsm_alloc(void);

/**
 * @brief Envoie une requête de LOCK exclusif sur la page contenant l'adresse
 * 
 * Acquiert un verrou exclusif sur la page contenant l'adresse fournie.
 * Le thread bloque jusqu'à l'acquisition du verrou. Une fois acquis,
 * aucun autre nœud ne peut accéder à cette page.
 * 
 * @param[in] addr Adresse quelconque dans la page à verrouiller
 * 
 * @return Code de retour
 * @retval 0 Succès : le verrou a été acquis
 * @retval -1 Erreur (adresse invalide, page non partagée, etc.)
 * 
 * @warning
 * - Le verrou est par page, pas par adresse. Tout accès à la même page 
 *   sera exclusif.
 * 
 * @note Le verrou est libéré par un appel correspondant à dsm_unlock().
 * 
 * @see dsm_unlock, dsm_alloc
 */
int dsm_lock(void *addr);

/**
 * @brief Envoie une requête de UNLOCK sur la page contenant l'adresse
 * 
 * Libère un verrou exclusif précédemment acquis sur la page contenant 
 * l'adresse fournie. Permet aux autres nœuds d'accéder à la page.
 * 
 * @param[in] addr Adresse quelconque dans la page à déverrouiller
 * 
 * @return Code de retour
 * @retval 0 Succès : le verrou a été libéré
 * @retval -1 Erreur (adresse invalide, verrou non détenu, etc.)
 * 
 * @warning
 * - Le verrou doit avoir été préalablement acquis par le même nœud.
 * - Libérer un verrou non détenu est une erreur.
 * - Libérer deux fois le même verrou provoque un comportement indéfini.
 * 
 * @note Doit toujours être appelé en paire avec dsm_lock().
 * 
 * @see dsm_lock, dsm_alloc
 */
int dsm_unlock(void *addr);

/**
 * @brief Bloque le thread en attendant l'arrivée de tous les nœuds à la barrière
 * 
 * Barrière de synchronisation simple : le thread bloque jusqu'à ce que 
 * TOUS les nœuds du système aient appelé cette fonction.
 * 
 * @return Code de retour
 * @retval 0 Succès : tous les nœuds ont atteint la barrière
 * @retval -1 Erreur (timeout, communication échouée, etc.)
 * 
 * @warning
 * - Tous les nœuds DOIVENT appeler cette fonction (sinon deadlock).
 * - Tous les threads d'un nœud doivent être synchronisés AVANT l'appel 
 *   (utiliser dsm_barrier_advanced() pour plus de contrôle).
 * 
 * @note Équivalent à dsm_barrier_advanced() avec un unique thread par nœud.
 * 
 * @see dsm_barrier_advanced
 */
int dsm_barrier(void);

/**
 * @brief Barrière avancée avec synchronisation intra-nœud
 * 
 * Barrière de synchronisation multi-niveaux : d'abord synchronise tous les 
 * threads locaux du nœud, puis synchronise tous les nœuds du système.
 * Un manager spécifique coordonne la barrière.
 * 
 * @param[in] threads_count Nombre de threads locaux du nœud qui vont appeler cette fonction
 * @param[in] manager_id Identifiant du manager coordinateur de la barrière
 * 
 * @return Code de retour
 * @retval 0 Succès : tous les threads et nœuds ont atteint la barrière
 * @retval -1 Erreur (coordinator non disponible, timeout, etc.)
 * 
 * @warning
 * - Exactement `threads_count` threads de chaque nœud DOIVENT appeler cette fonction.
 * - Le manager identifié doit être disponible et en cours d'exécution.
 * - Si le nombre réel de threads ne correspond pas à threads_count, deadlock garanti.
 * 
 * @note 
 * - Plus flexible que dsm_barrier() pour les architectures multi-threads.
 * - Permet de synchroniser un sous-ensemble de threads.
 * 
 * @see dsm_barrier
 */
int dsm_barrier_advanced(unsigned int threads_count, unsigned int manager_id);

/**
 * @brief Gère la finalisation complète de la DSM
 * 
 * Finalise le système DSM et libère toutes les ressources allouées.
 * Effectue :
 * - Synchronisation finale des nœuds
 * - Fermeture des connexions réseau
 * - Libération de la mémoire partagée
 * - Arrêt des threads internes
 * 
 * @return Code de retour
 * @retval 0 Succès : finalisation complétée
 * @retval -1 Erreur pendant la finalisation
 * 
 * @warning
 * - TOUS les nœuds doivent appeler cette fonction (sinon certains nœuds 
 *   resteront bloqués).
 * - À appeler une seule fois, en dernier recours du programme.
 * - Aucune opération DSM ne peut être effectuée après cet appel.
 * - Idéalement, tous les threads doivent être terminés avant cet appel.
 * 
 * @note Équivalent à dsm_finalize_advanced(1).
 * 
 * @see dsm_finalize_advanced
 */
int dsm_finalize(void);

/**
 * @brief Finalisation avancée avec synchronisation multi-thread
 * 
 * Finalise le système DSM en tenant compte de plusieurs threads locaux.
 * Effectue une barrière finale avec synchronisation de threads_count threads
 * sur le nœud courant.
 * 
 * @param[in] threads_count Nombre de threads locaux participants à la finalisation
 * 
 * @return Code de retour
 * @retval 0 Succès : finalisation complétée
 * @retval -1 Erreur pendant la finalisation
 * 
 * @warning
 * - Exactement `threads_count` threads doivent appeler cette fonction.
 * - Ne pas faire diverger le nombre de threads et l'argument threads_count.
 * - TOUS les nœuds doivent appeler dsm_finalize() ou dsm_finalize_advanced().
 * 
 * @note Utile pour les applications multi-threads où chaque thread 
 *       doit participer à la finalisation.
 * 
 * @see dsm_finalize
 */
int dsm_finalize_advanced(unsigned int threads_count);

/* Fonction de Tests */
/**
 * @brief Obtient le nombre de faults userfaultfd depuis le dernier reset
 * 
 * Retourne le nombre de page faults capturés par le mécanisme userfaultfd.
 * Ces faults correspondent aux accès à des pages non localement présentes.
 * 
 * @param[out] out Pointeur vers la variable où stocker le résultat
 * 
 * @return Booléen d'état
 * @retval true Succès : compteur récupéré
 * @retval false Erreur (pointeur invalide, DSM non initialisée, etc.)
 * 
 * @note Ce compteur aide à évaluer la localité des accès mémoire.
 * 
 * @see dsm_reset_uffd_count, dsm_get_data_count, dsm_get_inva_count
 */
bool dsm_get_uffd_count(unsigned long *out);

/**
 * @brief Obtient le nombre de messages DATA (transferts de données) envoyés
 * 
 * Retourne le nombre total de messages de transfert de données 
 * (invalidations ou mises à jour) échangés entre nœuds.
 * 
 * @param[out] out Pointeur vers la variable où stocker le résultat
 * 
 * @return Booléen d'état
 * @retval true Succès : compteur récupéré
 * @retval false Erreur (pointeur invalide, DSM non initialisée, etc.)
 * 
 * @note Utile pour profiler les coûts de communication.
 * 
 * @see dsm_reset_data_count, dsm_get_uffd_count, dsm_get_inva_count
 */
bool dsm_get_data_count(unsigned long *out);

/**
 * @brief Obtient le nombre de messages d'INVALIDATION envoyés
 * 
 * Retourne le nombre total de messages d'invalidation de pages
 * échangés dans le système pour maintenir la cohérence mémoire.
 * 
 * @param[out] out Pointeur vers la variable où stocker le résultat
 * 
 * @return Booléen d'état
 * @retval true Succès : compteur récupéré
 * @retval false Erreur (pointeur invalide, DSM non initialisée, etc.)
 * 
 * @note Un nombre élevé peut indiquer un problème de localité.
 * 
 * @see dsm_reset_inva_count, dsm_get_uffd_count, dsm_get_data_count
 */
bool dsm_get_inva_count(unsigned long *out);

bool dsm_reset_uffd_count();
bool dsm_reset_data_count();
bool dsm_reset_inva_count();
int validate_config(const dsm_config *config);

#endif // DSM1SAR_H