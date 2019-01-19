#!/usr/bin/env python3
# vim: set tw=78 et sw=4 ts=4 sts=4 fileencoding=utf-8:
# SPDX-License-Identifier: GPL-2.0

"""
This is a quick and dirty packet dump tool written around the
`6lhagent` utility as a proof-of-concept for interfacing an `asyncio` Python
application to `6lhagent`.
"""

import asyncio
import binascii
import struct

class SixLowHAMAgentProtocol(asyncio.SubprocessProtocol):
    # Byte definitions
    SOH     = b'\x01'
    STX     = b'\x02'
    E_STX   = b'b'
    ETX     = b'\x03'
    E_ETX   = b'c'
    EOT     = b'\x04'
    ACK     = b'\x06'
    DLE     = b'\x10'
    E_DLE   = b'p'
    NAK     = b'\x15'
    SYN     = b'\x16'
    FS      = b'\x1c'

    SOH_STRUCT = struct.Struct('>6BHLB')
    ETHERNET_HDR = struct.Struct('>6B6BH')
    IPV6_HDR = struct.Struct('>BBHHBB8H8H')

    def __init__(self):
        self._buffer = b''
        self._transport = None
        self._mac = None
        self._name = None
        self._ifidx = None
        self._mtu = None

    def set_transport(self, transport):
        self._transport = transport

    def pipe_data_received(self, fd, data):
        self._buffer += data

        # Process all pending frames
        framestart = self._buffer.find(self.STX)
        while framestart >= 0:
            frameend = self._buffer.find(self.ETX, framestart)
            if frameend < 0:
                break

            self._process_raw_frame(self._buffer[framestart+1:frameend])
            self._buffer = self._buffer[frameend+1:]

    def _process_raw_frame(self, rawframe):
        frame = rawframe.replace(self.DLE + self.E_STX, self.STX)
        frame = frame.replace(self.DLE + self.E_ETX, self.ETX)
        frame = frame.replace(self.DLE + self.E_DLE, self.DLE)
        self._process_frame(frame)

    def _process_frame(self, frame):
        frametype = frame[0:1]
        framedata = frame[1:]

        if frametype == self.SOH:
            print ('Interface data: %r' % framedata)
            (mac0, mac1, mac2, mac3, mac4, mac5, \
                    mtu, ifidx, name_len) = self.SOH_STRUCT.unpack(\
                    framedata[0:self.SOH_STRUCT.size])
            self._mac = [mac0, mac1, mac2, mac3, mac4, mac5]
            self._mtu = mtu
            self._ifidx = ifidx
            self._name = framedata[-name_len:].decode()
            print ('Interface: MAC=%s MTU=%s IDX=%s NAME=%s' % (
                self._mac, self._mtu, self._ifidx, self._name))
        elif frametype == self.FS:
            print ('Ethernet traffic: %s' % \
                    binascii.b2a_hex(framedata))
            (dm0,dm1,dm2,dm3,dm4,dm5, \
                    sm0,sm1,sm2,sm3,sm4,sm5,\
                    proto) = self.ETHERNET_HDR.unpack(
                            framedata[0:self.ETHERNET_HDR.size])
            print ('From: %02x:%02x:%02x:%02x:%02x:%02x' % \
                    (dm0,dm1,dm2,dm3,dm4,dm5))
            print ('To:   %02x:%02x:%02x:%02x:%02x:%02x' % \
                    (sm0,sm1,sm2,sm3,sm4,sm5))
            print ('Protocol: %04x' % proto)

            eth_payload = framedata[self.ETHERNET_HDR.size:]
            if proto == 0x86dd:
                (version_priority, flow_hi, flow_lo,
                        length, next_hdr, hop_limit,
                        src0, src1, src2, src3, src4, src5, src6, src7,
                        dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7) \
                                = self.IPV6_HDR.unpack(\
                                    eth_payload[0:self.IPV6_HDR.size])
                print ('IPv6: Priority %d, Flow %02x%04x' % (\
                        version_priority & 0x0f,
                        flow_hi, flow_lo))
                print ('From: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x' \
                        % (src0, src1, src2, src3, src4, src5, src6, src7))
                print ('To:   %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x' \
                        % (dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7))
                print ('Length: %d, Next header: %d, Hop Limit: %d' % (\
                        length, next_hdr, hop_limit))
        else:
            print ('Unknown frame type %r, data %r' \
                    % (frametype, framedata))

        if frametype in (self.SOH, self.FS, self.SYN):
            self.send_frame(self.ACK)
        else:
            self.send_frame(self.NAK)

    def send_frame(self, frame):
        frame = frame.replace(self.DLE, self.DLE + self.E_DLE)
        frame = frame.replace(self.STX, self.DLE + self.E_STX)
        frame = frame.replace(self.ETX, self.DLE + self.E_ETX)

        self._transport.get_pipe_transport(0).write(
                self.STX + frame + self.ETX)

@asyncio.coroutine
def interactive_popen():
    (transport, protocol) = yield from \
            asyncio.get_event_loop().subprocess_exec(
                    lambda : SixLowHAMAgentProtocol(),
                    '6lhagent',
                    stdin=asyncio.subprocess.PIPE,
                    stdout=asyncio.subprocess.PIPE)
    protocol.set_transport(transport)
    
    try:
        while transport.get_returncode() is None:
            yield from asyncio.sleep(1)
    except ConnectionResetError:
        pass
        
    
if __name__ == '__main__':
    loop = asyncio.get_event_loop()
loop.run_until_complete(interactive_popen())
