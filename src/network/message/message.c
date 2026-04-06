#include "message.h"

#include "debug/debug.h"
#include "network/network.h"
#include <arpa/inet.h>
#include <unistd.h>

int dsm_tcp_send_all(int sock, const uint8_t *buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent < 0) {
            LOG_ERROR("send");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

int dsm_tcp_recv_all(int sock, uint8_t *buf, size_t len, struct timeval *tv) {
    size_t total_recv = 0;
    while (total_recv < len) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        int res = select(sock + 1, &read_fds, NULL, NULL, tv);
        if (res < 0) {
            LOG_ERROR("select");
            return -1;
        } else if (res == 0) {
            LOG_ERROR("recv timeout");
            return -1;
        }

        ssize_t recv_len = recv(sock, buf + total_recv, len - total_recv, 0);
        if (recv_len < 0) {
            LOG_ERROR("recv");
            return -1;
        } else if (recv_len == 0) {
            LOG_ERROR("connection closed by peer");
            return -1;
        }
        total_recv += recv_len;
    }
    return 0;
}

int dsm_udp_sendto(int sock, const uint8_t *buf, size_t len, const char *dest_ip,
                   int dest_port) {
    struct sockaddr_in dest_addr;
    if (dsm_make_addr(dest_ip, dest_port, &dest_addr) < 0) {
        return -1;
    }
    ssize_t sent =
        sendto(sock, buf, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0) {
        LOG_ERROR("sendto");
        return -1;
    } else if ((size_t)sent != len) {
        LOG_ERROR("sendto: envoyés:%zd | attendus:%zu", sent, len);
        return -1;
    }
    return 0;
}

int dsm_udp_recvfrom(int sock, uint8_t *buf, size_t len, char *src_ip, int *src_port,
                     struct timeval *tv) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    int res = select(sock + 1, &read_fds, NULL, NULL, tv);
    if (res < 0) {
        LOG_ERROR("select");
        return -1;
    } else if (res == 0) {
        LOG_ERROR("recvfrom timeout");
        return -1;
    }

    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    ssize_t recv_len =
        recvfrom(sock, buf, len, 0, (struct sockaddr *)&src_addr, &addr_len);
    if (recv_len < 0) {
        LOG_ERROR("recvfrom");
        return -1;
    } else if ((size_t)recv_len > len) {
        LOG_ERROR("recvfrom: reçus:%zd | attendus:%zu", recv_len, len);
        return -1;
    }

    if (src_ip &&
        inet_ntop(AF_INET, &src_addr.sin_addr, src_ip, INET_ADDRSTRLEN) == NULL) {
        LOG_ERROR("inet_ntop");
        return -1;
    }
    if (src_port) {
        *src_port = ntohs(src_addr.sin_port);
    }
    return recv_len;
}
