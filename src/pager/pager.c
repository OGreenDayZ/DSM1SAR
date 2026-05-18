#include "pager.h"

#include <asm-generic/int-ll64.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug/debug.h"
#include "internal.h"
#include "network/worker/worker.h"

static int userfaultfd(int flags) {
    // UFFD_USER_MODE_ONLY -> rattrape que les fautes user et non kernel
    return syscall(SYS_userfaultfd, flags | UFFD_USER_MODE_ONLY);
}

int userfaultfd_init(int *out_uffd, uint64_t *out_range_start) {
    void *range = mmap(NULL, EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (range == MAP_FAILED) {
        LOG_ERROR("mmap userfaultfd_init");
        goto error1;
    }
    *out_range_start = (__u64)range;
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

    *out_uffd = fd;
    LOG_INFO("dsm_init_pager : OK, FD=%d \n", fd);

    return fd;
error2:
    if (fd != -1)
        close(fd);
error1:
    return -1;
}

int pager_protect(int uffd, int page_id, int protection) {
    if (protection == 0) {
        LOG_INFO("[PAGER] Promotion en écriture de la page %d", page_id);

    } else if (protection == UFFDIO_WRITEPROTECT_MODE_WP) {
        LOG_INFO("[PAGER] Protection en écriture de la page %d", page_id);

    } else {
        LOG_ERROR("[PAGER] Protection invalide dans pager_protect");
        return -1;
    }

    __u64 page = dsm_wkr_ctx->range_start + page_id * EFFECTIVE_PAGESIZE;
    struct uffdio_writeprotect wp = {.range = {.start = page, .len = EFFECTIVE_PAGESIZE},
                                     .mode = protection};
    if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp)) {
        LOG_ERROR("ioctl write-protect");
        return -1;
    }
    return 0;
}

int pager_copy(int uffd, int page_id, __u64 src, int mode) {
    if (mode == 0) {
        LOG_INFO("[PAGER] Copie & Promotion en écriture de la page %d", page_id);

    } else if (mode == UFFDIO_COPY_MODE_WP) {
        LOG_INFO("[PAGER] Copie & Protection en écriture immédiate de la page %d",
                 page_id);

    } else {
        LOG_ERROR("[PAGER] Mode invalide dans pager_copy");
    }

    __u64 dst = dsm_wkr_ctx->range_start + page_id * EFFECTIVE_PAGESIZE;
    struct uffdio_copy cp = {
        .dst = dst, .src = src, .len = EFFECTIVE_PAGESIZE, .mode = mode};
    if (ioctl(uffd, UFFDIO_COPY, &cp)) {
        LOG_ERROR("ioctl copy");
        return -1;
    }
    return 0;
}

int pager_destroy() {
    struct uffdio_range range = {.start = dsm_wkr_ctx->range_start,
                                 .len = EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES};
    if (ioctl(dsm_wkr_ctx->uffd, UFFDIO_UNREGISTER, &range)) {
        LOG_ERROR("ioctl unregister pager_destroy");
        return -1;
    }

    if (close(dsm_wkr_ctx->uffd)) {
        LOG_ERROR("close uffd pager_destroy");
        return -1;
    }

    if (munmap((void *)dsm_wkr_ctx->range_start,
               EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES)) {
        LOG_ERROR("munmap pager_destroy");
        return -1;
    }

    return 0;
}