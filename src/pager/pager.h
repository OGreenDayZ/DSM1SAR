#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <unistd.h>

/** 
 * La taille de la page est différente d'un système à un autre
 */
#define PAGESIZE  sysconf(_SC_PAGESIZE)

#define DSM_NB_PAGES 16384

struct page {

}; 

int dsm_init_pager(ssize_t); 

// structure de données : 
// hashmap de pages et leur id 


#endif // DSM_PAGER_H