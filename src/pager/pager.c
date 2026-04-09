#include <stdio.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <poll.h>
#include <internal.h>

#include "pager.h"

static int userfaultfd(int flags){
    // UFFD_USER_MODE_ONLY -> rattrape que les fautes user et non kernel
    return syscall(SYS_userfaultfd, flags | UFFD_USER_MODE_ONLY); 
}

static int userfaultfd_init(void* range, int nb_page){
    int fd = userfaultfd(O_NONBLOCK);
    if(fd == -1){
        perror("userfaultfd"); 
        goto error1; 
    }

    struct uffdio_api api = { .api = UFFD_API }; 
    if(ioctl(fd, UFFDIO_API, &api)){
        perror("ioctl api"); 
        goto error2; 
    }

    struct uffdio_register reg = {
            .mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP,  // notification lors de l'absence 
                                                //  d'une page et lors de l'écriture dans une page protéger en écriture
            .range = { .start = (long)range, .len = PAGESIZE*nb_page}
            
    }; 

    if(ioctl(fd, UFFDIO_REGISTER, &reg)){
        perror("ioctl register"); 
        goto error2;
    }
    
    return fd; 
error2 : 
    if(fd)
        close(fd); 
error1 :
    return -1; 
}

void dsm_thread_pager(int* fd){
    struct pollfd event = { .fd = *fd, .events = POLLIN}; 
    while(poll(&event, 1, 1000)){
        if(event.revents & POLLERR){
            perror("POLLERR"); 
            break; 
        }

        struct uffd_msg fault_msg = {0}; 
        if(read(*fd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)){
            perror("read"); 
            break; 
        }

        switch(fault_msg.event){
            case UFFD_EVENT_PAGEFAULT : // && fault_msg. == UFFD_PAGEFAULT_FLAG_WP: 
                break; 
            case UFFD_EVENT_REMOVE : // lors de l'invocation de madvise
                break; 
            case UFFD_EVENT_UNMAP : 
                break;
        }

    }
}

int dsm_init_pager(ssize_t nb_page){
    // Initialise la plage mémoire à surveiller 
    void* range = mmap(NULL, dsm_ctx->config.pager.nb_pages*PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) ;
    if(range == MAP_FAILED){
        perror("mmap"); 
        goto error1; 
    }
    int* userfault = malloc(sizeof(int)); 
    if(!(*userfault = userfaultfd_init(range, nb_page))){
        perror("userfault_init"); 
        goto error2; 
    }
    printf("dsm_init_pager OK, fd = %d \n",*userfault ); 

    
    return 0;
error2 : 
    munmap(range, nb_page); 
error1: 
    return -1; 
}