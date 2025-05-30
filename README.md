[Find this project on codeberg](https://codeberg.org/sjlongland/6lowham-tap-agent)

As a result of Github's decision to force AI-generated issues with no opt-out,
I am migrating personal projects of mine to Codeberg.  The project at Github
will be archived.

----

# 6LoWHAM TAP device agent

The purpose of this application is to open a TAP device on the Linux host
and pipe the Ethernet traffic between there and `stdin/stdout`.

It is intended to be run as a child process which then passes the Ethernet
frames to it.  It uses a simple byte-stuffed protocol to encapsulate the
Ethernet frames and maintain synchronisation with the parent process.

## Requirements

### Linux

* `libnl` version 3.x (tested against 3.4.0)
* Linux kernel headers in `/usr/include/linux`

### Others

Right now only Linux is supported.

There's scope there for supporting other OSes.  In theory we could support
MacOS X and BSD by implementing the `tun.h` interface for that OS.

Windows may be a bit tricky because they don't use a "file descriptor" type
model like Unix does.  I'm no Windows expert and consider it a legacy OS.  If
you can get it working, and have a patch to share, I'll include it on the
proviso that it doesn't break existing supported platforms.

## Command line arguments

* `-a`: Sets the MAC address to the provided colon-separated address.
* `-m`: Sets the MTU on the interface
* `-n`: Sets the interface name

## Framing format

* All frames start with a `STX` byte (ASCII `0x02`) and end with an `ETX` byte
  (ASCII `0x03`).
* Any `STX` byte within the frame is replaced with the sequence `DLE b`
  (`DLE` = ASCII `0x10`).
* Any `ETX` byte within the frame is replaced with the sequence `DLE c`.
* Any `DLE` byte within the frame is replaced with the sequence `DLE p`.
* The first byte of every frame gives the type of frame being sent.

## Frame types

### Device Detail (`SOH`; ASCII `0x01`)

The device detail is sent by the agent to the parent, and is the very first
frame sent.  It carries the following fields:

* 6 bytes: MAC address
* 2 bytes: MTU (big endian)
* 4 bytes: interface index (big endian)
* 1 byte: length of name field
* remainder: name field

This frame MUST be `ACK`ed by the parent on receipt.

### Exit agent (`EOT`; ASCII `0x04`)

This causes the agent to immediately shut down.  No `ACK` is returned.

### Keep-alive ping (`SYN`; ASCII `0x16`)

This may be used to see if the parent or child is "stuck".  Response should
be an immediate `ACK`.

### Ethernet frame data (`FS`; ASCII `0x1c`)

The payload is a raw Ethernet frame as it would be sent over the network.

If sent from the parent to the child, this tells the child to transmit that
frame.  It should `ACK` on success, or `NAK` if the send fails.

If sent by the child to the parent, this is an Ethernet frame that was
received.  It must be `ACK`ed or `NAK`ed by the parent.

## Acknowledgement (`ACK`; ASCII `0x06`) and Rejection (`NAK`; ASCII `0x15`)

These indicate successful processing of a frame, or rejection of a frame due to
an error in handling.
