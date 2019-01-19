#include "tap.h"
#include "frame.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>

/*!
 * Standard options
 */
const char* cmdline_opts = "a:m:n:";

int main(int argc, char* argv[]) {
	fd_set rfds;
	struct timeval tv;
	struct slh_agent_tap_ctx tap;
	int res;
	_Bool pending;

	/* Prepare TAP context */
	memset(&tap, 0, sizeof(tap));
	res = getopt(argc, argv, cmdline_opts);
	while (res != -1) {
		switch (res) {
		case 'a':
			/* Set the link-local address */
			{
				uint32_t val;
				char* ptr = optarg;
				char* saveptr;
				char* endptr = NULL;
				uint8_t idx = 0;

				ptr = strtok_r(optarg, ":", &saveptr);
				while ((idx < SLH_TAP_MAC_SZ) && ptr) {
					val = strtoul(ptr, &endptr, 16);
					if (endptr == ptr) {
						fprintf(stderr, "Could not parse MAC: %s\n",
								optarg);
						return 1;
					}
					if (val > UINT8_MAX) {
						fprintf(stderr, "Invalid MAC %s\n",
								optarg);
						return 1;
					}
					tap.mac[idx] = val;
					ptr = strtok_r(NULL, ":", &saveptr);
					idx++;
				}
			}
			break;
		case 'm':
			/* Set the MTU */
			{
				char* endptr = NULL;
				uint32_t mtu = strtoul(optarg, &endptr, 0);
				if (endptr == optarg) {
					fprintf(stderr, "Could not parse MTU: %s\n",
							optarg);
					return 1;
				}
				if (mtu > UINT16_MAX) {
					fprintf(stderr, "MTU too large: %u\n", mtu);
					return 1;
				}
				tap.mtu = mtu;
			}
			break;
		case 'n':
			/* Set the device name */
			strncpy(tap.name, optarg, SLH_TAP_NAME_SZ);
			break;
		default:
			fprintf(stderr, "Usage: %s [-m MTU] [-n NAME] [-a MAC]\n",
					argv[0]);
			return 1;
		}
		res = getopt(argc, argv, cmdline_opts);
	}

	/* Prepare control channel context */
	struct slh_agent_frame_ctx ctl;
	res = slh_agent_frame_init(&ctl, STDIN_FILENO,
			STDOUT_FILENO, NULL, 4096);
	if (res < 0) {
		fprintf(stderr, "Failed to initialise control channel: %s\n",
				strerror(-res));
		return 1;
	}

	/* Open a TAP device */
	res = slh_agent_tap_open(&tap);
	if (res < 0) {
		fprintf(stderr, "Failed to open device: %s\n",
				strerror(-res));
		return 1;
	}

	/* Drop privileges? */
	if (getuid() != geteuid()) {
		res = setuid(getuid());
		if (res < 0) {
			fprintf(stderr, "Failed to drop privileges: %s\n",
				strerror(-res));
			goto exit;
		}
	}

	/* Send the frame info */
	res = slh_agent_write_device_detail_frame(&ctl, &tap);
	if (res < 0) {
		fprintf(stderr, "Failed to send SOH frame: %s\n",
				strerror(-res));
		goto exit;
	}

	while (1) {
		union {
			uint8_t raw[tap.mtu + sizeof(struct slh_agent_frame)];
			struct slh_agent_frame header;
		} frame;

		/* Wait for the next frame (up to 5 seconds) */
		FD_ZERO(&rfds);
		FD_SET(ctl.rx_fd, &rfds);
		FD_SET(tap.fd, &rfds);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
		res = select(tap.fd + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			break;
		} else if (res > 0) {
			if (FD_ISSET(tap.fd, &rfds)) {
				/* We have an Ethernet frame */

				int len = slh_agent_tap_read(&tap,
						frame.header.payload,
						sizeof(frame.raw)
						- sizeof(struct slh_agent_frame));
				if (len < 0) {
					if (len == -EMSGSIZE)
						continue;
					else
						break;
				}

				if (pending) {
					continue;
				}

				/* Put the frame type in */
				frame.header.type = FS;
				res = slh_agent_write_frame(&ctl, &frame.header,
						len + sizeof(struct slh_agent_frame));
				if (!res) {
					pending = true;
				}
			} else if (FD_ISSET(ctl.rx_fd, &rfds)) {
				int len = slh_agent_read_frame(&ctl, &frame.header,
					sizeof(frame));
				if (len == -EBADMSG) {
					slh_agent_drop_frame(&ctl);
				} else if (len > 0) {
					if (frame.header.type == EOT)
						break;
					switch (frame.header.type) {
					case FS:
						/* Payload is an Ethernet frame */
						res = slh_agent_tap_write(
								&tap,
								frame.header.payload,
								len - sizeof(struct slh_agent_frame));
						if (res < 0) {
							slh_agent_write_frame_nopayload(&ctl, NAK);
						} else {
							slh_agent_write_frame_nopayload(&ctl, ACK);
						}
						break;
					case SYN:
						slh_agent_write_frame_nopayload(&ctl, ACK);
						break;
					case ACK:
					case NAK:
						pending = false;
						break;
					default:
						slh_agent_write_frame_nopayload(&ctl, NAK);
					}
				}
			}
		}
	}

exit:
	/* Close the TAP device */
	res = slh_agent_tap_close(&tap);
	if (res < 0) {
		return 1;
	}

	return 0;
}
