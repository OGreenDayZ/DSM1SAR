#include "message.h"

#include "debug/debug.h"
#include "network/network.h"

int dsm_tcp_send_all(int sock, const uint8_t *buf, size_t len) {
    LOG_INFO("Envoi de %zu octets...", len);
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
    LOG_INFO("En attente de %zu octets...", len);
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
            LOG_WARNING("connection closed by peer");
            return -2;
        }
        total_recv += recv_len;
    }
    return 0;
}

int dsm_udp_sendto(int sock, const uint8_t *buf, size_t len, uint32_t dest_ip,
                   uint16_t dest_port) {
    LOG_INFO("Envoie de %zu octets", len);
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

/**
 * @note : src_ip & src_port -> Host byte order
 */
int dsm_udp_recvfrom(int sock, uint8_t *buf, size_t len, uint32_t *src_ip,
                     uint16_t *src_port, struct timeval *tv) {
    LOG_INFO("En attente de %zu octets...", len);
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    int res = select(sock + 1, &read_fds, NULL, NULL, tv);
    if (res < 0) {
        LOG_ERROR("select");
        return -1;
    } else if (res == 0) {
        LOG_WARNING("recvfrom timeout");
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

    if (src_ip) {
        *src_ip = ntohl(src_addr.sin_addr.s_addr);
    }
    if (src_port) {
        *src_port = ntohs(src_addr.sin_port);
    }
    return recv_len;
}
