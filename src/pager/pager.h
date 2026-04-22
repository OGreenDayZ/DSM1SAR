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
// la map des pages est indexé par rapport au worker
#define PAGES_MAP dsm_ctx->worker_ctx.pages_reg
#define RANGE_START dsm_ctx->worker_ctx.range_start
typedef enum{
    PAGE_R, 
    PAGE_RW, 
    PAGE_NONE // cas ou le worker posséde pas la page - apres un madvise ou un remove
} dsm_page_mode_poss; // mode de détention de la page chez le worker

typedef enum {
    READ_REQ, 
    WRITE_REQ
} dsm_page_mode_req; // mode de requete de la page par le worker (lecture ou écriture)
typedef struct {
    unsigned int id; // key : page_id
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
    dsm_page_mode_poss mode; // mode de detention de la page 
    int owner_id; 
} dsm_page;

// le manager doit avoir une table de hash avec l'id de la page et les infos (struct dsm_page)

int userfaultfd_init() ; 
int pager_worker_loop(); 
int pager_writeprotect(int fd, __u64 page); 
int pager_copy(int fd, __u64 dst, __u64 src, int mode);
// conversion addresse to pageID -> par rapport au index des pages (à gamberge)

// NETWORK : 
// int send_read_request(int page_id); 
// int send_write_request(int page_id); 
// int send_invalidate(int page_id); 
// int send_page(int page_id, int worker_id); 
// int recv_page(int page_id, void* data); 
// int recv_invalidate(int page_id); 


#endif // DSM_PAGER_H