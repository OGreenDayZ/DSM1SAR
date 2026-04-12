#include "dsm.h"
#include "pager/pager.h"
#include <stdlib.h>

int main(int argc, char const *argv[]) {
    dsm_config dsm_config;
    dsm_get_default_config(&dsm_config);
    // dsm_init(dsm_config);
    dsm_init_pager(2);
    return 0;
}