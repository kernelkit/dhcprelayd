#!/usr/bin/env python3
# Send one hand made BOOTP/DHCP message from eth0, for the tests that
# need a message no DHCP client would send.
#
#   send.py DST OP [KEY=VALUE ...]
#
# DST is the destination address and OP 1 for a request, 2 for a reply.
# Keys: ciaddr, yiaddr, giaddr, chaddr (default the address of eth0),
# flags, sport, opts (options as hex, after the cookie) and size (cut
# the message to this many bytes).
import fcntl
import socket
import struct
import sys

dst, op = sys.argv[1], int(sys.argv[2])
kw = dict(arg.split('=', 1) for arg in sys.argv[3:])


def addr(key):
    return socket.inet_aton(kw.get(key, '0.0.0.0'))


sd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if 'chaddr' in kw:
    chaddr = bytes.fromhex(kw['chaddr'].replace(':', ''))
else:
    SIOCGIFHWADDR = 0x8927
    chaddr = fcntl.ioctl(sd, SIOCGIFHWADDR, struct.pack('256s', b'eth0'))[18:24]
msg = struct.pack('!BBBBIHH', op, 1, 6, 0, 0x1234, 0, int(kw.get('flags', '0'), 0))
msg += addr('ciaddr') + addr('yiaddr') + bytes(4) + addr('giaddr')
msg += chaddr.ljust(16, b'\0') + bytes(64 + 128)
msg += bytes.fromhex('63825363' + kw.get('opts', ''))
if 'size' in kw:
    msg = msg[:int(kw['size'])]

sd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sd.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sd.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, b'eth0')
sd.bind(('0.0.0.0', int(kw.get('sport', '68'))))
sd.sendto(msg, (dst, 67))
