/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8: */

#ifndef _6LH_AGENT_TAP_H
#define _6LH_AGENT_TAP_H

#include <errno.h>
#include <stdint.h>

/*! Maximum size of a device interface name */
#define SLH_TAP_NAME_SZ	(16)

/*! Size of a MAC address */
#define SLH_TAP_MAC_SZ	(6)

/*!
 * TAP interface context
 */
struct slh_agent_tap_ctx {
	/*!
	 * Receive buffer, must be mtu + 4 bytes in size.
	 *
	 * If this is NULL when slh_agent_tap_open is called, a new one
	 * will be `malloc`'d.  Otherwise the pointer given is assumed
	 * to point to a big-enough one.
	 */
	uint8_t* buffer;

	/*!
	 * Interface name.  If the first byte is non-zero, it is assumed
	 * that a `tap` interface with this name already exists and we should
	 * attempt to connect to it.
	 *
	 * The name field will be set to the name of the `tap` interface
	 * created or used.
	 */
	char name[SLH_TAP_NAME_SZ];

	/*!
	 * Interface MAC.  If set to non-zero values, the MAC address will
	 * be set to the given value.
	 *
	 * The value stored will be the MAC address of the interface.
	 */
	uint8_t mac[SLH_TAP_MAC_SZ];

	/*!
	 * Interface MTU.  This decides the size of the buffer needed and
	 * determines the MTU of the interface itself.  If left at 0, an MTU
	 * of 1280 is assumed.
	 */
	uint16_t mtu;

	/*!
	 * File descriptor.  This will receive the file descriptor of the
	 * `tap` interface when it is opened.
	 */
	int fd;

	/*!
	 * Interface index.  The interface index according to the TCP/IP
	 * stack.  Used when sending link-local IPv6 frames.
	 */
	int ifindex;

	/*! Flags: For internal use only */
	uint32_t flags;
};

/*!
 * Open the TAP interface.
 *
 * @param[inout]	ctx	TAP interface context
 *
 * @retval	0	Success
 */
int slh_agent_tap_open(struct slh_agent_tap_ctx* const ctx);

/*!
 * Read an Ethernet frame from the TAP interface.
 *
 * @param[inout]	ctx	TAP interface context
 * @param[out]		buf	Output buffer to write frame
 * @param[in]		buf_sz	Size of buffer
 *
 * @returns	Size of frame written to buffer
 */
int slh_agent_tap_read(struct slh_agent_tap_ctx* const ctx,
		uint8_t* const buf, uint16_t buf_sz);

/*!
 * Write an Ethernet frame to the TAP interface.
 *
 * @param[inout]	ctx	TAP interface context
 * @param[in]		buf	Input buffer to read frame
 * @param[in]		buf_sz	Size of buffer
 *
 * @retval	0	Success
 */
int slh_agent_tap_write(struct slh_agent_tap_ctx* const ctx,
		const uint8_t* const buf, uint16_t buf_sz);

/*!
 * Close the TAP interface.
 *
 * @param[inout]	ctx	TAP interface context
 *
 * @retval	0	Success
 */
int slh_agent_tap_close(struct slh_agent_tap_ctx* const ctx);

#endif
