#ifndef __NET_H_
#define __NET_H_

#include <netinet/in.h>
#include <stdint.h>

#include "packet_interface.h"

extern int net_fd;

typedef enum {
	NET_OK,
	NET_ERROR,
	NET_DROP
} net_status_t;

/* return -1 if the combination of socket/address is not suited for the task */
typedef int (*net_try_addr_cb)(int socket,
        const struct sockaddr *addr,
        socklen_t addrlen);
/* Typical examples: bind, connect */

/* Open a socket for the desired hostname and port, calls test_addr to check if
 * the current address is suited for the usage of the socket */
net_status_t net_open_socket(const char *__restrict hostname,
		const char *__restrict port, net_try_addr_cb test_addr);

/* Cleanup the net subsystem */
void net_close_socket();

/* Receive a packet, checking if its in window, non-corrupted
 */
net_status_t net_recv_pkt(pkt_t *, uint8_t expected_seq,
		uint8_t win_size);
/* Wait to receive a packet with the given expected sequence number,
 * and connect to it */
net_status_t net_wait_and_connect(pkt_t*, uint8_t);

/* Send a packet through the given file descriptor -- The packet must be
 * in wire format */
net_status_t net_send(const pkt_t *pkt);

#endif
