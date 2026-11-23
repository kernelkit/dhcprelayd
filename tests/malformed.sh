#!/bin/sh
# Verify that the relay agent survives malformed requests when it adds
# option 82, and that it still relays after them:
#
#  - a request without the end option
#  - a request cut off in the BOOTP header
#  - a request with the end option beyond the standard 548 bytes, which
#    is valid and must get option 82 like any other request
#  - a request with a pad option between two options, which is valid
#    and must reach the server with all its options

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep python3

# Message type DISCOVER, and host name "pad"
DISCOVER=350101
NAME=0c03706164

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {}
interface c1 {}
EOF

server --dhcp-circuitid=set:port,c1
relay

print "Request without end option ..."
send c1 255.255.255.255 1 opts=$DISCOVER
alive "on a request without end option"

print "Request of 100 bytes ..."
send c1 255.255.255.255 1 size=100
alive "on a short request"

print "Request with end option after 600 bytes ..."
send c1 255.255.255.255 1 opts="$DISCOVER$(printf '00%.0s' $(seq 1 360))ff"
alive "on a long request"
tagged=$(server_count "tags: port")
echo "Server got $tagged requests with circuit-id c1, expected 1"
[ "$tagged" -eq 1 ] || { show_logs; FAIL; }

print "Request with pad option ..."
send c1 255.255.255.255 1 opts="${DISCOVER}00${NAME}ff"
alive "on a pad option"
names=$(server_count "client provides name: pad")
echo "Server got $names requests with host name pad, expected 1"
[ "$names" -eq 1 ] || { show_logs; FAIL; }

client c1 || { show_logs; FAIL "No lease after malformed requests"; }

OK
