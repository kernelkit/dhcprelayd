#!/bin/sh
# Verify that a request with a client hardware address that is not the
# Ethernet source address of the frame is dropped: the server gets no
# request, and the address is not added to the FDB of the port.  From a
# trusted port the request is relayed, still without an FDB entry.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep python3

MAC=02:de:ad:be:ef:01
DISCOVER=350101ff

print "Creating world ..."
topo bridged

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
interface vlan1 {
    port c1 {}
    port c2 {
        trusted = true
    }
}
EOF

server
relay

print "Request from c1 ..."
send c1 255.255.255.255 1 chaddr=$MAC opts=$DISCOVER
got=$(server_count "DHCPDISCOVER(.* $MAC")
echo "Server got $got DISCOVER for $MAC, expected 0"
[ "$got" -eq 0 ] || { show_logs; FAIL; }

print "Request from c2, trusted ..."
send c2 255.255.255.255 1 chaddr=$MAC opts=$DISCOVER
got=$(server_count "DHCPDISCOVER(.* $MAC")
echo "Server got $got DISCOVER for $MAC, expected 1"
[ "$got" -eq 1 ] || { show_logs; FAIL; }

fdb=$(bridge fdb show br br0 | grep -c "^$MAC ")
echo "FDB has $fdb entries for $MAC, expected 0"
[ "$fdb" -eq 0 ] || { bridge fdb show br br0; show_logs; FAIL; }

OK
