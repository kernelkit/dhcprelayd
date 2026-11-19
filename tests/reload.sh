#!/bin/sh
# Verify that SIGHUP reloads the configuration: after changing the
# circuit-id of port c1 the client gets the address for the new one.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft

print "Creating world ..."
topo bridged

config()
{
    cat <<-EOF > "$DIR/relay.json"
	{
	    "option82": {},
	    "server": [ { "address": "20.0.0.2" } ],
	    "group":  [
	        {
	            "ifname": "vlan1",
	            "giaddr": "10.0.0.1",
	            "iface":  [ { "ifname": "c1", "circuit-id": "$1" } ]
	        }
	    ]
	}
	EOF
}

config before
server --dhcp-circuitid=set:before,before --dhcp-circuitid=set:after,after	\
       --dhcp-host=tag:before,*:*:*:*:*:*,10.0.0.41			\
       --dhcp-host=tag:after,*:*:*:*:*:*,10.0.0.42
relay

client c1 || { show_logs; FAIL "No lease before reload"; }
echo "c1 got $(lease c1), expected 10.0.0.41"
[ "$(lease c1)" = "10.0.0.41" ] || { show_logs; FAIL; }

print "Reloading relay agent ..."
config after
kill -HUP "$(cat "$DIR/relay.pid")"
sleep 1
kill -0 "$(cat "$DIR/relay.pid")" 2>/dev/null || { show_logs; FAIL "Relay agent died on reload"; }

client c1 || { show_logs; FAIL "No lease after reload"; }
echo "c1 got $(lease c1), expected 10.0.0.42"
[ "$(lease c1)" = "10.0.0.42" ] || { show_logs; FAIL; }

OK
