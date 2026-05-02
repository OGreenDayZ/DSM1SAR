#include "spvs_factory.h"

#include "debug/debug.h"
#include "dsm.h"
#include "internal.h"
#include "network/message/serializer.h"
#include "network/network.h"
#include "network/worker/worker.h"

static int send_dscv_spvs_data(uint32_t ip_dst, uint16_t port_dst) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_SUPERVISOR_DATA);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_spvs_data)];
    int msg_len = dsm_srlze_dscv_spvs_data(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int send_sock = dsm_udp_sock();
    if (send_sock < 0) {
        return -1;
    }

    if (dsm_udp_sendto(send_sock, msg_buf, msg_len, ip_dst, port_dst) < 0) {
        close(send_sock);
        return -1;
    }

    close(send_sock);
    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_SUPERVISOR_DATA);
    return 0;
}

int treat_dscv_init(dsm_msg_udp_thread_ctx *msg_ctx, dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_DISCOVER_INIT);

    uint32_t ip_src = msg_ctx->src_addr.sin_addr.s_addr;
    dsm_eligible_entry *entry = malloc(sizeof(dsm_eligible_entry));
    if (!entry) {
        LOG_ERROR("malloc : entry");
        return -1;
    }
    entry->ip = ip_src;
    dsm_safe_list_add_tail(&spvs_ctx->eligible_ip, &entry->node);

    dsm_msg_dscv_init init_msg;
    if (dsm_dsrlze_dscv_init(msg_ctx->payload, msg_ctx->header.payload_len, &init_msg) <
        0) {
        goto exit_err;
    }

    if (send_dscv_spvs_data(ip_src, init_msg.udp_port_uni) < 0) {
        goto exit_err;
    }

    LOG_INFO("Succès du traitement du CODE=%d", CODE_DISCOVER_INIT);
    return 0;

exit_err:
    free(entry);
    return -1;
}

int send_dscv_join_ack(int dest_sock, unsigned int assigned_id) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_JOIN_ACK);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_join_ack)];
    int msg_len = dsm_srlze_dscv_join_ack(assigned_id, msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(dest_sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_JOIN_ACK);
    return 0;
}

int send_dscv_sys_full(int dest_sock) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_SYSTEM_FULL);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_sys_full(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    int sent = dsm_tcp_send_all(dest_sock, msg_buf, msg_len);
    if (sent < 0) {
        return -1;
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_SYSTEM_FULL);
    return 0;
}

int treat_dscv_role_wish(dsm_msg_tcp_thread_ctx *msg_ctx, dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_DISCOVER_ROLE_WISH);

    dsm_msg_dscv_role_wish role_wish;
    if (dsm_dsrlze_dscv_role_wish(msg_ctx->payload, msg_ctx->header.payload_len,
                                  &role_wish) < 0) {
        return -1;
    }

    unsigned int id_node = msg_ctx->header.id;
    atomic_fetch_add(&spvs_ctx->role_wish_count, 1);
    LOG_INFO("Node ID %u souhaite être %s", id_node,
             role_wish.role == DSM_ROLE_MANAGER  ? "MANAGER"
             : role_wish.role == DSM_ROLE_WORKER ? "WORKER"
                                                 : "UNDEFINED");
    dsm_supervising_node *node_content;
    dsm_hash_safe_get(&spvs_ctx->spvs_reg, node_content, node, id_node) {
        if (node_content->id == id_node) {
            node_content->role = role_wish.role;
            LOG_INFO("Enregistrement du souhait");
            // dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, id_node);
            // break;
        }
    }
    dsm_hash_safe_get_end(&spvs_ctx->spvs_reg, id_node);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_DISCOVER_ROLE_WISH);
    return 0;
}

static int make_share_reg(dsm_supervisor_ctx *spvs_ctx, dsm_discover_ctx *dscv_ctx,
                          dsm_node_role shared_role, dsm_msg_dscv_share_reg *share,
                          size_t share_nb) {

    LOG_INFO("Construction d'un registre partagé de %d nodes pour le rôle %s ...",
             share_nb, shared_role == DSM_ROLE_MANAGER ? "MANAGER" : "WORKER");
    share->sys_total_nodes = dsm_ctx->config.net.system.nb_nodes;
    share->nb_nodes = share_nb;
    size_t index = 0;
    dsm_node_role *my_role = &dsm_ctx->config.net.node.role;
    if (*my_role == shared_role) {
        share->nodes[index].id = 0;
        share->nodes[index].ip = dscv_ctx->my_ip;
        share->nodes[index].manager_port = 0; // inutile pour le superviseur
        /**  Pas de liaison ici, car impossible d'avoir
         * (my_role == DSM_ROLE_WORKER && shared_role == DSM_ROLE_MANAGER) */
        index++;
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        if (index == share_nb) {
            dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, bkt);
            goto exit;
        }

        else if (pos->role == shared_role) {
            share->nodes[index].id = pos->id;
            share->nodes[index].ip = pos->ip;
            if (shared_role == DSM_ROLE_MANAGER) {
                share->nodes[index].manager_port = pos->manager_port;
                if (*my_role == DSM_ROLE_WORKER) {
                    dsm_mgr_to_node_content *convert_node_content =
                        malloc(sizeof(dsm_mgr_to_node_content));
                    if (!convert_node_content) {
                        LOG_ERROR("Erreur malloc : convert_node_content");
                        dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, bkt);
                        return -1;
                    }
                    convert_node_content->manager_id = index;
                    convert_node_content->node_id = pos->id;
                    convert_node_content->to_node_content =
                        NULL; // sera set dans spvs_ctx_cleanup

                    pos->set_shortcut_mgr = convert_node_content;

                    dsm_init_hlist_node(&convert_node_content->node);
                    dsm_hash_safe_add(&dsm_wkr_ctx->managerID_to_nodeID,
                                      &convert_node_content->node,
                                      convert_node_content->manager_id);
                }

            } else {
                share->nodes[index].manager_port = 0; // inutile pour les workers
            }
            index++;
        }
    }
exit:
    if (index == share_nb) {
        LOG_INFO("Succès de la construction du registre");
        return 0;
    }
    LOG_ERROR("Nombre de partages dans le registre insuffisant : %zu < %zu", index,
              share_nb);
    return -1;
}

int send_dscv_share_reg(dsm_supervisor_ctx *spvs_ctx, dsm_discover_ctx *dscv_ctx,
                        dsm_msg_code code) {
    if (code != CODE_DISCOVER_SHARE_REGISTRY_MANAGER &&
        code != CODE_DISCOVER_SHARE_REGISTRY_WORKER) {
        LOG_ERROR("Code de partage de registre inconnu : %d", code);
        return -1;
    }
    LOG_INFO("Envoi d'un CODE=%d ...", code);

    dsm_node_role shared_role = (code == CODE_DISCOVER_SHARE_REGISTRY_MANAGER)
                                    ? DSM_ROLE_WORKER
                                    : DSM_ROLE_MANAGER;
    dsm_system_config *sys_cfg = &dsm_ctx->config.net.system;
    size_t share_nb = shared_role == DSM_ROLE_MANAGER
                          ? sys_cfg->nb_managers
                          : sys_cfg->nb_nodes - sys_cfg->nb_managers;
    size_t buf_sz = sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_share_reg) +
                    share_nb * sizeof(dsm_reg_node);
    uint8_t msg_buf[buf_sz];

    dsm_msg_dscv_share_reg *share =
        malloc(sizeof(dsm_msg_dscv_share_reg) + share_nb * sizeof(dsm_reg_node));
    if (!share) {
        LOG_ERROR("malloc : share");
        return -1;
    }

    if (make_share_reg(spvs_ctx, dscv_ctx, shared_role, share, share_nb) < 0) {
        free(share);
        return -1;
    }

    int msg_len = dsm_srlze_dscv_share_reg(code, share, msg_buf, buf_sz);
    free(share);
    if (msg_len < 0) {
        return -1;
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        if (pos->role != shared_role) {
            int sent = dsm_tcp_send_all(pos->tcp_sock, msg_buf, msg_len);
            if (sent < 0) {
                LOG_ERROR("Erreur d'envoi du registre partagé au node ID %u", pos->id);
                dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, bkt);
                return -1;
            }
        }
    }
    LOG_INFO("Succès de l'envoi du CODE=%d", code);
    return 0;
}

int treat_dscv_share_reg_ack(dsm_msg_tcp_thread_ctx *msg_ctx,
                             dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);

    dsm_msg_dscv_share_reg_ack share_reg_ack;
    if (dsm_dsrlze_dscv_share_reg_ack(msg_ctx->payload, msg_ctx->header.payload_len,
                                      &share_reg_ack) < 0) {
        return -1;
    }

    unsigned int id_node = msg_ctx->header.id;
    atomic_fetch_add(&spvs_ctx->share_mgr_count, 1);

    dsm_supervising_node *node_content;
    dsm_hash_safe_get(&spvs_ctx->spvs_reg, node_content, node, id_node) {
        if (node_content->id == id_node) {
            node_content->manager_port = share_reg_ack.listen_port;
            // dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, id_node);
            // break;
        }
    }
    dsm_hash_safe_get_end(&spvs_ctx->spvs_reg, id_node);
    LOG_INFO("Succès du traitement du CODE=%d", CODE_DISCOVER_SHARE_REGISTRY_MANAGER_ACK);
    return 0;
}

int treat_dscv_mgr_ready(dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_DISCOVER_MANAGER_READY);

    atomic_fetch_add(&spvs_ctx->mgr_ready_count, 1);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_DISCOVER_MANAGER_READY);
    return 0;
}

int treat_dscv_wkr_config_ack(dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Traitement d'un CODE=%d ...", CODE_DISCOVER_WORKER_CONFIG_ACK);

    atomic_fetch_add(&spvs_ctx->wkr_config_count, 1);

    LOG_INFO("Succès du traitement du CODE=%d", CODE_DISCOVER_WORKER_CONFIG_ACK);
    return 0;
}

int send_dscv_wkr_config(dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_WORKER_CONFIG);

    uint8_t msg_buf[sizeof(dsm_msg_header) + sizeof(dsm_msg_dscv_wkr_config)];
    dsm_pager_config *pager_cfg = &dsm_ctx->config.pager;
    int msg_len = dsm_srlze_dscv_wkr_config(pager_cfg->nb_pages, pager_cfg->page_size,
                                            msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        if (pos->role == DSM_ROLE_WORKER) {
            int sent = dsm_tcp_send_all(pos->tcp_sock, msg_buf, msg_len);
            if (sent < 0) {
                LOG_ERROR("Erreur d'envoi de la configuration du worker au node ID %u",
                          pos->id);
                dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, bkt);
                return -1;
            }
        }
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_WORKER_CONFIG);
    return 0;
}

int send_dscv_finalize(dsm_supervisor_ctx *spvs_ctx) {
    LOG_INFO("Envoi d'un CODE=%d ...", CODE_DISCOVER_FINALIZE);

    uint8_t msg_buf[sizeof(dsm_msg_header)];
    int msg_len = dsm_srlze_dscv_finalize(msg_buf, sizeof(msg_buf));
    if (msg_len < 0) {
        return -1;
    }

    unsigned int bkt;
    dsm_supervising_node *pos;
    dsm_hash_safe_for_each(&spvs_ctx->spvs_reg, bkt, pos, node) {
        int sent = dsm_tcp_send_all(pos->tcp_sock, msg_buf, msg_len);
        if (sent < 0) {
            LOG_ERROR("Erreur d'envoi du message de finalisation au node ID %u", pos->id);
            dsm_hash_safe_unlock(&spvs_ctx->spvs_reg, bkt);
            return -1;
        }
    }

    LOG_INFO("Succès de l'envoi du CODE=%d", CODE_DISCOVER_FINALIZE);
    return 0;
}