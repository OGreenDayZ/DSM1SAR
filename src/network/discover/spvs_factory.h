#ifndef DSM_SPVS_FACTORY_H
#define DSM_SPVS_FACTORY_H

/**
 * @file : sous fichier de supervisor.c, pour les IO
 * @note : les fonctions "treat" de ce fichier sont invoquées après les recv sur les
 * sockets
 */

#include "network/discover/supervisor.h"
#include "network/message/message.h"

int treat_dscv_init(dsm_msg_udp_thread_ctx *msg_ctx, dsm_supervisor_ctx *spvs_ctx);
int send_dscv_join_ack(int dest_sock, unsigned int assigned_id);
int send_dscv_sys_full(int dest_sock);
int treat_dscv_role_wish(dsm_msg_tcp_thread_ctx *msg_ctx, dsm_supervisor_ctx *spvs_ctx);
int send_dscv_share_reg(dsm_supervisor_ctx *spvs_ctx, dsm_discover_ctx *dscv_ctx,
                        dsm_msg_code code);
int treat_dscv_share_reg_ack(dsm_msg_tcp_thread_ctx *msg_ctx,
                             dsm_supervisor_ctx *spvs_ctx);
int treat_dscv_mgr_ready(dsm_supervisor_ctx *spvs_ctx);
int send_dscv_wkr_config(dsm_supervisor_ctx *spvs_ctx);
int treat_dscv_wkr_config_ack(dsm_supervisor_ctx *spvs_ctx);
int send_dscv_finalize(dsm_supervisor_ctx *spvs_ctx);

#endif // DSM_SPVS_FACTORY_H