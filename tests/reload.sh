#!/bin/sh
# Verify that SIGHUP reloads the configuration: after changing the
# circuit-id of port c1 the client gets the address for the new one.
# An invalid configuration on SIGHUP is rejected and the relay agent
# keeps running with the one it has.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft

print "Creating world ..."
topo bridged

config()
{
    cat <<-EOF > "$DIR/relay.conf"
	option82 {}
	server 20.0.0.2 {}
	interface vlan1 {
	    port c1 {
	        circuit-id = $1
	    }
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

reload config after

client c1 || { show_logs; FAIL "No lease after reload"; }
echo "c1 got $(lease c1), expected 10.0.0.42"
[ "$(lease c1)" = "10.0.0.42" ] || { show_logs; FAIL; }

reload config ""

client c1 || { show_logs; FAIL "No lease after invalid reload"; }
echo "c1 got $(lease c1), expected 10.0.0.42"
[ "$(lease c1)" = "10.0.0.42" ] || { show_logs; FAIL; }

OK
