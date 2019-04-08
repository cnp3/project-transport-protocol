#ifndef __TRANSMIT_H_
#define __TRANSMIT_H_

#include "../common/pktbuf.h"

/* Transmit the content of a file, buffered, over an opened socket */
int transmit(int input, pktbuf_t *buffer);

#endif
