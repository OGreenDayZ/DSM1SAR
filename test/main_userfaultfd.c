#include "dsm.h"
#include "internal.h"
#include "pager/pager.h"

int main(int argc, char const *argv[]) {
    dsm_config dsm_config;
    dsm_get_default_config(&dsm_config);
    dsm_config.net.system.nb_nodes = 5;
    dsm_config.net.system.nb_managers = 2;
    dsm_init(&dsm_config);
    // pager_init(2);
    // PAGE_TABLE;
    printf("%ld\n", EFFECTIVE_PAGESIZE);
    return 0;
}