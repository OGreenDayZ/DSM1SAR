#include "net_discover.h"

#include "internal.h"
#include "network/network.h"

static dsm_discover_ctx_t discover_ctx;

int dsm_discover_start() {

    int ret = dsm_get_local_ip(discover_ctx.my_ip, INET_ADDRSTRLEN);
}