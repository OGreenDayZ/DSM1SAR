#include "dsm.h"

int main(int argc, char const *argv[]) {

    dsm_config config;
    dsm_get_default_config(&config);

    return dsm_init(&config);
}
