#include "net.h"

#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>

#include "macros.h"


int net_fd = -1;

#define MAX_RETRIES 5


net_status_t net_open_socket(const char *__restrict hostname,
		const char *__restrict port, net_try_addr_cb test_addr)
{
    int fd, err;
    struct addrinfo hints, *addrlist, *addr;
    int enable = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET6;
    hints.ai_protocol = IPPROTO_UDP;

	LOG("Resolving [%s]:%s", hostname, port);

    if ((err = getaddrinfo(hostname, port, &hints, &addrlist)) != 0)
        goto_trace(error, "%s", gai_strerror(err));
    /* Attempt to bind on the first working interface */
    for (addr = addrlist; addr != NULL; addr = addr->ai_next) {
        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd == -1)
            continue;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)))
            goto_trace(close, "Couldn't enable the re-use of the address ...");
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &enable, sizeof(enable)))
            goto_trace(close, "Cannot force the socket to IPv6");
        if (test_addr(fd, addr->ai_addr, addr->ai_addrlen) != -1)
            break; /* Success ! */
close:
            close(fd); /* Try another one*/
    }
    freeaddrinfo(addrlist);
    if (!addr)
        goto_trace(error, "Could find any address for the given hostname!");

	net_fd = fd;
    return NET_OK;

error:
    return NET_ERROR;

}

void net_close_socket()
{
	if (net_fd >= 0)
		close(net_fd);
}

PRIVATE net_status_t net_recvfrom(pkt_t *rbuf, void *addr, socklen_t *addrlen)
{
	ssize_t rlen;

	if ((rlen = recvfrom(net_fd, rbuf, sizeof(*rbuf), 0, addr, addrlen)) == -1)
		goto_trace(rx_err, "Failed to receive a packet: %s", strerror(errno));
	if (pkt_decode_inline(rbuf, rlen) != PKT_OK)
		goto drop;
	return NET_OK;

rx_err:
	return NET_ERROR;
drop:
	return NET_DROP;
}

net_status_t net_recv_pkt(pkt_t *rbuf, uint8_t expected_seq,
		uint8_t win_size)
{
	int err;

	if ((err = net_recvfrom(rbuf, NULL, NULL)) != NET_OK)
		return err;
    if ((uint8_t)(rbuf->seq - expected_seq) > win_size)
        goto_trace(drop, "Dropping out of window packet [rcv: %u, expect: %u"
				", winsize: %u]", rbuf->seq, expected_seq, win_size);
    LOG("< #%u", rbuf->seq);
    return NET_OK;

drop:
    return NET_DROP;
}
/* Wait to receive a packet with the given expected sequence number,
 * and connect to it */
net_status_t net_wait_and_connect(pkt_t *rbuf, uint8_t expect_seq)
{
	struct sockaddr_storage addr = {0};
	socklen_t addr_len = sizeof(addr_len);
	int retry_count = 0, err;

	LOG("Waiting to receive data #%u from the remote endpoint", expect_seq);
	for (;;) {
		++retry_count;
		if (retry_count > MAX_RETRIES)
			goto_trace(fail, "Giving up after %d retries", MAX_RETRIES);
		if ((err = net_recvfrom(rbuf, &addr, &addr_len)) != NET_OK)
			if (err == NET_DROP)
				continue; /* retry ... */
			else
				goto_trace(fail, "I/O error");
		else if (rbuf->seq != expect_seq)
			trace_error("Ignoring packet with seqnum #%u != expected:%u",
					rbuf->seq, expect_seq);
		else {
			char host[NI_MAXHOST], port[NI_MAXSERV];
			const struct sockaddr *sock_addr = (const struct sockaddr*)&addr;
			/* We ignore the return value as we're fine with numeric hostname
			 * if the reverse-dns query fails */
			if ((err = getnameinfo(sock_addr, addr_len, host, sizeof(host),
							port, sizeof(port), NI_NUMERICSERV)))
				ERROR("Could not resolve the peer address: %s",
						gai_strerror(err));
			LOG("Received data #%u from [%s]:%s", expect_seq, host, port);
			if (connect(net_fd, sock_addr, addr_len))
				goto_trace(fail, "Could not connect the socket to the remote "
						"endpoint: %s", strerror(errno));
			break;
		}
	}
	return NET_OK;

fail:
	return NET_ERROR;
}

static inline size_t pkt_len_serial(const pkt_t *pkt)
{
	if (pkt->length)
		return PKT_HEADERLEN + PKT_FOOTERLEN + ntohs(pkt->length);

	return PKT_HEADERLEN;
}

net_status_t net_send(const pkt_t *pkt)
{
	/* Recover the serialized packet length */
	int plen = pkt_len_serial(pkt);
	/* Assumes we called a connect on the socket at some point */
	if (write(net_fd, pkt, plen) != plen) {
		trace_error("Cannot send packet #%u: %s", pkt->seq, strerror(errno));
		return NET_ERROR;
	}
	LOG("> #%u", pkt->seq);
	return NET_OK;
}
