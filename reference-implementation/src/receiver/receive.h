#ifndef __SRC_RECEIVER_RECEIVE_H__
#define __SRC_RECEIVER_RECEIVE_H__

#include "../common/pktbuf.h"

/* Maximal window size that can be announced */
extern unsigned int max_window;

/* Receive the file and write it to the given file descriptor, using
 * rbuf to store out-of-order packets. */
int receive(int fd, pktbuf_t *rbuf);

#endif
