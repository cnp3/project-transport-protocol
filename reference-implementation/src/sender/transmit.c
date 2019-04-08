#include "transmit.h"

#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>

#include "../common/macros.h"
#include "../common/packet_interface.h"
#include "../common/pktbuf.h"
#include "../common/net.h"


#define MAX_DUP_ACK 3
#define RETRANSMISSION_DELAY 4000
#define MAX_RETRANSMISSION 5


PRIVATE int input_fd; /* Input file */
PRIVATE pktbuf_t *send_buf; /* buffer storing all packets */
PRIVATE uint8_t last_ack = 0; /* Last received ack*/
PRIVATE uint8_t last_win = 1; /* Last received window */
PRIVATE uint8_t last_sent = -1; /* Last sent packet */
PRIVATE uint8_t last_chunk_read = -1; /* Last chunk seqnum of the input file */
PRIVATE uint8_t dup_ack = 0; /* Number of duplicate ACK's */
/* Number of successive Retransmission timer expiration */
PRIVATE int retry_count = 0;
PRIVATE ssize_t last_in_read = -1;


PRIVATE int process_nack(uint8_t nack)
{
  LOG("Received a NACK for seq #%u; retransmit packet", nack);
  int max_iter = pktbuf_used(send_buf);
  pkt_t *pkt;
  for (int i = 0; i < max_iter; i++) {
    pkt = pktbuf_at(send_buf, i);
    if (pkt->seq == nack)
      return (net_send(pkt) != NET_OK);
  }
  LOG("Cannot found packet #%u for retransmission...", nack);
  return 0;
}


PRIVATE int process_ack(uint8_t ack)
{
    LOG("Ack'ing %u packets [#%u -> #%u]", (uint8_t)(ack - last_ack),
			last_ack, ack);
    while (last_ack != ack) {
        /* Dequeue all ACK'ed packets */
        pktbuf_dequeue(send_buf);
        last_ack += 1;
    }
    dup_ack = 0;
    return 0;
}

PRIVATE int process_dup_ack(uint8_t ack)
{
    ++dup_ack;
    LOG("Duplicate ACK #%u [%d/%d]", ack, dup_ack, MAX_DUP_ACK);
    if (dup_ack == MAX_DUP_ACK) {
        dup_ack = 0;
        LOG("Fast retransmission for #%u", ack);
		return net_send(pktbuf_first(send_buf)) != NET_OK;
    }
    return 0;
}

PRIVATE int handle_socket_read()
{
	int err;
	uint8_t win;
	pkt_t pkt;

	/* The link is alive, remember it */
	retry_count = 0;
  /* Compute the window size, to discard old ACK's that have been delayed,
  * except the last one seen (as the corresponding data segment might
  * have been lost). */
  win = last_sent - last_ack + 1;
  /* Restrict the reception to a valid ACK or NACK */
  if ((err = net_recv_pkt(&pkt, last_ack, win)) != NET_OK)
    /* Propagate error if I/O related, ignore if dropped */
    return err == NET_ERROR;
  /* Sanity check*/
  if (pkt.type != PTYPE_ACK && pkt.type != PTYPE_NACK) {
    ERROR("Dropping wrong packet type [%u instead of %u or %u]",
      pkt.type, PTYPE_ACK, PTYPE_NACK);
    /* Do not propagate the error */
    return 0;
  }
	if (pkt.ts != PKT_TIMESTAMP) {
		ERROR("The receiver is corrupting the timestamp! [expected: %u,"
				" received: %u]", PKT_TIMESTAMP, pkt.ts);
	}
	if (last_win != pkt.window) {
		LOG("Updating receive window: %u -> %u", last_win, pkt.window);
		last_win = pkt.window;
	}
  /* Process the NACK */
  if (pkt.type == PTYPE_NACK)
    return process_nack(pkt.seq);
	/* Process the ACK */
  return (last_ack == pkt.seq) ?
		process_dup_ack(pkt.seq) : process_ack(pkt.seq);
}

PRIVATE int handle_input_read()
{
	/* Get the next sequence number */
	++last_chunk_read;
	/* Get its slot in the buffer */
	pkt_t *pkt = pktbuf_enqueue(send_buf);
	/* Fill the packet */
	pkt->type = PTYPE_DATA;
	pkt->window = 0;
	pkt->seq = last_chunk_read;
	pkt->ts = PKT_TIMESTAMP;
	if ((last_in_read = read(input_fd, pkt->payload, sizeof(pkt->payload)))
			== -1) {
		perror("Cannot read input stream");
		return -1;
	}
	pkt->length = last_in_read;
	LOG("Queued chunk #%u [%db]", pkt->seq, pkt->length);
	pkt_encode_inline(pkt);
	return 0;
}

/* The retransmission timer has expired, perform a go-back-n */
PRIVATE int handle_retransmission()
{
	uint8_t sseq;

	++retry_count;
	if (retry_count > MAX_RETRANSMISSION)
		goto_trace(bail, "Too many consecutive retransmission timeouts, "
				"aborting transfer");

    LOG("Retransmission timer expired, sending window [%u->%u]",
			last_ack, last_sent);
	for (sseq = last_ack; sseq != last_sent + 1; ++sseq) {
		pkt_t *pkt = pktbuf_slotfor_seq(send_buf, sseq);
		LOG("Resending %u", pkt->seq);
        if(net_send(pkt) != NET_OK)
			goto bail;
	}
    /* /1* Send all unack'ed packets *1/ */
	/* foreach_pktbuf(send_buf, pkt) { */
		/* LOG("Resending %u", pkt->seq); */
    /*     if(net_send(pkt) != NET_OK) */
			/* goto bail; */
    /* } */
    dup_ack = 0;
	return 0;

bail:
	return -1;
}

PRIVATE int can_send()
{
	return !pktbuf_empty(send_buf) &&
		(uint8_t)(last_sent + 1 - last_ack) < last_win;
}

PRIVATE int do_send_sbuf()
{
	pkt_t *pkt;

	while (last_sent != last_chunk_read && can_send()) {
		++last_sent;
		pkt = pktbuf_slotfor_seq(send_buf, last_sent);
		if (net_send(pkt))
			return -1;
	}
	return 0;
}

int transmit(int input_file, pktbuf_t *buffer)
{
	int err, pfds_count;
	struct pollfd pfds[2];
#define poll_file pfds[0]
#define poll_socket pfds[1]

	input_fd = input_file;
	send_buf = buffer;
	if (!pktbuf_empty(send_buf))
		printf("not empty\n");

	poll_file.fd = input_fd;
	poll_file.events = POLLIN;
	poll_socket.fd = net_fd;
	poll_socket.events = POLLIN;
	pfds_count = 2;
	do {
        err = poll(&pfds[sizeof(pfds) / sizeof(struct pollfd) - pfds_count],
				pfds_count, RETRANSMISSION_DELAY);
        if (err == -1)
            goto_errno(fail);
        else if (err > 0) {
			/* We first check the socket, to update the window to the latest
			 * received */
            if ((poll_socket.revents & (POLLIN | POLLERR | POLLHUP)) &&
					handle_socket_read())
                goto_trace(fail, "Cannot process the socket anymore");
			/* We read a chunk from the input file and encode it right away. */
            if ((poll_file.revents & (POLLIN | POLLERR | POLLHUP)) &&
					handle_input_read())
				goto fail;
			/* Try to send data if possible.
			 * We do not care if the socket is blocking when writing, as it is
			 * the only way to make progress in the connection. */
			if (do_send_sbuf())
				goto_trace(fail, "Cannot send new segments");
			/* Check wether we should poll the file again or not */
			if (last_in_read != 0 && !pktbuf_full(send_buf)) {
				/* Poll all fd's */
				pfds_count = 2;
			} else {
				/* Only poll the socket fd */
				pfds_count = 1;
				poll_file.revents = 0;
			}
			/* We optimistically always poll the socket (i.e. to handle
			 * unexpected acks */
		} else {
			/* Retransmission timeout expiration */
			if (!pktbuf_empty(send_buf) && handle_retransmission())
				goto fail;
		}
    } while (last_in_read != 0 || !pktbuf_empty(send_buf));
	/* Keep looping until we reach EOF on input and the send buf is empty */
	LOG("Transfert completed");

    return 0;

fail:
    return -ECONNABORTED;
}
