#!/bin/sh
# Verify a binary circuit-id and remote-id, set with circuit-id-hex and
# remote-id-hex, including a NUL byte.  The server hands out a static
# address only to requests with both.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {
    remote-id-hex = c0:ff:ee
}
interface c1 {
    port c1 {
        circuit-id-hex = 00:01:ff
    }
}
EOF

server --dhcp-circuitid=set:port,00:01:ff --dhcp-remoteid=set:agent,c0:ff:ee	\
       --dhcp-host=tag:port,tag:agent,*:*:*:*:*:*,10.0.0.42
relay

client c1 || { show_logs; FAIL "No lease"; }

print "Analyzing ..."
addr=$(lease c1)
echo "c1 got $addr, expected 10.0.0.42"
[ "$addr" = "10.0.0.42" ] || { show_logs; FAIL; }

OK
