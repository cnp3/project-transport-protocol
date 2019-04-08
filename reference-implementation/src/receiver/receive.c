#include "receive.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>

#include "../common/macros.h"
#include "../common/pktbuf.h"
#include "../common/packet_interface.h"
#include "../common/net.h"

#define IDLE_TIME 10000
#define INITIAL_SEQNUM 0
#define LINGER 3000
#define MAX_LINGER_RETRY 5


PUBLIC unsigned int max_window = MAX_WINDOW_SIZE;

/* Output file descriptor */
PRIVATE int out_fd;
/* Paquet buffer */
PRIVATE pktbuf_t *recv_buf;
/* Bitfield of out-of-sequence paquets relative to current buffer start */
PRIVATE uint32_t oos_mask = 0;
/* Next in-order sequence number */
PRIVATE uint8_t expected_seq = 0;
/* Last received timestamp */
PRIVATE uint32_t last_ts;
/* Whether we need to send an ACK or not */
PRIVATE int need_ack;
/* Whether we need to send an NACK or not */
PRIVATE int need_nack = 0;
/* The sequence number to be sent in the NACK */
PRIVATE uint8_t nack_seq;
/* Last written data on the disk */
PRIVATE int last_written_len = -1;

PRIVATE int rbuf_full()
{
	/* Could also be window_size() == 0 (albeit slower) */
	return __builtin_popcount(oos_mask) >= max_window;
}

PRIVATE int can_empty_rbuf()
{
	/* We can start emptying the receive buffer iff not empty and have a first
	 * packet in-sequence */
	return !pktbuf_empty(recv_buf) && (oos_mask & 1);
}

PRIVATE unsigned int window_size() {
	unsigned int in_seq_count = 0;
	uint32_t mask = oos_mask;

	/* Count the number of consecutive in-sequence packet */
	for (; mask & 1; mask >>= 1)
		++in_seq_count;

	return max_window - in_seq_count;
}

PRIVATE int send_ack()
{
	static pkt_t pkt = {
		.type = PTYPE_ACK,
		.length = 0,
		.payload = {0},
	};

	pkt.seq = expected_seq;
	pkt.ts = last_ts;
	pkt.window = window_size();
	pkt_encode_inline(&pkt);
	return net_send(&pkt);
}

PRIVATE int send_nack(uint8_t seq)
{
	static pkt_t pkt = {
		.type = PTYPE_NACK,
		.length = 0,
		.payload = {0},
	};

	pkt.seq = seq;
	pkt.ts = last_ts;
	pkt.window = window_size();
	pkt_encode_inline(&pkt);
	return net_send(&pkt);
}

PRIVATE int discard_incoming_data()
{
	char buf[PKT_MAX_LEN];
	/* Discard data from the socket buffer but report errors */
	if (read(net_fd, buf, sizeof(buf)) == - 1) {
		perror("Cannot process incoming data");
		return -1;
	}
	DEBUG("Discarded incoming data buf");
	return 0;
}

PRIVATE int process_incoming_pkt(pkt_t *pkt, unsigned int win)
{
	uint8_t gap, distance, received_seq;
	pkt_t *stored_pkt;

	DEBUG("Processing incoming packet #%u in window of %u", pkt->seq, win);
	last_ts = pkt->ts;
	/* Distance from the start of the buffer, to the expected one */
	distance = (oos_mask & 1) ? /* Do we have any packet in sequence? */
		(expected_seq - pktbuf_first(recv_buf)->seq - 1) : 0;
	/* Gap between the expected next sequence number, and the received one. */
	gap = pkt->seq - expected_seq;
	if (pkt->tr) {
		LOG("Packet #%u is truncated!", pkt->seq);
		need_nack = 1;
		nack_seq = pkt->seq;
		if (gap > 0) {
			/* Restore the seqnum on the first slot as its been erased */
			pkt->seq = expected_seq;
		}
		return 0;
	}
	oos_mask |= 1 << (distance + gap);
	if (gap > 0) {
		LOG("Received an out-of-sequence packet "
				"[#: %u, expected: %u, win: %d]", pkt->seq, expected_seq, win);
		received_seq = pkt->seq;
		/* Restore the seqnum on the first slot as its been erased */
		pkt->seq = expected_seq;
		/* Copy the packet further down in the buffer, the current copy will
		 * be overwritten when we receive the missing in-sequence packet */
		stored_pkt = pktbuf_slotfor_seq(recv_buf, received_seq);
		memcpy(stored_pkt, pkt, sizeof(*pkt));
		stored_pkt->seq = received_seq;
	} else {
		/* Increase the expected next sequence number taking into account
		 * possible out-of-order packets received earlier, as well as already
		 * ack'ed packets still present in the buffer. */
		expected_seq += max_window - window_size();
	}
	DEBUG("New expected seq: %u, new oos_mask: %u", expected_seq, oos_mask);
	return 0;
}

PRIVATE int do_receive_data()
{
	pkt_t *pkt;
	int err;
	unsigned int win;

	win = window_size();
	pkt = pktbuf_slotfor_seq(recv_buf, expected_seq);
	if ((err = net_recv_pkt(pkt, expected_seq, win)) != NET_OK) {
		/* Restore the buffer space seqnum */
		pkt->seq = expected_seq;
		/* Propagate any I/O error, ignore drops */
		return err == NET_ERROR;
	}
	if (pkt->type != PTYPE_DATA) {
		ERROR("Dropping wrong packet type [%u instead of %u]",
		pkt->type, PTYPE_DATA);
		/* Restore the buffer space seqnum */
		pkt->seq = expected_seq;
		/* Do not propagate the error */
		return 0;
	}
	return process_incoming_pkt(pkt, win);
}

PRIVATE int do_empty_rbuf()
{
	ssize_t err;
	pkt_t *pkt;

	while (oos_mask & 1) {
		ASSERT(!pktbuf_empty(recv_buf), "OOS mask cannot be full if the buffer "
				" is empty!");
		/* Get the first packet of the buffer */
		pkt = pktbuf_first(recv_buf);
		/* Track the payload len, as it indicates the end of the transfert
		 * if it is equals to 0 */
		if ((last_written_len = pkt->length) != 0) {
			/* Write it to disk */
			if ((err = write(out_fd, pkt->payload, pkt->length)) < 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN) {
					LOG("Cannot empty the receive buffer further as the output"
							" stream would block");
					return 0;
				}
				goto_trace(fail, "Error when writing the output file: %s",
						strerror(errno));
			} else if (err != pkt->length)
				goto_trace(fail, "Failed to write the complete packet #%u out "
						"[%ld vs %u]", pkt->seq, err, pkt->length);
			LOG("Wrote chunk #%u", pkt->seq);
		} else LOG("Chunk #%u indicates the end of the transfert.", pkt->seq);
		pktbuf_dequeue(recv_buf);
		oos_mask >>= 1;
	}
	return 0;

fail:
	return -1;
}

PRIVATE int do_read_sock()
{
	return !rbuf_full() ? do_receive_data() : discard_incoming_data();
}

PRIVATE int unblock_out_file()
{
	int flags;

	if ((flags = fcntl(out_fd, F_GETFD)) == -1)
		goto_errno(fail);
	flags |= O_NONBLOCK;
	if ((flags = fcntl(out_fd, F_SETFD, &flags)) == -1)
		goto_errno(fail);
	return 0;

fail:
	return -1;
}

int receive(int fd, pktbuf_t *rbuf)
{
	int err, retry, pfds_count;
	struct pollfd pfds[2];
#define poll_file pfds[0]
#define poll_socket pfds[1]

	recv_buf = rbuf;
	out_fd = fd;
	/* Because poll only tells us if the FD is in a ready state,
	 * we also need to make sure that our calls when writing to it won't block
	 * as well.
	 * This is not applicable for reading the socket as (i) UDP datagrams are
	 * guaranteed to be delivered in a single packet (ii) read is specified
	 * to return immediately if the FD is in a ready state, possibly returning
	 * less data than requested.*/
	if (unblock_out_file())
		goto_trace(fail, "Cannot set the output file as non-blocking");

	pkt_t *slot = pktbuf_enqueue(recv_buf);
	if (net_wait_and_connect(slot, INITIAL_SEQNUM))
		goto fail;
	process_incoming_pkt(slot, window_size());
	need_ack = 1;

	poll_file.fd = out_fd;
	poll_file.events = POLLOUT;
	poll_socket.fd = net_fd;
	poll_socket.events = POLLIN;
	pfds_count = 2;
	do {
        err = poll(&pfds[sizeof(pfds) / sizeof(struct pollfd) - pfds_count],
				pfds_count, IDLE_TIME);
		if (err < 0)
			goto_errno(fail);
		else if (err > 0) {
			/* Process incoming data */
			if ((poll_socket.revents & (POLLIN | POLLERR | POLLHUP))) {
				if (do_read_sock())
					goto_trace(fail, "Cannot read the socket");
				/* Schedule an ACK to help to resync the sender, if the packet was not truncated */
				if (!need_nack)
					need_ack = 1;
			}
			/* Free up buffer space as much as possible. Either because we
			 * still had data to write after the last poll, or because the
			 * received packet was in-sequence. */
			if (((poll_file.revents & (POLLOUT | POLLERR | POLLHUP)) ||
						can_empty_rbuf()) &&
					do_empty_rbuf())
				goto_trace(fail, "Cannot write the received data");
			/* Send an ACK with the updated window size */
			if (need_ack && send_ack())
				goto_trace(fail, "Could not send an ACK packet");
			/* Send a NACK with the needed sequence number */
			if (need_nack && send_nack(nack_seq))
				goto_trace(fail, "Could not send a NACK packet");
			/* Poll the file for writing iff we have data to write */
			if (can_empty_rbuf())
				/* Poll all fd's */
				pfds_count = 2;
			else {
				/* Only poll the socket fd */
				pfds_count = 1;
				poll_file.revents = 0;
			}
			/* We unconditionally check the socket to send 0-sized window
			 * if the output file is blocking */
		} else
			goto_trace(fail, "No I/O acivity in the last %.1fs, aborting "
					"transfert!", IDLE_TIME / 1000.0);
		/* Don't send an ACK unless we received data */
		need_ack = 0;
		/* Don't send a NACK unless we received truncated data */
		need_nack = 0;
	} while (last_written_len != 0 || !pktbuf_empty(recv_buf));

	LOG("Sending last ACK #%u", expected_seq);
	retry = 0;
	while ((err = poll(&poll_socket, 1, LINGER)) != 0 &&
			retry < MAX_LINGER_RETRY) {
		if (err < 0)
			goto_errno(fail);
		if (send_ack())
			goto_trace(fail, "Could not send the final ACK packet");
		++retry;
	}
	if (retry == MAX_LINGER_RETRY)
		ERROR("Could not successfully send an ACK after %d tries!",
				MAX_LINGER_RETRY);

	return 0;

fail:
	return -ECONNABORTED;
}
