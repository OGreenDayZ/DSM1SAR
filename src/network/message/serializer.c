#include "serializer.h"

#include <string.h>

#include "debug/debug.h"
#include "internal.h"

static int serialize_header(dsm_msg_header *header, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le header : %zu < %zu",
                  buf_sz, sizeof(dsm_msg_header));
        return -1;
    }

    int len = sizeof(dsm_msg_header);
    header->code = htonl(header->code);
    header->id = htonl(header->id);
    header->payload_len = htonl(header->payload_len);
    memcpy(buf, header, len);

    return len;
}

static int serialize_no_payload_msg(uint32_t header_code, uint32_t header_id,
                                    uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le message sans payload "
                  "CODE=%d : %zu < %zu",
                  header_code, buf_sz, sizeof(dsm_msg_header));
        return -1;
    }

    dsm_msg_header header = {
        .code = header_code,
        .id = header_id,
        .payload_len = 0,
    };

    return serialize_header(&header, buf, buf_sz);
}

int dsm_srlze_dscv_init(uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_init)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_INIT, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_init));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_INIT,
        .id = dsm_ctx->config.net.system.system_id,
        .payload_len = sizeof(dsm_msg_dscv_init),
    };

    dsm_msg_dscv_init payload = {
        .udp_port_uni = htons(dsm_ctx->config.net.node.udp_port),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

int dsm_srlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_SUPERVISOR_DATA, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_SUPERVISOR_DATA,
        .id = dsm_ctx->config.net.system.system_id,
        .payload_len = sizeof(dsm_msg_dscv_spvs_data),
    };

    dsm_msg_dscv_spvs_data payload = {
        .tcp_port_spvs = htons(dsm_ctx->config.net.node.tcp_port),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

int dsm_srlze_dscv_join_ack(int assigned_id, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_join_ack)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_JOIN_ACK, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_join_ack));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_JOIN_ACK,
        .id = dsm_ctx->node_id,
        .payload_len = sizeof(dsm_msg_dscv_join_ack),
    };

    dsm_msg_dscv_join_ack payload = {
        .assigned_id = htonl(assigned_id),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

int dsm_srlze_dscv_sys_full(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_SYSTEM_FULL, dsm_ctx->node_id, buf,
                                    buf_sz);
}

int dsm_srlze_dscv_role_wish(uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_role_wish)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_ROLE_WISH, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_role_wish));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_ROLE_WISH,
        .id = dsm_ctx->node_id,
        .payload_len = sizeof(dsm_msg_dscv_role_wish),
    };

    dsm_msg_dscv_role_wish payload = {
        .role = htonl(dsm_ctx->config.net.node.role),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

static int serialize_reg_node(dsm_reg_node *node, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_reg_node)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser un noeud du registre : "
                  "%zu < %zu",
                  buf_sz, sizeof(dsm_reg_node));
        return -1;
    }

    dsm_reg_node net_node = {
        .id = htonl(node->id),
        .ip = htonl(node->ip),
        .manager_port = htons(node->manager_port),
    };
    memcpy(buf, &net_node, sizeof(net_node));

    return sizeof(net_node);
}

int dsm_srlze_dscv_share_reg(uint32_t code, dsm_msg_dscv_share_reg *share, uint8_t *buf,
                             size_t buf_sz) {
    size_t needed_sz = sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg) +
                       share->nb_nodes * sizeof(dsm_reg_node);
    if (buf_sz < needed_sz) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  code, buf_sz, needed_sz);
        return -1;
    }
    dsm_msg_header header = {
        .code = code,
        .id = dsm_ctx->node_id,
        .payload_len =
            sizeof(dsm_msg_dscv_share_reg) + share->nb_nodes * sizeof(dsm_reg_node),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }

    dsm_msg_dscv_share_reg net_share = {
        .sys_total_nodes = htonl(share->sys_total_nodes),
        .nb_nodes = htonl(share->nb_nodes),
    };
    memcpy(buf + offset, &net_share, sizeof(net_share));
    offset += sizeof(dsm_msg_dscv_share_reg);

    for (uint32_t i = 0; i < share->nb_nodes; i++) {
        int res = serialize_reg_node(&share->nodes[i], buf + offset, buf_sz - offset);
        if (res < 0) {
            return -1;
        }
        offset += res;
    }

    return offset;
}

int dsm_srlze_dscv_share_reg_ack(uint16_t listen_port, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg_ack)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg_ack));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK,
        .id = dsm_ctx->node_id,
        .payload_len = sizeof(dsm_msg_dscv_share_reg_ack),
    };

    dsm_msg_dscv_share_reg_ack payload = {
        .listen_port = htons(listen_port),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

int dsm_srlze_dscv_wkr_id(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_WORKER_IDENTITY, dsm_ctx->node_id, buf,
                                    buf_sz);
}

int dsm_srlze_dscv_wkr_id_ack(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_WORKER_IDENTITY_ACK, dsm_ctx->node_id,
                                    buf, buf_sz);
}

int dsm_srlze_dscv_mgr_ready(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_MANAGER_READY, dsm_ctx->node_id, buf,
                                    buf_sz);
}

// dsm_srlze_dscv_mgr_ready
// srlze_dscv_mgr_ready

int dsm_srlze_dscv_wkr_config(uint64_t nb_pages, uint32_t page_size, uint8_t *buf,
                              size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_wkr_config)) {
        LOG_ERROR("Taille du buffer insuffisante pour sérialiser le CODE=%d : %zu < %zu",
                  CODE_DISCOVER_WORKER_CONFIG, buf_sz,
                  sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_wkr_config));
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_DISCOVER_WORKER_CONFIG,
        .id = dsm_ctx->node_id,
        .payload_len = sizeof(dsm_msg_dscv_wkr_config),
    };

    dsm_msg_dscv_wkr_config payload = {
        .nb_pages = htobe64(nb_pages),
        .page_size = htonl(page_size),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

int dsm_srlze_dscv_wkr_config_ack(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_WORKER_CONFIG_ACK, dsm_ctx->node_id,
                                    buf, buf_sz);
}

int dsm_srlze_dscv_finalize(uint8_t *buf, size_t buf_sz) {
    return serialize_no_payload_msg(CODE_DISCOVER_FINALIZE, dsm_ctx->node_id, buf,
                                    buf_sz);
}

// envoie la requete du worker pour une page (lecture ou écriture)
int dsm_srlze_pgr_wkr_req(uint32_t page_id, uint8_t mode, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_pgr_wkr_req)) {
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_PAGER_REQUEST,
        .id = dsm_ctx->node_id, // worker_id qui envoie la requete
        .payload_len = sizeof(dsm_msg_pgr_wkr_req),
    };

    dsm_msg_pgr_wkr_req payload = {
        .page_id = htonl(page_id),
        .mode = mode,
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

// envoie la requete d'invalidation de la page
int dsm_srlze_pgr_mgr_inva(uint32_t page_id, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_pgr_mgr_inva)) {
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_PAGER_INVALIDATE,
        .id = dsm_ctx->node_id, // manager_id qui envoie la requete
        .payload_len = sizeof(dsm_msg_pgr_mgr_inva),
    };

    dsm_msg_pgr_mgr_inva payload = {
        .page_id = htonl(page_id),
    };

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

// envoie la requete de demande de données de la page
int dsm_srlze_pgr_mgr_data_req(uint32_t page_id, uint8_t flags, uint8_t *buf,
                               size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_pgr_mgr_data_req)) {
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_PAGER_DATA_REQUEST,
        .id = dsm_ctx->node_id, // manager_id qui envoie la requete
        .payload_len = sizeof(dsm_msg_pgr_mgr_data_req),
    };

    dsm_msg_pgr_mgr_data_req payload = {.page_id = htonl(page_id), .flags = flags};

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));

    return offset + sizeof(payload);
}

// Envoyer les données de la page
int dsm_srlze_pgr_data(uint32_t page_id, uint8_t *data, size_t data_len, uint8_t *buf,
                       size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_pgr_data) + data_len) {
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_PAGER_DATA,
        .id =
            dsm_ctx
                ->node_id, // sender_id qui envoie les données (can be manager ou worker)
        .payload_len = sizeof(dsm_msg_pgr_data) + data_len,
    };

    dsm_msg_pgr_data payload = {.page_id = htonl(page_id)};

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));
    offset += sizeof(payload);
    memcpy(buf + offset, data, data_len);
    return offset + data_len;
}

int dsm_srlze_pgr_mgr_ack(uint32_t page_id, uint32_t mode, uint8_t flags, uint8_t *data,
                          size_t data_len, uint8_t *buf, size_t buf_sz) {
    if (buf_sz < sizeof(dsm_msg_header) + sizeof(dsm_msg_pgr_mgr_ack) + data_len) {
        return -1;
    }

    dsm_msg_header header = {
        .code = CODE_PAGER_MANAGER_ACK,
        .id =
            dsm_ctx
                ->node_id, // sender_id qui envoie les données (can be manager ou worker)
        .payload_len = sizeof(dsm_msg_pgr_mgr_ack) + data_len,
    };

    dsm_msg_pgr_mgr_ack payload = {
        .page_id = htonl(page_id), .mode = mode, .flags = flags /*, .data = 0*/};

    int offset = serialize_header(&header, buf, buf_sz);
    if (offset < 0 || (size_t)offset != sizeof(dsm_msg_header)) {
        return -1;
    }
    memcpy(buf + offset, &payload, sizeof(payload));
    offset += sizeof(payload);
    if (data_len > 0) {
        memcpy(buf + offset, data, data_len);
    }
    return offset + data_len;
}

int dsm_dsrlze_header(uint8_t *buf, size_t buf_sz, dsm_msg_header *header) {
    if (buf_sz < sizeof(dsm_msg_header)) {
        LOG_ERROR("Taille du buffer insuffisante pour désérialiser le header : %zu < %zu",
                  buf_sz, sizeof(dsm_msg_header));
        return -1;
    }

    dsm_msg_header net_header;
    memcpy(&net_header, buf, sizeof(net_header));
    header->code = ntohl(net_header.code);
    header->id = ntohl(net_header.id);
    header->payload_len = ntohl(net_header.payload_len);

    return sizeof(dsm_msg_header);
}

int dsm_dsrlze_dscv_init(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_init *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_init)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_INIT, buf_sz, sizeof(dsm_msg_dscv_init));
        return -1;
    }

    dsm_msg_dscv_init net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->udp_port_uni = ntohs(net_msg.udp_port_uni);

    return sizeof(dsm_msg_dscv_init);
}

int dsm_dsrlze_dscv_spvs_data(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_spvs_data *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_spvs_data)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_SUPERVISOR_DATA, buf_sz, sizeof(dsm_msg_dscv_spvs_data));
        return -1;
    }

    dsm_msg_dscv_spvs_data net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->tcp_port_spvs = ntohs(net_msg.tcp_port_spvs);

    return sizeof(dsm_msg_dscv_spvs_data);
}

int dsm_dsrlze_dscv_join_ack(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_join_ack *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_join_ack)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_JOIN_ACK, buf_sz, sizeof(dsm_msg_dscv_join_ack));
        return -1;
    }

    dsm_msg_dscv_join_ack net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->assigned_id = ntohl(net_msg.assigned_id);

    return sizeof(dsm_msg_dscv_join_ack);
}

int dsm_dsrlze_dscv_role_wish(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_role_wish *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_role_wish)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_ROLE_WISH, buf_sz, sizeof(dsm_msg_dscv_role_wish));
        return -1;
    }

    dsm_msg_dscv_role_wish net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->role = ntohl(net_msg.role);

    return sizeof(dsm_msg_dscv_role_wish);
}

static int deserialize_reg_node(uint8_t *buf, size_t buf_sz, dsm_reg_node *node) {
    if (buf_sz < sizeof(dsm_reg_node)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser un noeud du registre : "
            "%zu < %zu",
            buf_sz, sizeof(dsm_reg_node));
        return -1;
    }

    dsm_reg_node net_node;
    memcpy(&net_node, buf, sizeof(net_node));
    node->id = ntohl(net_node.id);
    node->ip = ntohl(net_node.ip);
    node->manager_port = ntohs(net_node.manager_port);

    return sizeof(net_node);
}

int dsm_dsrlze_dscv_share_reg(uint8_t *buf, size_t buf_sz, dsm_msg_dscv_share_reg **msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_share_reg)) {
        LOG_ERROR(
            "1. Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_SHARE_REGISTRY_MANAGER, buf_sz, sizeof(dsm_msg_dscv_share_reg));
        return -1;
    }

    dsm_msg_dscv_share_reg net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    net_msg.sys_total_nodes = ntohl(net_msg.sys_total_nodes);
    net_msg.nb_nodes = ntohl(net_msg.nb_nodes);
    size_t offset = sizeof(dsm_msg_dscv_share_reg);

    size_t needed_sz =
        sizeof(dsm_msg_dscv_share_reg) + net_msg.nb_nodes * sizeof(dsm_reg_node);
    if (buf_sz < needed_sz) {
        LOG_ERROR(
            "2. Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_SHARE_REGISTRY_MANAGER, buf_sz, needed_sz);
        return -1;
    }

    *msg =
        malloc(sizeof(dsm_msg_dscv_share_reg) + net_msg.nb_nodes * sizeof(dsm_reg_node));
    if (!*msg) {
        LOG_ERROR("malloc : msg");
        return -1;
    }

    memcpy(*msg, &net_msg, offset);

    for (uint32_t i = 0; i < net_msg.nb_nodes; i++) {
        int res = deserialize_reg_node(buf + offset, buf_sz - offset, &(*msg)->nodes[i]);
        if (res < 0) {
            free(*msg);
            return -1;
        }
        offset += res;
    }

    return offset;
}

int dsm_dsrlze_dscv_share_reg_ack(uint8_t *buf, size_t buf_sz,
                                  dsm_msg_dscv_share_reg_ack *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_share_reg_ack)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK, buf_sz,
            sizeof(dsm_msg_dscv_share_reg_ack));
        return -1;
    }

    dsm_msg_dscv_share_reg_ack net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->listen_port = ntohs(net_msg.listen_port);

    return sizeof(dsm_msg_dscv_share_reg_ack);
}

int dsm_dsrlze_dscv_wkr_config(uint8_t *buf, size_t buf_sz,
                               dsm_msg_dscv_wkr_config *msg) {
    if (buf_sz < sizeof(dsm_msg_dscv_wkr_config)) {
        LOG_ERROR(
            "Taille du buffer insuffisante pour désérialiser le CODE=%d : %zu < %zu",
            CODE_DISCOVER_WORKER_CONFIG, buf_sz, sizeof(dsm_msg_dscv_wkr_config));
        return -1;
    }

    dsm_msg_dscv_wkr_config net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->nb_pages = be64toh(net_msg.nb_pages);
    msg->page_size = ntohl(net_msg.page_size);

    return sizeof(dsm_msg_dscv_wkr_config);
}

// recevoir la requete du worker pour une page (lecture ou écriture)
int dsm_dsrlze_pgr_wkr_req(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_wkr_req *msg) {
    if (buf_sz < sizeof(dsm_msg_pgr_wkr_req)) {
        return -1;
    }

    dsm_msg_pgr_wkr_req net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->page_id = ntohl(net_msg.page_id);
    msg->mode = net_msg.mode;

    return sizeof(dsm_msg_pgr_wkr_req);
}

// recevoir la requete d'invalidation de la page
int dsm_dsrlze_pgr_mgr_inva(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_mgr_inva *msg) {
    if (buf_sz < sizeof(dsm_msg_pgr_mgr_inva)) {
        return -1;
    }

    dsm_msg_pgr_mgr_inva net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->page_id = ntohl(net_msg.page_id);

    return sizeof(dsm_msg_pgr_mgr_inva);
}

// recevoir la requete de demande de données de la page
int dsm_dsrlze_pgr_mgr_data_req(uint8_t *buf, size_t buf_sz,
                                dsm_msg_pgr_mgr_data_req *msg) {
    if (buf_sz < sizeof(dsm_msg_pgr_mgr_data_req)) {
        return -1;
    }

    dsm_msg_pgr_mgr_data_req net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->page_id = ntohl(net_msg.page_id);
    msg->flags = net_msg.flags;

    return sizeof(dsm_msg_pgr_mgr_data_req);
}

// recevoir les données de la page
int dsm_dsrlze_pgr_data(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_data *msg) {
    if (buf_sz < sizeof(dsm_msg_pgr_data)) {
        return -1;
    }

    dsm_msg_pgr_data net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->page_id = ntohl(net_msg.page_id);
    size_t data_len = buf_sz - sizeof(dsm_msg_pgr_data);
    memcpy(msg->data, buf + sizeof(net_msg), data_len);

    return sizeof(dsm_msg_pgr_data) + data_len;
}

// recevoir l'ack du manager pour la requete de page
int dsm_dsrlze_pgr_mgr_ack(uint8_t *buf, size_t buf_sz, dsm_msg_pgr_mgr_ack *msg) {
    if (buf_sz < sizeof(dsm_msg_pgr_mgr_ack)) {
        return -1;
    }
    dsm_msg_pgr_mgr_ack net_msg;
    memcpy(&net_msg, buf, sizeof(net_msg));
    msg->page_id = ntohl(net_msg.page_id);
    msg->mode = net_msg.mode;
    msg->flags = net_msg.flags;
    size_t data_len = buf_sz - sizeof(dsm_msg_pgr_mgr_ack);
    if (data_len > 0) {
        memcpy(msg->data, buf + sizeof(net_msg), data_len);
    }
    return sizeof(dsm_msg_pgr_mgr_ack) + data_len;
}