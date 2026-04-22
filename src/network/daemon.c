#include "debug/debug.h"
#include "pager/pager.h"
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdio.h>

int pager_worker_loop() {
    int fd = userfaultfd_init();
    if (fd == -1) {
        perror("pager_worker_loop");
        return -1;
    }

    struct pollfd event = {.fd = fd, .events = POLLIN};
    int ret;
    while (1) {
        ret = poll(&event, 1, 1000);
        if (ret == 0)
            continue;
        if (event.revents & POLLERR) {
            perror("POLLERR");
            break;
        }

        struct uffd_msg fault_msg = {0};
        if (read(fd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
            perror("read");
            break;
        }

        switch (fault_msg.event) {
            case UFFD_EVENT_PAGEFAULT:
                // a exécuter dans un  slave thread du pool de thread

                break;
            case UFFD_EVENT_REMOVE: // lors de l'invocation de madvise
                // envoyer un ack au manager qui demande l'invalidation de la page
                break;
            case UFFD_EVENT_UNMAP: // lors de l'invocation de unmap
                // a gamberge
                break;
        }
    }

    return 0;
}

int handler_pagefault_worker(struct uffd_msg *fault_msg, int fd) {
    __u64 page = fault_msg->arg.pagefault.address;
    int page_index = (page - range_start) / EFFECTIVE_PAGESIZE;
    int flags = fault_msg->arg.pagefault.flags;

    if (flags & UFFD_PAGEFAULT_FLAG_WP) {
        // voir message avec olivier
    }
    return 0;
}

int send_pgr_msg() {
    // LOG_INFO("Envoie d'une msg CODE=%d\n", );
}