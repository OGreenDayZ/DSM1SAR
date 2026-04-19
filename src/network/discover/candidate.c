#include "candidate.h"

static dsm_candidate_ctx *cand_ctx;

static int cand_ctx_init(dsm_dscv_ctx *dscv_ctx);
static void cand_ctx_cleanup();

int dsm_candidate_start(dsm_dscv_ctx *dscv_ctx) {

    // TODO MOVE FROM NET_DISCOVER
    // TODO APRES VERSION STABLE DISCOVER
    return 0;
}