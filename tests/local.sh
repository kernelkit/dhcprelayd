#!/bin/sh
# Verify a DHCP server on the same system as the relay agent: dnsmasq
# in the relay agent's namespace, reached over loopback, hands out the
# static address it has for each port's circuit-id.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo bridged
ip link set lo up

cat <<EOF > "$DIR/relay.conf"
server 127.0.0.1 {}
option82 {}
interface vlan1 {
    port c1 {}
    port c2 {}
}
EOF

print "Starting DHCP server on the relay agent ..."
dnsmasq -k --conf-file=/dev/null --port=0 --user=root --bind-dynamic	\
	--pid-file="$DIR/dnsmasq.pid" --dhcp-leasefile="$DIR/leases"	\
	--log-dhcp --log-facility="$DIR/dnsmasq.log" --no-ping		\
	--dhcp-range=10.0.0.100,10.0.0.150,255.255.255.0,2m		\
	--dhcp-circuitid=set:p1,c1 --dhcp-host=tag:p1,*:*:*:*:*:*,10.0.0.41 \
	--dhcp-circuitid=set:p2,c2 --dhcp-host=tag:p2,*:*:*:*:*:*,10.0.0.42 &
echo $! >> "$DIR/PIDs"

relay

client c1 || { show_logs; FAIL "No lease for c1"; }
client c2 || { show_logs; FAIL "No lease for c2"; }

print "Analyzing ..."
echo "c1 got $(lease c1), expected 10.0.0.41"
echo "c2 got $(lease c2), expected 10.0.0.42"
[ "$(lease c1)" = "10.0.0.41" ] || { show_logs; FAIL; }
[ "$(lease c2)" = "10.0.0.42" ] || { show_logs; FAIL; }

OK
