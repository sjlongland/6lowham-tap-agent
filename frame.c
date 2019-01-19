/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8: */

#include "frame.h"

#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef SLH_WRITE_BUF_SZ
#define SLH_WRITE_BUF_SZ	(256)
#endif

/*!
 * Return the number of bytes waiting to be read.
 */
static int slh_agent_frame_buf_waiting(
		struct slh_agent_frame_ctx* const ctx);

/*!
 * Read data into the buffer from the file descriptor.
 * Stop when we run out of data to read or space in the buffer.
 *
 * @returns	Number of bytes waiting in buffer.
 * @retval	<0		errno.h error
 */
static int slh_agent_frame_buf_fetch(
		struct slh_agent_frame_ctx* const ctx);

/*!
 * Seek `offset` bytes into the buffer and return the byte
 * at that offset.
 */
static uint8_t slh_agent_frame_buf_readbyte(
		struct slh_agent_frame_ctx* const ctx,
		uint16_t offset);

/*!
 * Dequeue `len` bytes from the buffer.
 */
static void slh_agent_frame_buf_dequeue(
		struct slh_agent_frame_ctx* const ctx,
		uint16_t len);

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
		int rx_fd, int tx_fd, uint8_t* buf, uint16_t buf_sz) {
	if (!buf) {
		buf = malloc(buf_sz);
		if (!buf)
			return -ENOMEM;
	}
	ctx->buffer = buf;
	ctx->buffer_sz = buf_sz;
	ctx->read_ptr = 0;
	ctx->write_ptr = 0;
	ctx->rx_fd = rx_fd;
	ctx->tx_fd = tx_fd;

	return 0;
}

int slh_agent_read_frame(struct slh_agent_frame_ctx* const ctx,
		struct slh_agent_frame* const frame,
		uint16_t max_sz) {
	uint16_t frame_sz = 0;
	uint8_t offset = 0;
	uint8_t* ptr = (uint8_t*)frame;

	/* See if anything is waiting */
	uint16_t rem = slh_agent_frame_buf_fetch(ctx);

	/* Read until we see STX */
	while (rem) {
		uint8_t byte = slh_agent_frame_buf_readbyte(ctx, offset);
		if (byte == STX)
			break;

		offset++;
		rem--;
	}

	/* Discard these initial bytes */
	slh_agent_frame_buf_dequeue(ctx, offset);

	/* If nothing left, bail */
	if (!rem) {
		if (offset)
			return -EBADMSG;
		else
			return 0;
	}

	/* Count the STX */
	offset = 1;
	rem--;

	/* Following this should be our frame */
	while (rem) {
		uint8_t byte = slh_agent_frame_buf_readbyte(ctx, offset);
		if (byte == ETX) {
			/* This is the end of the frame */
			offset++;

			/* Dequeue the now complete frame and exit */
			slh_agent_frame_buf_dequeue(ctx, offset);
			return frame_sz;
		} else if (byte == STX) {
			/* This is the start of another frame! */
			slh_agent_frame_buf_dequeue(ctx, offset);
			return -EBADMSG;
		} else if (byte == DLE) {
			/* This is a two-byte escape sequence */
			if (rem < 2)
				/* Only the DLE present, wait for the rest */
				return 0;

			byte = slh_agent_frame_buf_readbyte(ctx, offset+1);
			switch (byte) {
			case E_STX:
				byte = STX;
				break;
			case E_ETX:
				byte = ETX;
				break;
			case E_DLE:
				byte = DLE;
				break;
			default:
				/* Invalid sequence */
				slh_agent_frame_buf_dequeue(ctx, offset+2);
				return -EBADMSG;
			}
			offset += 2;
			rem -= 2;
		} else {
			/* This is a valid byte in the frame */
			offset++;
		}

		/* No more space for this byte */
		if (!max_sz)
			return -EMSGSIZE;

		*ptr = byte;
		ptr++;
		frame_sz++;
		max_sz--;
	}

	/* If we're here, then no ETX was seen. */
	return 0;
}

int slh_agent_drop_frame(struct slh_agent_frame_ctx* const ctx) {
	uint16_t rem = slh_agent_frame_buf_waiting(ctx);
	uint16_t num = 0;

	while (rem) {
		uint8_t byte = slh_agent_frame_buf_readbyte(ctx, num);
		num++;
		rem--;

		switch (byte) {
		case ETX:
			/* Drop everything up to the ETX */
			slh_agent_frame_buf_dequeue(ctx, num);
			return num;
		case STX:
			/* Drop everything up to just before STX */
			slh_agent_frame_buf_dequeue(ctx, num - 1);
			return num - 1;
		}
	}

	/* No data or all garbage. */
	ctx->read_ptr = 0;
	ctx->write_ptr = 0;
	return num;
}

int slh_agent_write_frame(struct slh_agent_frame_ctx* const ctx,
		const struct slh_agent_frame* const frame,
		uint16_t frame_sz) {
	const uint8_t* rptr = (const uint8_t*)frame;
	uint8_t buf[SLH_WRITE_BUF_SZ];
	uint8_t* wptr;
	uint16_t buf_sz = 0;
	_Bool stx_sent = false;
	_Bool etx_sent = false;

	/* Start with an STX byte */
	while (frame_sz) {
		wptr = buf;
		buf_sz = 0;

		if (!stx_sent) {
			*wptr = STX;
			wptr++;
			buf_sz++;
		}

		/* Fill up the write buffer */
		while (frame_sz) {
			if ((*rptr == STX)
					|| (*rptr == ETX)
					|| (*rptr == DLE)) {
				/* Escape sequence needed */
				if ((buf_sz + 1) >= sizeof(buf))
					break;

				wptr[0] = DLE;
				switch (*rptr) {
				case STX:
					wptr[1] = E_STX;
					break;
				case ETX:
					wptr[1] = E_ETX;
					break;
				case DLE:
					wptr[1] = E_DLE;
					break;
				}

				wptr += 2;
				buf_sz += 2;
			} else {
				/* Ordinary byte */
				if (buf_sz >= sizeof(buf))
					break;
				*wptr = *rptr;
				wptr++;
				buf_sz++;
			}
			frame_sz--;
			rptr++;
		}

		if (!frame_sz && (buf_sz < sizeof(buf))) {
			*wptr = ETX;
			buf_sz++;
			etx_sent = true;
		}

		/* Write out the buffer */
		if (write(ctx->tx_fd, buf, buf_sz) < buf_sz)
			return -errno;
	}

	if (!etx_sent) {
		/* Didn't squeeze the ETX in, send it now */
		buf[0] = ETX;
		if (write(ctx->tx_fd, buf, 1) < 1)
			return -errno;
	}

	return 0;
}

static int slh_agent_frame_buf_fetch(
		struct slh_agent_frame_ctx* const ctx) {
	fd_set rfds;
	struct timeval tv;

	/* How many bytes are spare? */
	uint16_t buf_rem;
	if (ctx->write_ptr <= ctx->read_ptr) {
		buf_rem = ctx->buffer_sz + ctx->read_ptr - ctx->write_ptr;
	} else {
		buf_rem = ctx->read_ptr - ctx->write_ptr;
	}

	/* Stop if there's no space */
	if (!buf_rem)
		return ctx->buffer_sz;

	/* See if there's data waiting */
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(ctx->rx_fd, &rfds);
	int res = select(ctx->rx_fd + 1, &rfds, NULL, NULL, &tv);
	if (res < 0) {
		return -errno;
	} else if (!res) {
		/* No new data */
		return 0;
	}

	/* Allocate some buffer space and read */
	uint8_t buf[buf_rem];
	const uint8_t* ptr = buf;
	ssize_t sz = read(ctx->rx_fd, buf, sizeof(buf));
	/* EAGAIN and EWOULDBLOCK are fine */
	if ((sz < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK))
		return -errno;

	while (sz) {
		uint16_t len;
		if (ctx->read_ptr <= ctx->write_ptr) {
			/* [  R        W-------] */
			len = ctx->buffer_sz - ctx->write_ptr;
		} else {
			len = ctx->read_ptr - ctx->write_ptr;
		}

		if (len > sz)
			len = sz;

		memcpy(&(ctx->buffer[ctx->write_ptr]),
				ptr,
				len);
		ptr += len;
		sz -= len;
		ctx->write_ptr = (ctx->write_ptr + len)
			% ctx->buffer_sz;
	}

	return slh_agent_frame_buf_waiting(ctx);
}

static int slh_agent_frame_buf_waiting(
		struct slh_agent_frame_ctx* const ctx) {
	if (ctx->read_ptr <= ctx->write_ptr) {
		return ctx->write_ptr - ctx->read_ptr;
	} else {
		return ctx->buffer_sz + ctx->write_ptr - ctx->read_ptr;
	}
}

static uint8_t slh_agent_frame_buf_readbyte(
		struct slh_agent_frame_ctx* const ctx,
		uint16_t offset) {
	return ctx->buffer[
		(ctx->read_ptr + offset)
			% ctx->buffer_sz];
}

static void slh_agent_frame_buf_dequeue(
		struct slh_agent_frame_ctx* const ctx,
		uint16_t len) {
	/* Clamp to amount remaining */
	uint16_t rem = slh_agent_frame_buf_waiting(ctx);
	if (len > rem)
		len = rem;

	ctx->read_ptr = (ctx->read_ptr + len)
		% ctx->buffer_sz;
}
