#include "pktbuf.h"

#include <stdlib.h>

#include "macros.h"


#define mask_index(i, buf) ((i) & ((buf)->capacity - 1))
#define first_item(buf) mask_index((buf)->first, (buf))
#define last_item(buf) mask_index(((buf)->last - 1), (buf))
#define buf_get(idx, buf) buf->head[mask_index((idx), (buf))]
#define buf_get_first(buf) buf_get(first_item(buf), buf)
#define buf_get_last(buf) buf_get(last_item(buf), buf)


pktbuf_t * pktbuf_new(uint32_t capacity)
{
	pktbuf_t *buf;

	ASSERT_ALWAYS(__builtin_popcount(capacity) == 1,
			"%u is not a power of 2!", capacity);
	ASSERT_ALWAYS(capacity <= (UINT32_MAX >> 1), "%u is too big!",
			capacity);

	if (!(buf = calloc(1, sizeof(*buf) + capacity * sizeof(buf->head[0])))) {
		ERROR("Could not allocate memory for the packet buffer!");
		return NULL;
	}
	buf->capacity = capacity;
	return buf;
}

void pktbuf_free(pktbuf_t *buf)
{
	NOTNULL(buf);

	free(buf);
}

pkt_t *pktbuf_first(pktbuf_t *buf)
{
	NOTNULL(buf);

	return !pktbuf_empty(buf) ? &buf_get_first(buf) : NULL;
}

pkt_t *pktbuf_last(pktbuf_t *buf)
{
	NOTNULL(buf);

	return !pktbuf_empty(buf) ? &buf_get_last(buf) : NULL;
}

pkt_t *pktbuf_slotfor_seq(pktbuf_t *buf, uint8_t seqnum)
{
	uint8_t offset, first_seq;
	pkt_t *pkt;

	NOTNULL(buf);

	/* If the buffer is empty, the slot is the first one by definition,
	 * create it. */
	if (pktbuf_empty(buf)) {
		pktbuf_enqueue(buf)->seq = seqnum;
		DEBUG("Slot %u is the only one in the buffer", seqnum);
	}
	/* How far is that seqnum from the head ? */
	first_seq = pktbuf_first(buf)->seq;
	offset = seqnum - first_seq;
	ASSERT_ALWAYS(offset <= buf->capacity, "Cannot access %u from the head as "
			"it only has a capacity of %u and starts at %u", offset,
			buf->capacity, first_seq);
	/* Allocate slots if needed */
	if (!(offset < pktbuf_used(buf))) {
		DEBUG("Extending the buffer beyond %u to reach %u",
				pktbuf_last(buf)->seq, seqnum);
	}
	while (!(offset < pktbuf_used(buf))) {
		pkt = pktbuf_enqueue(buf);
		pkt->seq = first_seq + pktbuf_used(buf) - 1;
		DEBUG("Reserved slot for %u", first_seq + pktbuf_used(buf) - 1);
	}
	/* Return the slot */
	pkt = &buf_get(first_item(buf) + offset, buf);
	ASSERT(pkt->seq == seqnum,
			"The buffer has not been extended/updated properly!, "
			"[%u at %u's position]", pkt->seq, seqnum);
	return pkt;
}

pkt_t *pktbuf_at(pktbuf_t *buf, uint32_t idx)
{
	uint32_t masked_idx;

	NOTNULL(buf);
	masked_idx = mask_index(idx, buf);
	ASSERT_ALWAYS(masked_idx < pktbuf_used(buf),
			"Cannot access an out of bounds index %u [start: %u, capacity: %u,"
			" used: %u, masked: %u]", idx, first_item(buf), buf->capacity,
			pktbuf_used(buf), masked_idx);
	return &buf_get(idx, buf);
}

/* If these two functions were inlined as macros, they would cause undefined
 * behaviors, i.e. their implicit sequence point would be removed and things
 * such as pktbuf_enqueue(buf)->seq = 3 would be UB as a result ... */
pkt_t *pktbuf_dequeue(pktbuf_t * buf)
{
	NOTNULL(buf);
	ASSERT(!pktbuf_empty(buf), "Cannot dequeue in an empty buffer!");

	return &buf_get(buf->first++, buf);
}

pkt_t *pktbuf_enqueue(pktbuf_t *buf) {
	NOTNULL(buf);
	ASSERT(!pktbuf_full(buf), "Cannot enqueue in a full buffer!, used: %u, "
			"capacity: %d", pktbuf_used(buf), buf->capacity);

	return &buf_get(buf->last++, buf);
}
