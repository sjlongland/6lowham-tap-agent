/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8: */

#ifndef _6LH_AGENT_FRAME_H
#define _6LH_AGENT_FRAME_H

#include "tap.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

/*
 * Protocol definition:
 * 
 * - Frames will begin with the STX byte (0x02)
 * - The frame type is encoded as the second byte.
 * - The frame is terminated by the ETX byte (0x03)
 * - Any STX, ETX or DLE byte will be encoded as follows:
 *	STX (0x02) → DLE 'b'	(0x10 0x62)
 *	ETX (0x03) → DLE 'c'	(0x10 0x63)
 *	DLE (0x10) → DLE 'p'	(0x10 0x63)
 * - The following frame types are defined:
 *	SOH (0x01):	Device detail
 *	EOT (0x04):	End of session, shut down and exit.
 *	ACK (0x06):	Acknowledgement of last frame
 *	NAK (0x15):	Rejection of last frame
 *	SYN (0x16):	Keep-alive, no traffic to send
 *	FS (0x1c):	Ethernet frame
 * - Only one frame may be sent at a time, an ACK or NAK must be
 *   received in reply before the next may be sent.
 * - SYN may be used to poll the other side to see if it's still alive.
 *   An ACK should be the immediate response.
 * - The very first frame sent by this program will be a `SOH` frame which
 *   reports the details of the `tap` device created:
 *   - 6 bytes: hardware address (MAC)
 *   - 2 bytes: MTU (big-endian)
 *   - 4 bytes: interface index (big endian)
 *   - 1 byte: length of name field
 *   - N bytes: interface name
 */

#define SOH	((uint8_t)(0x01))
#define STX	((uint8_t)(0x02))
#define E_STX	((uint8_t)('b'))
#define ETX	((uint8_t)(0x03))
#define E_ETX	((uint8_t)('c'))
#define EOT	((uint8_t)(0x04))
#define ACK	((uint8_t)(0x06))
#define DLE	((uint8_t)(0x10))
#define E_DLE	((uint8_t)('p'))
#define NAK	((uint8_t)(0x15))
#define SYN	((uint8_t)(0x16))
#define FS	((uint8_t)(0x1c))

/*!
 * Frame to be transmitted or received
 */
struct slh_agent_frame {
	/*! Frame type */
	uint8_t		type;
	/*! Frame payload */
	uint8_t		payload[];
};

/*!
 * Reader/writer context
 */
struct slh_agent_frame_ctx {
	/*! Receive buffer */
	uint8_t* buffer;
	/*! Incoming data file descriptor */
	int rx_fd;
	/*! Outgoing data file descriptor */
	int tx_fd;
	/*! Size of receive buffer */
	uint16_t buffer_sz;
	/*! Location of read pointer */
	volatile uint16_t read_ptr;
	/*! Location of write pointer */
	volatile uint16_t write_ptr;
};

/*!
 * Initialise a frame reader/writer context.
 *
 * @param[inout]	ctx	Frame reader/writer context
 * @param[in]		rx_fd	Receive file descriptor
 * @param[in]		tx_fd	Transmit file descriptor
 * @param[in]		buf	Buffer pointer (or NULL to `malloc`)
 * @param[in]		buf_sz	Buffer size to initialise.
 *
 * @retval	0	Success
 * @retval	-EINVAL	Invalid parameters
 * @retval	-ENOMEM	Unable to allocate buffer
 */
int slh_agent_frame_init(struct slh_agent_frame_ctx* const ctx,
	int rx_fd, int tx_fd, uint8_t* buf, uint16_t buf_sz);

/*!
 * Read a frame from the peer process.
 *
 * @param[inout]	ctx	Frame reader context
 * @param[out]		frame	The frame to read the data into.
 * @param[in]		max_sz	Maximum size of the frame
 *
 * @returns	Size of frame read
 *
 * @retval	-EMSGSIZE	Message too big to fit into buffer.
 * @retval	-EBADMSG	Frame error occurred.
 * @retval	-EWOULDBLOCK	No (complete) frame waiting yet.
 */
int slh_agent_read_frame(struct slh_agent_frame_ctx* const ctx,
		struct slh_agent_frame* const frame,
		uint16_t max_sz);

/*!
 * Flush the frame waiting in the buffer.
 *
 * @param[inout]	ctx	Frame reader context
 *
 * @returns		Size of frame discarded.
 */
int slh_agent_drop_frame(struct slh_agent_frame_ctx* const ctx);

/*!
 * Write a frame to the peer process.
 *
 * @param[inout]	ctx	Frame writer context
 * @param[in]		frame	Frame to write.
 *
 * @retval		0	Success
 */
int slh_agent_write_frame(struct slh_agent_frame_ctx* const ctx,
		const struct slh_agent_frame* const frame,
		uint16_t frame_sz);

/*!
 * Send frame without payload.  (e.g. ACK, NAK or SYN)
 *
 * @param[inout]	ctx	Frame writer context
 * @param[in]		type	Payload type
 */
static inline int slh_agent_write_frame_nopayload(
		struct slh_agent_frame_ctx* const ctx,
		uint8_t type) {
	const struct slh_agent_frame frame = {
		.type = type
	};
	return slh_agent_write_frame(ctx, &frame, 1);
}

/*!
 * Send the device detail frame
 *
 * @param[inout]	ctx	Frame writer context
 * @param[in]		tap	TAP interface context
 *
 * @retval		0	Success
 */
static inline int slh_agent_write_device_detail_frame(
		struct slh_agent_frame_ctx* const ctx,
		const struct slh_agent_tap_ctx* const tap) {
	uint32_t ifindex = htonl(tap->ifindex);
	uint16_t mtu = htons(tap->mtu);
	uint8_t name_len = strlen(tap->name);
	uint8_t* ptr;
	union {
		struct slh_agent_frame header;
		uint8_t raw[	1			/* Frame type */
				+ SLH_TAP_MAC_SZ	/* MAC address */
				+ sizeof(uint16_t)	/* MTU */
				+ sizeof(uint32_t)	/* Index */
				+ 1			/* Name length */
				+ name_len		/* Name */
		];
	} frame;

	frame.header.type = SOH;

	ptr = frame.header.payload;
	memcpy(ptr, tap->mac, sizeof(tap->mac));
	ptr += sizeof(tap->mac);

	memcpy(ptr, &mtu, sizeof(mtu));
	ptr += sizeof(mtu);

	memcpy(ptr, &ifindex, sizeof(ifindex));
	ptr += sizeof(ifindex);

	*ptr = name_len;
	ptr++;

	memcpy(ptr, tap->name, name_len);
	return slh_agent_write_frame(ctx, &frame.header, sizeof(frame));
}

#endif
