#include "pager_msg.h"
#include "debug/debug.h"
#include "internal.h"
#include "network/message/message.h"
#include "network/message/serializer.h"

int send_wkr_req(uint8_t page_id, uint8_t mode) {
    LOG_INFO("Envoi du CODE=%d ...", CODE_PAGER_REQUEST);
    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len =
        dsm_srlze_pgr_wkr_req(page_id, dsm_ctx->node_id, mode, msg_buf, sizeof(msg_buf));
    if (msg_len < 0)
        return -1;
    // envoie du message au manager qui gere la page

    return 0;
}

int recv_wkr_req() {
    return 0;
}
