#ifndef DSM_PAGER_H
#define DSM_PAGER_H

#include <unistd.h>
#include "util/hashmap.h"
#include <linux/userfaultfd.h>
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
    PAGE_NONE // cas ou le worker posséde pas la page 
} dsm_page_mode; 

typedef struct {
    unsigned int id; // key : page_id
    unsigned long addr; // l'adresse de la page localement
    struct dsm_hlist_node node; // "node" pour la liste de la hashmap
    dsm_page_mode mode; // mode de detention de la page è
} dsm_page;

// le manager doit avoir une table de hash avec l'id de la page et les infos (struct dsm_page)
__u64 range_start; 

#define PAGE_TABLE dsm_ctx->pages_reg

int pager_init();
void thread_pager(int*); 
void pager_destroy(void);

int handler_pagefault_worker(struct uffd_msg *fault_msg, int fd); 
int handler_REMOVE(); 
int handler_UNMAP(); 


// conversion addresse to pageID -> par rapport au index des pages (à gamberge)


// NETWORK : 
// int send_read_request(int page_id); 
// int send_write_request(int page_id); 
// int send_invalidate(int page_id); 
// int send_page(int page_id, int worker_id); 
// int recv_page(int page_id, void* data); 
// int recv_invalidate(int page_id); 


#endif // DSM_PAGER_H