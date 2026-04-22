#include "pager/pager.h"
#include "pager/pager_msg.h"
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdio.h>

typedef struct {
    struct uffd_msg *fault_msg;
    int fd;
} thread_arg;

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
    int page_index = (page - RANGE_START) / EFFECTIVE_PAGESIZE;
    int flags = fault_msg->arg.pagefault.flags;

    if (flags & UFFD_PAGEFAULT_FLAG_WP) {
        // voir message avec olivier

        // accés en écriture a une page présente en mémoire et protégé en écriture
        // envoie un message au manager qui gere la page -> pour prévenir que la page va
        // être modifié donc faut invalider la page chez les autres workers faut juste
        // retirer la protection d'écriture
        send_wkr_req(page_index, WRITE_REQ);
        pager_writeprotect(fd, page); // retire la protection d'écriture
    } else if (flags & UFFD_PAGEFAULT_FLAG_WRITE) {
        // envoie une demande d'écriture au manager
        send_wkr_req(page_index, WRITE_REQ);
        // attends la réponse du manager
    }
    return 0;
}
