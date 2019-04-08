#ifndef __PKTBUF_H_
#define __PKTBUF_H_

#include "packet_interface.h"

/* first ... (last - 1)  last
 *   ^            ^       ^
 * [s0] ...     [smax] [snext]
 */
typedef struct pktbuf {
	uint32_t first; /* First used slot */
	uint32_t last; /* Next free slot */
	uint32_t capacity;
	pkt_t head[0];
} pktbuf_t;

/* Initializes a new buffer of the given capacity, must be a power of 2! */
pktbuf_t *pktbuf_new(uint32_t);
void pktbuf_free(pktbuf_t *);

/* Wether there is any slot filled */
#define pktbuf_empty(x) ((x)->first == (x)->last)
/* How many slots are filled */
#define pktbuf_used(x) ((x)->last - (x)->first)
/* How many slots are free */
#define pktbuf_freeslots(x) ((x)->capacity - pktbuf_used(x))
/* If all slots are filled */
#define pktbuf_full(x) (pktbuf_used(x) >= (x)->capacity)

/* The first packet in the buffer, NULL if empty */
pkt_t *pktbuf_first(pktbuf_t*);
/* The last packet in the buffer, NULL if empty */
pkt_t *pktbuf_last(pktbuf_t*);
/* Return the buffer slot for the requested sequence number, potentially
 * allocating slots if needed */
pkt_t *pktbuf_slotfor_seq(pktbuf_t*, uint8_t);
/* Return the slot for the given index,
 * undefined if the index is not within the bounds*/
pkt_t *pktbuf_at(pktbuf_t*, uint32_t);
/* Loop over each packet in the array (supports {} after the foreach thanks to
 * the inner loop that should be unrolled by the compiler) */
#define foreach_pktbuf(pbuf, pkt)\
	for (uint32_t _pktbuf_iter_idx = pbuf->first, _iter_keep = 1;\
			_pktbuf_iter_idx != pbuf->last;\
			++_pktbuf_iter_idx)\
		for (pkt_t *pkt = pktbuf_at(pbuf, _pktbuf_iter_idx); _iter_keep;\
				_iter_keep = 0)

/* Return the first item of the buffer and remove it from it, NULL if empty */
pkt_t *pktbuf_dequeue(pktbuf_t *);
/* Advance the tail of the buffer up by one */
pkt_t *pktbuf_enqueue(pktbuf_t *);

#endif /* __PKTBUF_H_ */
