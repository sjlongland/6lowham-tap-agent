/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8: */

#ifndef _6LH_AGENT_TAPINTERNAL_H
#define _6LH_AGENT_TAPINTERNAL_H

#include "tap.h"
#include <stdlib.h>
#include <stdbool.h>

#ifndef SLH_AGENT_TAP_DEFAULT_MTU
#define SLH_AGENT_TAP_DEFAULT_MTU	(1280)
#endif

/*!
 * Free the buffer on close.  This indicates it was us that malloc'd the
 * buffer in the first place.
 */
#define SLH_AGENT_TAP_FLAG_FREEBUF	(1 << 0)

/*!
 * Set the MTU if not already set.
 */
static inline void slh_agent_tap_core_set_mtu(
		struct slh_agent_tap_ctx* const ctx) {
	if (!(ctx->mtu))
		ctx->mtu = SLH_AGENT_TAP_DEFAULT_MTU;
}

/*!
 * Allocate a buffer, if one not already provided.
 *
 * @param	ctx	TAP device context.
 * @param	extra	Number of extra bytes on top of MTU.
 */
static inline int slh_agent_tap_core_alloc_buf(
		struct slh_agent_tap_ctx* const ctx,
		size_t extra) {
	if (!(ctx->buffer)) {
		ctx->buffer = malloc(ctx->mtu + extra);
		if (!ctx->buffer)
			return -ENOMEM;

		ctx->flags = SLH_AGENT_TAP_FLAG_FREEBUF;
	}
	return 0;
}

/*!
 * Free the buffer, if we allocated it.
 */
static inline void slh_agent_tap_core_free_buf(
		struct slh_agent_tap_ctx* const ctx) {
	if (ctx->flags & SLH_AGENT_TAP_FLAG_FREEBUF) {
		free(ctx->buffer);
		ctx->buffer = NULL;
	}
}

/*!
 * Return true if the MAC address is set
 */
static inline _Bool slh_agent_tap_core_has_macaddr(
		struct slh_agent_tap_ctx* const ctx) {
	uint8_t i;
	for (i = 0; i < sizeof(ctx->mac); i++) {
		if (ctx->mac[i])
			return true;
	}
	return false;
}

#endif
