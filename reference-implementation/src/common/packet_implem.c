#include "packet_interface.h"

#include <stdlib.h>    /* malloc, free */
#include <string.h>    /* memcpy */
#include <arpa/inet.h> /* ntohx, htonx */
#include <zlib.h>      /* crc32 */

#include "macros.h"

pkt_t* pkt_new() { return calloc(1, sizeof(pkt_t)); }
void pkt_del(pkt_t *pkt)
{
	if (pkt)
		free(pkt);
}

PRIVATE int valid_length(uint16_t l) { return l <= MAX_PAYLOAD_SIZE; }
PRIVATE int valid_window(uint8_t w) { return w <= MAX_WINDOW_SIZE; }

PRIVATE int valid_type(size_t t)
{
	return t == PTYPE_DATA || t == PTYPE_ACK || t == PTYPE_NACK;
}

PRIVATE int valid_tr(size_t t, uint8_t tr) {
	return tr == 0 || t == PTYPE_DATA;
}

PRIVATE uint32_t crc_of(const char *data, size_t len)
{
	uLong crc = crc32(0L, Z_NULL, 0);
    return crc32(crc, (const Bytef*)data, len);
}

pkt_status_code pkt_decode(const char *data, const size_t len, pkt_t *pkt)
{
	VALIDIF(len <= sizeof(*pkt), E_UNCONSISTENT,
			"[received length: %lu, max: %lu]", len, sizeof(*pkt));
	memcpy(pkt, data, len);
	return pkt_decode_inline(pkt, len);
}

pkt_status_code pkt_decode_inline(pkt_t *pkt, size_t rlen)
{
	size_t plen, payload_len = 0, payload_crc2_len;
	uint32_t crc1, computed_crc1;
	uint32_t crc2, computed_crc2;

	VALIDIF(rlen >= PKT_MIN_LEN, E_NOHEADER, "[%lu < %lu]", rlen, PKT_MIN_LEN);
	plen = ntohs(pkt->length);
	/* Check header sanity */
	VALIDIF(valid_type(pkt->type), E_TYPE, "[%u]", pkt->type);
	VALIDIF(valid_tr(pkt->type, pkt->tr), E_TR, "[Type %u TR %u]", pkt->type,
			pkt->tr);
	VALIDIF(valid_length(plen), E_LENGTH, "[%lu <= %u]", plen,
			MAX_PAYLOAD_SIZE);
	/* Check CRC1 */
	/* We first set the TR bit to 0 */
	uint8_t tr = pkt->tr;
	pkt->tr = 0;
	crc1 = ntohl(*(uint32_t*)&((char *)pkt)[PKT_HEADERLEN - sizeof(pkt->crc1)]);
	computed_crc1 = crc_of((const char *)pkt, PKT_HEADERLEN - sizeof(pkt->crc1));
	VALIDIF(crc1 == computed_crc1, E_CRC, "[CRC1: computed: %u, found: %u]",
			computed_crc1, crc1);
	pkt->crc1 = crc1;
	/* Don't forget to put TR back */
	pkt->tr = tr;

	/* Compute the payload size (including padding) */
	payload_crc2_len = rlen - PKT_HEADERLEN;
	if (payload_crc2_len)
		payload_len = payload_crc2_len - PKT_FOOTERLEN;
	/* Handle eventual payload */
	switch (pkt->type) {
		case PTYPE_DATA: {
				if (pkt->tr) {
					/* Check absence of payload */
					VALIDIF(!payload_len, E_UNCONSISTENT,
						"[PTYPE_DATA, got tr bit, but found payload length: %lu]",
						payload_len);
				} else {
					/* Check consistency with announced payload length */
					VALIDIF(plen == payload_len, E_UNCONSISTENT,
							"[PTYPE_DATA, computed length: %lu, found: %lu, read: %lu]",
							payload_len, plen, rlen);
				}
				if (payload_len) {
					/* Check CRC2 */
					crc2 = ntohl(*(uint32_t*)&((char *)pkt)[rlen - PKT_FOOTERLEN]);
					computed_crc2 = crc_of((const char *)pkt->payload, payload_len);
					VALIDIF(crc2 == computed_crc2, E_CRC, "[CRC2: computed: %u, found: %u]",
							computed_crc2, crc2);
					pkt->crc2 = crc2;
				}
				break;
			}
		case PTYPE_ACK:
			/* Fallthrough */
		case PTYPE_NACK:
			/* Fallthrough */
		default:
			/* Check absence of data */
			VALIDIF(!payload_len && !plen, E_UNCONSISTENT,
					"[type %u, computed length: %lu, found: %lu]",
					pkt->type, payload_len, plen);
			break;
	}
	/* Modifiy the received data at the very end */
	pkt->length = plen;
	return PKT_OK;
}

pkt_status_code pkt_encode(const pkt_t* pkt, char *buf, size_t *len)
{
	/* Check that the given buffer is long enough */
	PRECONDITION(len != NULL && buf != NULL, E_NOMEM);
	size_t target_len = pkt_len(pkt);
	PRECONDITION(*len >= target_len, E_NOMEM);
	*len = target_len;
	memcpy(buf, pkt, target_len - PKT_FOOTERLEN);
	return pkt_encode_inline((pkt_t*)buf);
}

pkt_status_code pkt_encode_inline(pkt_t* pkt)
{
	uint32_t *crc1, *crc2;
	size_t plen = pkt->length;
	int offset1 = PKT_HEADERLEN - PKT_CRC1LEN;
	int offset2 = PKT_HEADERLEN + pkt->length;
	/* Correct endianness of the length field */
	pkt->length = htons(pkt->length);
	/* Set CRC1 value on pseudo-header */
	uint8_t tr = pkt->tr;
	pkt->tr = 0;
	crc1 = (uint32_t*)&((char *)pkt)[offset1];
	*crc1 = htonl(crc_of((const char *)pkt, offset1));
	pkt->tr = tr;
	if (plen) {
		/* Set CRC2 value */
		crc2 = (uint32_t*)&((char *)pkt)[offset2];
		*crc2 = htonl(crc_of((const char *)pkt->payload, plen));
	}
	return PKT_OK;
}

#define ACCESSOR(field) (const pkt_t *pkt) { return pkt ? pkt->field : 0; }
ptypes_t    pkt_get_type      ACCESSOR (type)
uint8_t     pkt_get_tr        ACCESSOR (tr)
uint8_t     pkt_get_window    ACCESSOR (window)
uint8_t     pkt_get_seqnum    ACCESSOR (seq)
uint16_t    pkt_get_length    ACCESSOR (length)
const char* pkt_get_payload   ACCESSOR (payload)
uint32_t    pkt_get_timestamp ACCESSOR (ts)
uint32_t    pkt_get_crc1      ACCESSOR (crc1)
uint32_t    pkt_get_crc2      ACCESSOR (crc2)

#define SETTER(x, field, val) do { \
		PRECONDITION(x != NULL, E_NOMEM); \
		x->field = val; \
		return PKT_OK; \
	} while (0)

pkt_status_code pkt_set_type(pkt_t *pkt, const ptypes_t type)
{
	PRECONDITION(valid_type(type), E_TYPE);
	SETTER(pkt, type, type);
}

pkt_status_code pkt_set_tr(pkt_t *pkt, const uint8_t tr)
{
	PRECONDITION(valid_tr(pkt->type, tr), E_TR);
	SETTER(pkt, tr, tr);
}

pkt_status_code pkt_set_window(pkt_t *pkt, const uint8_t window)
{
	PRECONDITION(valid_window(window), E_WINDOW);
	SETTER(pkt, window, window);
}

pkt_status_code pkt_set_seqnum(pkt_t *pkt, const uint8_t seqnum)
{
	SETTER(pkt, seq, seqnum);
}

pkt_status_code pkt_set_length(pkt_t *pkt, const uint16_t length)
{
	PRECONDITION(valid_length(length), E_LENGTH);
	SETTER(pkt, length, length);
}

pkt_status_code pkt_set_timestamp(pkt_t *pkt, const uint32_t ts)
{
	SETTER(pkt, ts, ts);
}

pkt_status_code pkt_set_crc1(pkt_t *pkt, const uint32_t crc1)
{
	SETTER(pkt, crc1, crc1);
}

pkt_status_code pkt_set_crc2(pkt_t *pkt, const uint32_t crc2)
{
	SETTER(pkt, crc2, crc2);
}

pkt_status_code pkt_set_payload(pkt_t *pkt,
							    const char *data,
								const uint16_t length)
{
	/* Set and validate the proposed payload length */
	pkt_status_code s = pkt_set_length(pkt, length);
	PRECONDITION(s == PKT_OK, s);
	/* Copy the payload */
	memcpy(pkt->payload, data, length);
	return PKT_OK;
}

const char* pkt_err_code(pkt_status_code code)
{
	switch(code) {
		case PKT_OK:
			return "[OK] The packet has been processed successfully";
		case E_TYPE:
			return "[E_TYPE] Invalid packet type";
		case E_TR:
			return "[E_TR] Invalid tr value";
		case E_LENGTH:
			return "[E_LENGTH] Invalid packet length";
		case E_CRC:
			return "[E_CRC] Invalid packet crc";
		case E_WINDOW:
			return "[E_WINDOW] Invalid packet window";
		case E_SEQNUM:
			return "[E_SEQNUM] Invalid packet sequence number";
		case E_NOMEM:
			return "[E_NOMEM] Not enough memory";
		case E_NOHEADER:
			return "[E_NOHEADER] Packet has no header";
		case E_UNCONSISTENT:
			return "[E_UNCONSISTENT] Packet is unconsistent";
		default:
			return "Unknown Error ...";
	}
}

const char* pkt_type_descr(ptypes_t type)
{
#define explain(t) case t: return "["#t"]"
	switch(type) {
		explain(PTYPE_DATA);
		explain(PTYPE_ACK);
		explain(PTYPE_NACK);
		default: return "Unknown type";
	}
#undef explain
}
