#include "pager.h"
#include "debug/debug.h"
#include <internal.h>

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
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
        perror("mmap");
        goto error1;
    }
    range_start = (__u64)range;
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
    printf("dsm_init_pager OK, fd = %d \n", fd);

    return fd;
error2:
    if (fd != -1)
        close(fd);
error1:
    return -1;
}

int pager_writeprotect(int fd, __u64 page) {
    struct uffdio_writeprotect wp = {.range = {.start = page, .len = EFFECTIVE_PAGESIZE},
                                     .mode = 0};
    if (ioctl(fd, UFFDIO_WRITEPROTECT, &wp)) {
        perror("ioctl write-protect");
        return -1;
    }
    return 0;
}

int pager_copy(int fd, __u64 dst, __u64 src, int mode) {
    struct uffdio_copy cp = {
        .dst = dst, .src = src, .len = EFFECTIVE_PAGESIZE, .mode = mode};
    if (ioctl(fd, UFFDIO_COPY, &cp)) {
        perror("ioctl copy");
        return -1;
    }
    return 0;
}

int handler_pagefault_worker(struct uffd_msg *fault_msg, int fd) {
    __u64 page = fault_msg->arg.pagefault.address;
    int page_index = (page - range_start) / EFFECTIVE_PAGESIZE;
    int flags = fault_msg->arg.pagefault.flags;

    if (flags & UFFD_PAGEFAULT_FLAG_WP) {
        // accés en écriture a une page présente en mémoire et protégé en écriture
        // envoie un message au manager qui gere la page -> pour prévenir que la page va
        // être modifié donc faut invalider la page chez les autres workers faut juste
        // retirer la protection d'écriture
        struct uffdio_writeprotect wp = {
            .range = {.start = (__u64)page, .len = EFFECTIVE_PAGESIZE}, .mode = 0};
        if (ioctl(fd, UFFDIO_WRITEPROTECT, &wp)) {
            perror("ioctl write-protect");
            return -1;
        }
        // ici pas besoin de demander les données, car si la page est présente en mémoire
        // et qu'on demande une écriture alors entre temps personne n'a écrit sur la page,
        // car sinon on aurai invalider la page
    } else if (flags & UFFD_PAGEFAULT_FLAG_WRITE) {
        // accés en écriture a une page non présente en mémoire
        // contact le manager de la page
        // on reprotège pas la page en écriture ici
        if (0 /*recoit des données de la page car elle avait un propriétaire*/) {
            char *page_data = {
                0}; // normalement = le contenu de la page recu de la part du manager
            struct uffdio_copy cp = {.dst = page,
                                     .src = (__u64)page_data,
                                     .len = EFFECTIVE_PAGESIZE,
                                     .mode = 0};
            if (ioctl(fd, UFFDIO_COPY, &cp)) {
                perror("ioctl copy");
                return -1;
            }
        } else {
            // cas ou la page est vide -> n'a jamais été écrite
            char *page_data = {0};
            struct uffdio_copy cp = {.dst = page,
                                     .src = (__u64)page_data,
                                     .len = EFFECTIVE_PAGESIZE,
                                     .mode = 0};
            if (ioctl(fd, UFFDIO_COPY, &cp)) {
                perror("ioctl copy");
                return -1;
            }
        }
    } else {
        // accés en lecture ou écriture a une page non mappé
        // envoie un message au manager qui gere la page, message avec l'index de la page
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
            char *page_data = {
                0}; // ici page_data est rempli de 0, ce qui est équivalent a faire
                    // UFFDIO_ZEROPAGE, mais je choisi de faire un UFFDIO_COPY pour
                    // pouvoir re-verrouiller la page en écriture de maniere atomique
            struct uffdio_copy cp = {.dst = page,
                                     .src = (__u64)page_data,
                                     .len = EFFECTIVE_PAGESIZE,
                                     .mode = UFFDIO_COPY_MODE_WP};
            if (ioctl(fd, UFFDIO_COPY, &cp)) {
                perror("ioctl copy");
                return -1;
            }
        }
    }
    return 0;
}

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
                handler_pagefault_worker(&fault_msg, fd);
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

// A mettre dans le réseau ca
int handler_pagefault_manager() {
    // reception de <num_page, client_id, READ>
    // reception de <num_page, client_id, WRITE>
    // reception de <num_page, data_page>
    return 0;
}

int manager_read(int page_index, int worker_id) {
    dsm_page *page = NULL;

    // met dans la map le client et le mode de détention de la page
    dsm_hash_for_each_possible(PAGES_MAP, page, node, worker_id) {
        if (page->id == page_index) {
            page->mode = PAGE_R;
            // check si la page a un propriétaire, si oui envoie un message de demande de
            // données au propriétaire
            if (page->owner_id == -1) {
                LOG_INFO("La page n'a pas de propriétaire\n");
            } else {
                // le propriétaire passe de writer à reader (donc on doit lui envoyer
                // un message pour lui remettre la protection d'écriture)
                // envoie un message de réponse au client avec les données de la page
                LOG_INFO("La page a un propriétaire, envoie un message de demande de "
                         "données au propriétaire et faire passer le propriétaire "
                         "de writer à reader\n");
            }
            return 0;
        }
    }
    // si la page n'est pas dans la map, on l'ajoute avec le client comme propriétaire
    page = malloc(sizeof(dsm_page));
    if (!page) {
        perror("malloc manager_read");
        return -1;
    }
    page->id = page_index;
    page->mode = PAGE_R;
    page->owner_id = -1;
    dsm_init_hlist_node(&page->node);
    // la map des pages est indexé par rapport au worker
    dsm_hash_add(PAGES_MAP, &page->node, worker_id);
    return 0;
}

int manager_write(int page_index, int worker_id) {
    dsm_page *page = NULL;

    // met dans la map le client et le mode de détention de la page
    dsm_hash_for_each_possible(PAGES_MAP, page, node, worker_id) {
        if (page->id == page_index) {
            page->mode = PAGE_RW;
            // check si la page a un propriétaire, si oui envoie un message de demande de
            // données au propriétaire
            if (page->owner_id == -1) {
                LOG_INFO("La page n'a pas de propriétaire\n");
            } else {
                // le propriétaire passe de writer à reader (donc on doit lui envoyer
                // un message pour lui remettre la protection d'écriture)
                // envoie un message de réponse au client avec les données de la page
                LOG_INFO("La page a un propriétaire, envoie un message de demande de "
                         "données au propriétaire et faire passer le propriétaire "
                         "de writer à reader + modifié l'owner_id + demande "
                         "d'invalidation de la page a tout les worker\n");
                dsm_hash_for_each_possible(PAGES_MAP, page, node, page->owner_id) {
                    if (page->id == page_index) {
                        page->mode = PAGE_R;
                        break;
                    }
                }
            }
            page->owner_id = worker_id;
            return 0;
        }
    }
    // si la page n'est pas dans la map, on l'ajoute avec le client comme propriétaire
    page = malloc(sizeof(dsm_page));
    if (!page) {
        perror("malloc manager_read");
        return -1;
    }
    page->id = page_index;
    page->mode = PAGE_RW;
    page->owner_id = worker_id;
    dsm_init_hlist_node(&page->node);
    // la map des pages est indexé par rapport au worker
    dsm_hash_add(PAGES_MAP, &page->node, worker_id);
    return 0;
}
