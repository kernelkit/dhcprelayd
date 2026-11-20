#!/bin/sh
# Verify the option 82 policy for requests from a client that already
# carry option 82, here circuit-id "old":
#
#  - replace: our option 82 replaces the client's, circuit-id "c1"
#  - keep:    the client's option 82 is forwarded, circuit-id "old"
#  - drop:    the request is not relayed
#
# The server has one static address per circuit-id.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

# Option 82 with sub-option 1, circuit-id, length 3, "old"
OPT82="0x52:01036f6c64"

print "Creating world ..."
topo routed

config()
{
    cat <<-EOF > "$DIR/relay.conf"
	server 20.0.0.2 {}
	option82 {
	    policy = $1
	}
	interface c1 {}
	EOF
}

config replace
server --dhcp-circuitid=set:new,c1 --dhcp-circuitid=set:old,old	\
       --dhcp-host=tag:new,*:*:*:*:*:*,10.0.0.42			\
       --dhcp-host=tag:old,*:*:*:*:*:*,10.0.0.43
relay

print "Policy replace ..."
client c1 -x "$OPT82" || { show_logs; FAIL "No lease with policy replace"; }
echo "c1 got $(lease c1), expected 10.0.0.42"
[ "$(lease c1)" = "10.0.0.42" ] || { show_logs; FAIL; }

print "Policy keep ..."
reload config keep
client c1 -x "$OPT82" || { show_logs; FAIL "No lease with policy keep"; }
echo "c1 got $(lease c1), expected 10.0.0.43"
[ "$(lease c1)" = "10.0.0.43" ] || { show_logs; FAIL; }

print "Policy drop ..."
reload config drop
before=$(server_count "DHCPDISCOVER(")
if client c1 -x "$OPT82"; then
    show_logs
    FAIL "Got lease $(lease c1) with policy drop"
fi
after=$(server_count "DHCPDISCOVER(")
echo "Server got $((after - before)) DISCOVER with policy drop, expected 0"
[ "$after" -eq "$before" ] || { show_logs; FAIL; }

OK
