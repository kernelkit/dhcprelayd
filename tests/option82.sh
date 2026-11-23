#!/bin/sh
# Verify that the relay agent adds option 82 on a routed interface: the
# server hands out a static address only to requests with circuit-id
# "c1", the interface name, and remote-id "relay1".  With option82 set
# to false for the interface the request is relayed without option 82,
# and the client gets an address from the pool.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

config()
{
    cat <<-EOF > "$DIR/relay.conf"
	server 20.0.0.2 {}
	option82 {
	    remote-id = relay1
	}
	interface c1 {
	    $1
	}
	EOF
}

config

server --dhcp-circuitid=set:port,c1 --dhcp-remoteid=set:agent,relay1	\
       --dhcp-host=tag:port,tag:agent,*:*:*:*:*:*,10.0.0.42
relay

client c1 || { show_logs; FAIL "No lease"; }

print "Analyzing ..."
addr=$(lease c1)
echo "c1 got $addr, expected 10.0.0.42"
[ "$addr" = "10.0.0.42" ] || { show_logs; FAIL; }

reload config "option82 = false"
client c1 || { show_logs; FAIL "No lease without option 82"; }
addr=$(lease c1)
echo "c1 got $addr, expected another address than 10.0.0.42"
[ "$addr" != "10.0.0.42" ] || { show_logs; FAIL; }

OK
