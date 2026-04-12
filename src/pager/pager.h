#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <unistd.h>

/**
 * La taille de la page est différente d'un système à un autre
 */
#define DSM_DEFAULT_PAGESIZE sysconf(_SC_PAGESIZE)

#define DSM_DEFAULT_NB_PAGES 16384

// structure de données :
// hashmap de pages et leur id

struct page {

};

int pager_init(ssize_t);
void pager_destroy(void);
int handler_PAGEFAULT(); 
int handler_REMOVE(); 
int handler_UNMAP(); 





// NETWORK : 
// int send_read_request(int page_id); 
// int send_write_request(int page_id); 
// int send_invalidate(int page_id); 
// int send_page(int page_id, int worker_id); 
// int recv_page(int page_id, void* data); 
// int recv_invalidate(int page_id); 


#endif // DSM_PAGER_H