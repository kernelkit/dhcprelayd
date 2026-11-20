#!/bin/sh
# Verify that DHCP requests on a bridge port are relayed once and not
# flooded to the other bridge ports.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft
check_dep tcpdump

print "Creating world ..."
topo bridged

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
interface vlan1 {
    port c1 {}
    port c2 {}
}
EOF

server
relay

print "Starting collector on c2 ..."
capture c2
sleep 1

client c1 || { show_logs; FAIL "No lease"; }
sleep 1

print "Analyzing ..."
flooded=$(wc -l < "$DIR/c2.cap")
echo "c2 saw $flooded requests from c1, expected 0"
[ "$flooded" -eq 0 ] || { show_logs; FAIL; }

discover=$(server_count "DHCPDISCOVER(")
request=$(server_count "DHCPREQUEST(")
echo "Server got $discover DISCOVER and $request REQUEST, expected 1 each"
if [ "$discover" -ne 1 ] || [ "$request" -ne 1 ]; then show_logs; FAIL; fi

OK
