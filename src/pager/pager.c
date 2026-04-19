#include "pager.h"
#include <fcntl.h>
#include <internal.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>

static int userfaultfd(int flags) {
    // UFFD_USER_MODE_ONLY -> rattrape que les fautes user et non kernel
    return syscall(SYS_userfaultfd, flags | UFFD_USER_MODE_ONLY);
}

static int userfaultfd_init(void *range) {
    int fd = userfaultfd(O_NONBLOCK);
    if (fd == -1) {
        perror("userfaultfd");
        goto error1;
    }

    struct uffdio_api api = {.api = UFFD_API};
    if (ioctl(fd, UFFDIO_API, &api)) {
        perror("ioctl api");
        goto error2;
    }

    struct uffdio_register reg = {
        .mode = UFFDIO_REGISTER_MODE_MISSING |
                UFFDIO_REGISTER_MODE_WP, // notification lors de l'absence
                                         //  d'une page et lors de l'écriture dans une
                                         //  page protéger en écriture
        .range = {.start = (long)range, .len = EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES}};

    if (ioctl(fd, UFFDIO_REGISTER, &reg)) {
        perror("ioctl register");
        goto error2;
    }

    return fd;
error2:
    if (fd)
        close(fd);
error1:
    return -1;
}

void thread_pager(int *fd) {
    struct pollfd event = {.fd = *fd, .events = POLLIN};
    while (poll(&event, 1, 1000)) {
        if (event.revents & POLLERR) {
            perror("POLLERR");
            break;
        }

        struct uffd_msg fault_msg = {0};
        if (read(*fd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
            perror("read");
            break;
        }

        switch (fault_msg.event) {
            case UFFD_EVENT_PAGEFAULT:
                handler_pagefault(&fault_msg, *fd);
                break;
            case UFFD_EVENT_REMOVE: // lors de l'invocation de madvise
                break;
            case UFFD_EVENT_UNMAP:
                break;
        }
    }
}

int pager_init() {
    // Initialise la plage mémoire à surveiller
    void *range = mmap(NULL, EFFECTIVE_PAGESIZE * EFFECTIVE_NB_PAGES,
                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (range == MAP_FAILED) {
        perror("mmap");
        goto error1;
    }
    range_start = (__u64)range;
    int *userfault = malloc(sizeof(int));
    if (!(*userfault = userfaultfd_init(range))) {
        perror("userfault_init");
        goto error2;
    }
    printf("dsm_init_pager OK, fd = %d \n", *userfault);

    return 0;
error2:
    munmap(range, EFFECTIVE_PAGESIZE);
error1:
    return -1;
}

int handler_pagefault(struct uffd_msg *fault_msg, int fd) {
    __u64 page = fault_msg->arg.pagefault.address;
    int page_index = (page - range_start) / EFFECTIVE_PAGESIZE;
    int flags = fault_msg->arg.pagefault.flags;

    if (flags & UFFD_PAGEFAULT_FLAG_WP) {
        // accés en écriture a une page protégé en écriture
        // envoie un message au manager qui gere la page
    } else if (flags & UFFD_PAGEFAULT_FLAG_WRITE) {
        // accés en écriture a une page non mappé
    } else {
        // accés en lecture ou écriture a une page non mappé
        // envoie un message au manager qui gere la page
        if (0 /*recoit des données de la page car elle avait un propriétaire*/) {
            char *page_data = {
                0}; // normalement = le contenu des data recu de la part du manager
            struct uffdio_copy cp = {
                .dst = page,
                .src = (__u64)page_data,
                .len = EFFECTIVE_PAGESIZE,
                .mode = UFFDIO_COPY_MODE_WP /* marque la page write-protected*/};
            if (ioctl(fd, UFFDIO_COPY, &cp)) {
                perror("ioctl copy");
                return -1;
            }
        } else {
            // cas ou la page est vide -> n'a jamais été écrite
            struct uffdio_zeropage zp = {
                .range = {.start = page, .len = EFFECTIVE_PAGESIZE}, .mode = 0};
            if (ioctl(fd, UFFDIO_ZEROPAGE, &zp)) {
                perror("ioctl zeropage");
                return -1;
            }
            // Pour protéger la page en écriture apres son initialisation
            struct uffdio_writeprotect wp = {
                .range = {.start = page, .len = EFFECTIVE_PAGESIZE},
                .mode = UFFDIO_WRITEPROTECT_MODE_WP};
            ioctl(fd, UFFDIO_WRITEPROTECT, &wp);
        }
    }
    return 0;
}