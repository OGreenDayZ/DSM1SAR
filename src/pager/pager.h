#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <unistd.h>
#include "util/hashmap.h"
#include <linux/userfaultfd.h>
#include "internal.h"
/**
 * La taille de la page est différente d'un système à un autre
 */
#define DSM_DEFAULT_PAGESIZE sysconf(_SC_PAGESIZE)
#define DSM_DEFAULT_NB_PAGES 16384

#define EFFECTIVE_PAGESIZE dsm_ctx->config.pager.nb_pages
#define EFFECTIVE_NB_PAGES dsm_ctx->config.pager.page_size
// structure de données :
// hashmap de pages et leur id

typedef enum{
    PAGE_R, 
    PAGE_RW, 
    PAGE_NONE // cas ou le worker posséde pas la page - apres un madvise ou un remove
} dsm_page_mode; 

typedef struct {
    unsigned int id; // key : page_id
    unsigned long addr; // l'adresse de la page localement
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
    dsm_page_mode mode; // mode de detention de la page 
    int owner_id; 
} dsm_page;

// le manager doit avoir une table de hash avec l'id de la page et les infos (struct dsm_page)
__u64 range_start; 

int userfaultfd_init() ; 
int pager_worker_loop(); 

// conversion addresse to pageID -> par rapport au index des pages (à gamberge)
// la map des pages est indexé par rapport au worker
#define PAGES_MAP dsm_ctx->pages_reg


// NETWORK : 
// int send_read_request(int page_id); 
// int send_write_request(int page_id); 
// int send_invalidate(int page_id); 
// int send_page(int page_id, int worker_id); 
// int recv_page(int page_id, void* data); 
// int recv_invalidate(int page_id); 


#endif // DSM_PAGER_H