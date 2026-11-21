#!/bin/sh
# Verify remote-id-type giaddr: the server hands out a static address
# only to requests with remote-id "10.0.0.1", the gateway address.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {
    remote-id-type = giaddr
}
interface c1 {}
EOF

server --dhcp-remoteid=set:agent,10.0.0.1	\
       --dhcp-host=tag:agent,*:*:*:*:*:*,10.0.0.42
relay

client c1 || { show_logs; FAIL "No lease"; }

print "Analyzing ..."
addr=$(lease c1)
echo "c1 got $addr, expected 10.0.0.42"
[ "$addr" = "10.0.0.42" ] || { show_logs; FAIL; }

OK
