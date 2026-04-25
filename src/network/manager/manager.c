#include "manager.h"
#include "debug/debug.h"

dsm_manager_ctx *dsm_mgr_ctx = NULL;

int dsm_mgr_ctx_init() {
    LOG_INFO("Initialisation du contexte du manager ...");

    if (dsm_mgr_ctx) {
        LOG_ERROR("dsm_mgr_ctx déjà initialisé ?!");
        return -1;
    }

    dsm_mgr_ctx = malloc(sizeof(dsm_manager_ctx));
    if (!dsm_mgr_ctx) {
        LOG_ERROR("malloc : dsm_mgr_ctx");
        return -1;
    }

    if (dsm_safe_map_init(&dsm_mgr_ctx->pages_reg, DSM_MGR_PAGE_HASH_BITS) < 0) {
        return -1;
    }

    return 0;
}

void dsm_mgr_ctx_cleanup();

static int treat_wkr_req_write() {
    // dsm_page *page = NULL;

    // dsm_hash_safe_get(&MANAGER_PAGES_MAP, page, node, payload->page_id) {
    //     if (page->id == payload->page_id)
    //         break;
    // }
    // dsm_hash_safe_get_end(&MANAGER_PAGES_MAP, payload->page_id);

    // if (page != NULL) {
    //     /*
    //      * La page a déjà un propriétaire : demander les données au propriétaire,
    //      * qui devra invalider sa copie locale.
    //      */
    //     LOG_INFO("Page %u : demande de données + transfert de propriétaire",
    //              payload->page_id);
    //     // TODO majda : revoir avec la bonne signature adapté au nouvelle structure
    //     return send_mgr_data_req(payload->page_id, true);
    // }

    // /* Première écriture sur cette page : l'enregistrer */
    // page = malloc(sizeof(dsm_page));
    // if (!page) {
    //     LOG_ERROR("malloc dsm_page");
    //     return -1;
    // }
    // page->id = payload->page_id;
    // page->owner_id = -1;
    // dsm_init_hlist_node(&page->node);
    // dsm_hash_safe_add(&MANAGER_PAGES_MAP, &page->node, page->id);
    // dsm_safe_list_init(&page->readers);

    // /* TODO (majda) : ne pas oublier de free worker_info */
    // reader_info *reader = malloc(sizeof(reader_info));
    // if (!reader) {
    //     LOG_ERROR("malloc worker_info");
    //     return -1;
    // }
    // dsm_safe_list_add(&page->readers, &reader->node);
    return 0;
}

/**
 * TODO (MAJDA) ?
 */
void dispatch_call_mgr(void *arg) {
    dsm_msg_tcp_thread_ctx *msg_ctx = (dsm_msg_tcp_thread_ctx *)arg;

    // Ancien taff, ->suivre le mm pattern que dans dispatch_call_wkr
    // if (code == CODE_WKR_REQ) {
    //     /* Recpetion d'une requete de la part d'un worker */
    //     dsm_msg_wkr_req payload;
    //     if (dsm_dsrlze_wkr_req(pbuf, buf_size, &payload) < 0)
    //         return -1;

    //     switch (payload.mode) {
    //         case READ_REQ:
    //             /* TODO : reception d'une requete de lecture */
    //             break;
    //         case WRITE_REQ:
    //             /*  reception d'une requete d'écriture */
    //             return treat_wkr_req_write(fd, &payload);
    //         case LOCK_REQ:
    //             /* TODO : reception d'une requete de lock */
    //             break;
    //         case UNLOCK_REQ:
    //             /* TODO : reception d'une requete d'unlock   */
    //             break;
    //     }

    // } else if (code == CODE_WKR_DATA_ACK) {
    //     // TODO - majda : récrire avec les bonnes structures
    //     /* Recpetion des données de la part d'un worker */
    //     // dsm_msg_data_ack payload;
    //     // if (dsm_dsrlze_data_ack(pbuf, buf_size, &payload) < 0)
    //     //     return -1;

    //     // return send_mgr_ack(payload.page_id, WRITE_REQ, MGR_ACK_DATA, payload.data);
    // }

    free(msg_ctx->payload);
    free(msg_ctx);
}