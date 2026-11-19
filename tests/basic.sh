#!/bin/sh
# Verify that a client on a routed interface gets an address from the
# pool of a DHCP server on another subnet, and that each request is
# relayed exactly once.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.json"
{
    "server": [ { "address": "20.0.0.2" } ],
    "group":  [ { "giaddr": "10.0.0.1" } ]
}
EOF

server
relay

client c1 || { show_logs; FAIL "No lease"; }

print "Analyzing ..."
addr=$(lease c1)
case "$addr" in
    10.0.0.1[0-4][0-9]|10.0.0.150)
	;;
    *)
	show_logs
	FAIL "Expected address in 10.0.0.100-150, got $addr"
	;;
esac

discover=$(server_count "DHCPDISCOVER(")
request=$(server_count "DHCPREQUEST(")
echo "Server got $discover DISCOVER and $request REQUEST, expected 1 each"
if [ "$discover" -ne 1 ] || [ "$request" -ne 1 ]; then show_logs; FAIL; fi

OK
