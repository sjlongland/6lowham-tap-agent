/* SPDX-License-Identifier: GPL-2.0+ */
/* vim: set tw=78 ts=8 sts=8 noet fileencoding=utf-8: */
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netlink/netlink.h>
#include <netlink/cache.h>
#include <netlink/route/link.h>

#include "tap.h"
#include "tapinternal.h"

int slh_agent_tap_open(struct slh_agent_tap_ctx* const ctx) {
	int res = 0;
	struct ifreq ifr;
	struct nl_sock* sock;
	struct nl_cache* cache;
	struct nl_addr* lladdr;
	struct rtnl_link* link;
	struct rtnl_link* changedlink;

	slh_agent_tap_core_set_mtu(ctx);

	res = slh_agent_tap_core_alloc_buf(ctx, sizeof(struct tun_pi));
	if (res)
		goto exit;

	ctx->fd = open("/dev/net/tun", O_RDWR);
	if (ctx->fd < 0) {
		res = -errno;
		goto freebuf;
	}

	memset(&ifr, 0, sizeof(ifr));

	/* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
	 *        IFF_TAP   - TAP device  
	 *
	 *        IFF_NO_PI - Do not provide packet information  
	 */
	ifr.ifr_flags = IFF_TAP;

	/* Did we get a device name? */
	if (ctx->name[0])
		strncpy(ifr.ifr_name, ctx->name, IFNAMSIZ);

	res = ioctl(ctx->fd, TUNSETIFF, (void *) &ifr);
	if (res < 0) {
		res = -errno;
		goto closetap;
	}

	/* Set the device name */
	strncpy(ctx->name, ifr.ifr_name, sizeof(ctx->name));

	sock = nl_socket_alloc();
	if (!sock) {
		res = -ENOMEM;
		goto closetap;
	}

	res = nl_connect(sock, NETLINK_ROUTE);
	if (res) {
		res = -EIO;
		goto freenl;
	}

	res = rtnl_link_alloc_cache(sock, AF_UNSPEC, &cache);
	if (res) {
		res = -ENOMEM;
		goto closenl;
	}

	link = rtnl_link_get_by_name(cache, ctx->name);
	if (!link) {
		res = -ENODEV;
		goto closenl;
	}

	/* Grab the ifindex */
	ctx->ifindex = rtnl_link_get_ifindex(link);

	/* Allocate a link to record our changes */
	changedlink = rtnl_link_alloc();
	if (!changedlink) {
		res = -ENOMEM;
		goto putlink;
	}

	/* Are we setting the MAC address? */
	if (slh_agent_tap_core_has_macaddr(ctx)) {
		lladdr = nl_addr_build(
				AF_LLC,
				ctx->mac,
				sizeof(ctx->mac));
		if (!lladdr) {
			/* Failed to build/allocate? */
			res = -ENOMEM;
			goto putchangedlink;
		}

		rtnl_link_set_addr(changedlink, lladdr);
	} else {
		/* We're leaving it as-is */
		lladdr = rtnl_link_get_addr(link);
		if (!lladdr) {
			/* Failed to build/allocate? */
			res = -ENOMEM;
			goto putchangedlink;
		}
		if (nl_addr_get_len(lladdr) != sizeof(ctx->mac)) {
			res = -EMSGSIZE;
			goto putchangedlink;
		}
		memcpy(ctx->mac, nl_addr_get_binary_addr(lladdr),
				sizeof(ctx->mac));
	}

	rtnl_link_set_mtu(changedlink, ctx->mtu);
	rtnl_link_set_flags(changedlink, IFF_UP);

	/* Apply the changes */
	res = rtnl_link_change(sock, link, changedlink, 0);
	if (res) {
		/* Failed to set parameters */
		res = -EIO;
		goto putlladdr;
	}

putlladdr:
	nl_addr_put(lladdr);
putchangedlink:
	rtnl_link_put(changedlink);
putlink:
	rtnl_link_put(link);
closenl:
	nl_close(sock);
freenl:
	nl_socket_free(sock);
	if (!res)
		goto exit;
closetap:
	close(ctx->fd);
freebuf:
	slh_agent_tap_core_free_buf(ctx);
exit:
	return res;
}

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
		uint8_t* const buf, uint16_t buf_sz) {
	struct tun_pi info;
	ssize_t len = read(ctx->fd, ctx->buffer,
			ctx->mtu + sizeof(struct tun_pi));
	if (len < sizeof(info))
		return -errno;

	/* Inspect the packet info */
	memcpy(&info, ctx->buffer, sizeof(info));
	if (info.flags & TUN_PKT_STRIP) {
		/* Ethernet frame got truncated */
		return -EMSGSIZE;
	}
	len -= sizeof(info);

	if (len > buf_sz)
		/* It's too big for the buffer given */
		return -EMSGSIZE;

	/* Copy it out */
	memcpy(buf, &(ctx->buffer[sizeof(info)]), len);
	return len;
}

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
		const uint8_t* const buf, uint16_t buf_sz) {
	if (buf_sz > ctx->mtu)
		return -EMSGSIZE;

	/* Zero out the packet info */
	memset(ctx->buffer, 0, sizeof(struct tun_pi));

	/* Copy the destination buffer */
	memcpy(&(ctx->buffer[sizeof(struct tun_pi)]),
			buf, buf_sz);

	/* Include the size of the packet info header */
	buf_sz += sizeof(struct tun_pi);

	/* Write */
	if (write(ctx->fd, ctx->buffer, buf_sz) < buf_sz)
		return -errno;
	return 0;
}

/*!
 * Close the TAP interface.
 *
 * @param[inout]	ctx	TAP interface context
 *
 * @retval	0	Success
 */
int slh_agent_tap_close(struct slh_agent_tap_ctx* const ctx) {
	close(ctx->fd);
	slh_agent_tap_core_free_buf(ctx);
	return 0;
}
