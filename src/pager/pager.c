#include "pager.h"

#include "debug/debug.h"

#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static int userfaultfd(int flags) {
    // UFFD_USER_MODE_ONLY -> rattrape que les fautes user et non kernel
    return syscall(SYS_userfaultfd, flags | UFFD_USER_MODE_ONLY);
}

int userfaultfd_init() {
    void *range = mmap(NULL, EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (range == MAP_FAILED) {
        LOG_ERROR("mmap userfaultfd_init");
        goto error1;
    }
    dsm_pager_uffd = -1;
    RANGE_START = (__u64)range;
    int fd = userfaultfd(O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) {
        LOG_ERROR("userfaultfd userfaultfd_init");
        goto error1;
    }

    struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_PAGEFAULT_FLAG_WP};
    if (ioctl(fd, UFFDIO_API, &api)) {
        LOG_ERROR("ioctl api userfaultfd_init");
        goto error2;
    }

    struct uffdio_register reg = {
        .mode = UFFDIO_REGISTER_MODE_MISSING |
                UFFDIO_REGISTER_MODE_WP, // notification lors de l'absence
                                         //  d'une page et lors de l'écriture dans une
                                         //  page protéger en écriture
        .range = {.start = (long)range, .len = EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES}};

    if (ioctl(fd, UFFDIO_REGISTER, &reg)) {
        LOG_ERROR("ioctl register userfaultfd_init");
        goto error2;
    }

    dsm_pager_uffd = fd;
    LOG_INFO("dsm_init_pager OK, fd = %d \n", fd);

    return fd;
error2:
    if (fd != -1)
        close(fd);
error1:
    return -1;
}

int pager_writeprotect(int page_id) {
    __u64 page = RANGE_START + page_id * EFFECTIVE_PAGESIZE;
    struct uffdio_writeprotect wp = {.range = {.start = page, .len = EFFECTIVE_PAGESIZE},
                                     .mode = 0};
    if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp)) {
        LOG_ERROR("ioctl write-protect");
        return -1;
    }
    return 0;
}

int pager_copy(int page_id, __u64 src, int mode) {
    __u64 dst = RANGE_START + page_id * EFFECTIVE_PAGESIZE;
    struct uffdio_copy cp = {
        .dst = dst, .src = src, .len = EFFECTIVE_PAGESIZE, .mode = mode};
    if (ioctl(uffd, UFFDIO_COPY, &cp)) {
        LOG_ERROR("ioctl copy");
        return -1;
    }
    return 0;
}

// Trouver le manager qui gere la page :
// TODO - majda
int get_manager_for_page(int page_index) {
    // Pour le worker pages_reg est indexé par l'id du manager
    dsm_page *page;
    // MAP_WORKER

    return 0;
}
