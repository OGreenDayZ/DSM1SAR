#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/poll.h>

#include "debug/debug.h"
#include "network/message/message.h"
#include "network/message/serializer.h"
#include "pager/pager.h"
#include "pager/pager_msg.h"
#include "util/concurrent/safe_list.h"
#include "util/concurrent/safe_map.h"

static int recv_header(int fd, dsm_msg_header *out) {
    uint8_t buf[sizeof(dsm_msg_header)];
    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};

    if (dsm_tcp_recv_all(fd, buf, sizeof(buf), &timeout) < 0)
        return -1;

    return dsm_dsrlze_header(buf, sizeof(buf), out);
}

static int recv_payload(int fd, size_t len, uint8_t *out) {
    struct timeval timeout = {.tv_sec = DSM_MSG_RECV_TIMEOUT_SEC, .tv_usec = 0};
    return dsm_tcp_recv_all(fd, out, len, &timeout);
}

/* =========================================================================
 * Worker : traitement des événements
 * ========================================================================= */

static int handle_pagefault_worker(struct uffd_msg *fault_msg) {
    __u64 page = fault_msg->arg.pagefault.address;
    int page_index = (page - RANGE_START) / EFFECTIVE_PAGESIZE;
    int flags = fault_msg->arg.pagefault.flags;

    if (flags & UFFD_PAGEFAULT_FLAG_WP) {
        /*
         * Accès en écriture sur une page présente et protégée en écriture :
         * prévenir le manager pour qu'il invalide la page chez les autres workers,
         * puis retirer la protection d'écriture localement.
         */
        return send_wkr_req(page_index, WRITE_REQ);
    }

    if (flags & UFFD_PAGEFAULT_FLAG_WRITE) {
        /*
         * Accés en écriture sur une page non présente :
         */
        return send_wkr_req(page_index, WRITE_REQ);
    }

    /*
     * Accés a une page non présente en lecture :
     */
    return send_wkr_req(page_index, READ_REQ);
}

static int handle_uffd_event(void) {
    struct uffd_msg fault_msg = {0};

    if (read(dsm_pager_uffd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
        LOG_ERROR("readdsm_pager_uffd");
        return -1;
    }

    switch (fault_msg.event) {
        case UFFD_EVENT_PAGEFAULT:
            /* TODO (majda) : exécuter dans un thread dédié */
            return handle_pagefault_worker(&fault_msg);

        case UFFD_EVENT_REMOVE:
            /* TODO : traiter l'invalidation de page */
            // Peut être pas necessaire a voir
            break;

        case UFFD_EVENT_UNMAP:
            /* TODO : traiter l'unmap */
            // Peut être pas necessaire a voir
            break;
    }
    return 0;
}
// TODO - majda : refaire avec les bonnes structures et code

// static int handle_network_message_worker(int fd, dsm_msg_header *header) {
//     int code = header->code;
//     size_t buf_size = header->payload_len;
//     uint8_t pbuf[buf_size];

//     if (recv_payload(fd, buf_size, pbuf) < 0)
//         return -1;

//     if (code == CODE_PAGER_MANAGER_ACK) {
//         dsm_msg_pgr_mgr_ack payload;
//         if (dsm_dsrlze_pgr_mgr_ack(pbuf, buf_size, &payload) < 0)
//             return -1;

//         if (payload.flags & MGR_ACK_DATA) {
//             /* Données reçues avec l'ACK */
//             if (payload.mode == READ_REQ) {
//                 /* TODO : reception d'une reponse a une requete en lecture */
//             } else if (payload.mode == WRITE_REQ) {
//                 /* mode = 0 : on ne reprotège pas en écriture ici */
//                 pager_copy(payload.page_id, (__u64)payload.data, 0);
//             }
//         } else {
//             /* Pas de données */
//             if (payload.mode == READ_REQ) {
//                 // initialiser page avec des 0
//                 void *data = malloc(EFFECTIVE_PAGESIZE);
//                 memset(data, 0, EFFECTIVE_PAGESIZE);
//                 pager_copy(payload.page_id, (__u64)data, 0);
//                 free(data);
//             } else if (payload.mode == WRITE_REQ) {
//                 pager_writeprotect(payload.page_id);
//             }
//         }

//         if (payload.flags & MGR_INVALIDATE) {
//             /* TODO : invalider la page */
//         }

//         if (payload.flags & PGR_SYNC) {
//             /* TODO : répondre au manager */
//         }

//     } else if (code == CODE_MGR_DATA_REQ) {
//         dsm_msg_mgr_data_req payload;
//         if (dsm_dsrlze_mgr_data_req(pbuf, buf_size, &payload) < 0)
//             return -1;

//         uint8_t *data = (uint8_t *)(RANGE_START + payload.page_id *
//         EFFECTIVE_PAGESIZE);

//         if (payload.isInvalidate) {
//             madvise(data, EFFECTIVE_PAGESIZE, MADV_DONTNEED);
//             LOG_INFO("Page %u invalidée", payload.page_id);
//         }

//         return send_pgr_data(payload.page_id, data);
//     }

//     return 0;
// }

/* =========================================================================
 * Boucle principale worker
 * ========================================================================= */
int pager_worker_loop(void) {
    // poll_set ps;
    int ret = 0;
    int i = 1;
    int sock_count = dsm_ctx->config.net.system.nb_managers + 1;
    struct pollfd *pollfds;

    if (userfaultfd_init() == -1) {
        LOG_ERROR("userfaultfd_init");
        return -1;
    }

    pollfds = malloc(sock_count * sizeof(struct pollfd));
    if (!pollfds) {
        LOG_ERROR("malloc poolfds : pager_worker_loop");
        return -1;
    }

    pollfds[0] = (struct pollfd){.fd = dsm_pager_uffd, .events = POLLIN};

    unsigned int bkt;
    dsm_node_content *pos;
    struct dsm_hlist_node *tmp;

    dsm_hash_safe_for_each_safe(&dsm_ctx->nodes_reg, bkt, tmp, pos, node) {
        pollfds[i] = (struct pollfd){.fd = dsm_pager_uffd, .events = POLLIN};
        i++;
    }

    while (1) {
        int n = poll(pollfds, sock_count, DSM_MSG_TIMEOUT_POLL);

        if (n == 0)
            continue; /* timeout, on réessaie */

        if (n < 0) {
            LOG_ERROR("poll");
            ret = -1;
            goto cleanup;
        }

        for (int i = 0; i < sock_count; i++) {
            short revents = pollfds[i].revents;

            if (!(revents & (POLLIN | POLLERR)))
                continue;

            if (revents & POLLERR) {
                LOG_ERROR("POLLERR sur fd=%d", ps.pollfds[i].fd);
                ret = -1;
                goto cleanup;
            }

            if (pollfds[i].fd == dsm_pager_uffd)
                ret = handle_uffd_event();
            else {
                dsm_msg_header header;

                if (recv_header(pollfds[i].fd, &header) < 0)
                    return -1;
                // TODO majda : revoir
                // return handle_network_message_worker(pollfds[i].fd, &header);
            }
            if (ret < 0)
                goto cleanup;
        }
    }

cleanup:
    if (pollfds)
        free(pollfds);
    /* TODO (majda) : a bouger d'ici et a faire dans pager_destroy*/
    close(dsm_pager_uffd);
    return ret;
}

/* =========================================================================
 * Manager : traitement des messages réseau
 * ========================================================================= */
static int handle_write_request_manager(int node_id, dsm_msg_wkr_req *payload) {
    dsm_page *page = NULL;

    dsm_hash_safe_get(&MAP_MANAGER, page, node, payload->page_id) {
        if (page->id == payload->page_id)
            break;
    }
    dsm_hash_safe_get_end(&MAP_MANAGER, payload->page_id);

    if (page != NULL) {
        /*
         * La page a déjà un propriétaire : demander les données au propriétaire,
         * qui devra invalider sa copie locale.
         */
        LOG_INFO("Page %u : demande de données + transfert de propriétaire",
                 payload->page_id);
        // TODO majda : revoir avec la bonne signature adapté au nouvelle structure
        return send_mgr_data_req(payload->page_id, true);
    }

    /* Première écriture sur cette page : l'enregistrer */
    page = malloc(sizeof(dsm_page));
    if (!page) {
        LOG_ERROR("malloc dsm_page");
        return -1;
    }
    page->id = payload->page_id;
    page->owner_id = -1;
    dsm_init_hlist_node(&page->node);
    dsm_hash_safe_add(&MAP_MANAGER, &page->node, page->id);
    dsm_safe_list_init(&page->readers);

    /* TODO (majda) : ne pas oublier de free worker_info */
    reader_info *reader = malloc(sizeof(reader_info));
    if (!reader) {
        LOG_ERROR("malloc worker_info");
        return -1;
    }
    dsm_safe_list_add(&page->readers, &reader->node);
    return 0;
}

static int handle_network_message_manager(int fd, dsm_msg_header *header) {
    int code = header->code;
    size_t buf_size = header->payload_len;
    uint8_t pbuf[buf_size];

    if (recv_payload(fd, buf_size, pbuf) < 0)
        return -1;

    if (code == CODE_WKR_REQ) {
        /* Recpetion d'une requete de la part d'un worker */
        dsm_msg_wkr_req payload;
        if (dsm_dsrlze_wkr_req(pbuf, buf_size, &payload) < 0)
            return -1;

        switch (payload.mode) {
            case READ_REQ:
                /* TODO : reception d'une requete de lecture */
                break;
            case WRITE_REQ:
                /*  reception d'une requete d'écriture */
                return handle_write_request_manager(fd, &payload);
            case LOCK_REQ:
                /* TODO : reception d'une requete de lock */
                break;
            case UNLOCK_REQ:
                /* TODO : reception d'une requete d'unlock   */
                break;
        }

    } else if (code == CODE_WKR_DATA_ACK) {
        // TODO - majda : récrire avec les bonnes structures
        /* Recpetion des données de la part d'un worker */
        // dsm_msg_data_ack payload;
        // if (dsm_dsrlze_data_ack(pbuf, buf_size, &payload) < 0)
        //     return -1;

        // return send_mgr_ack(payload.page_id, WRITE_REQ, MGR_ACK_DATA, payload.data);
    }

    return 0;
}

/* =========================================================================
 * Boucle principale manager
 * ========================================================================= */

int pager_manager_loop(void) {
    int ret = 0;
    int i = 0;
    int sock_count =
        dsm_ctx->config.net.system.nb_nodes - dsm_ctx->config.net.system.nb_managers;
    struct pollfd *pollfds;

    pollfds = malloc(sock_count * sizeof(struct pollfd));
    if (!pollfds) {
        LOG_ERROR("malloc poolfds : pager_worker_loop");
        return -1;
    }
    unsigned int bkt;
    dsm_node_content *pos;
    struct dsm_hlist_node *tmp;

    dsm_hash_safe_for_each_safe(&dsm_ctx->nodes_reg, bkt, tmp, pos, node) {
        pollfds[i] = (struct pollfd){.fd = dsm_pager_uffd, .events = POLLIN};
        i++;
    }

    while (1) {
        int n = poll(pollfds, sock_count, DSM_MSG_TIMEOUT_POLL);

        if (n == 0)
            continue;

        if (n < 0) {
            LOG_ERROR("poll");
            ret = -1;
            goto cleanup;
        }

        for (int i = 0; i < sock_count; i++) {
            short revents = pollfds[i].revents;

            if (!(revents & (POLLIN | POLLERR)))
                continue;

            if (revents & POLLERR) {
                LOG_ERROR("POLLERR sur fd=%d", ps.pollfds[i].fd);
                ret = -1;
                goto cleanup;
            }

            dsm_msg_header header;
            if (recv_header(pollfds[i].fd, &header) < 0)
                return -1;

            ret = handle_network_message_manager(pollfds[i].fd, &header);
            if (ret < 0)
                goto cleanup;
        }
    }

cleanup:
    if (pollfds)
        free(pollfds);
    return ret;
}
