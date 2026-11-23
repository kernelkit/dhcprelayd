#!/bin/sh
# Verify the trusted setting, on a bridge where it is set for the
# interface and turned off for port c1, so only port c2 is trusted.
#
# A request that is already relayed, with giaddr set and its own option
# 82, circuit-id "old":
#
#  - is dropped from c1, the server gets no request
#  - is relayed unchanged from c2
#
# A request with option 82 but without giaddr, as from a switch that
# adds option 82 without being a relay agent:
#
#  - gets the relay agent's option 82 from c1, policy replace
#  - keeps its own option 82 from c2

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep python3

# DISCOVER with option 82: sub-option 1, circuit-id, length 3, "old"
SUB=01036f6c64
DISCOVER="3501015205${SUB}ff"

# Number of requests the server got with circuit-id c1 and old
count()
{
    echo "$(server_count "tags: .*new") $(server_count "tags: .*old")"
}

print "Creating world ..."
topo bridged

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {}
interface vlan1 {
    trusted = true
    port c1 {
        trusted = false
    }
    port c2 {}
}
EOF

server --dhcp-circuitid=set:new,c1 --dhcp-circuitid=set:old,old
relay

print "Relayed request on c1, not trusted ..."
send c1 255.255.255.255 1 giaddr=10.0.0.99 opts=$DISCOVER
got=$(server_count "DHCPDISCOVER(")
echo "Server got $got DISCOVER, expected 0"
[ "$got" -eq 0 ] || { show_logs; FAIL; }

print "Relayed request on c2, trusted ..."
send c2 255.255.255.255 1 giaddr=10.0.0.99 opts=$DISCOVER
echo "Server got $(count) requests with circuit-id c1 and old, expected 0 1"
[ "$(count)" = "0 1" ] || { show_logs; FAIL; }

print "Request with option 82 on c1, not trusted ..."
send c1 255.255.255.255 1 opts=$DISCOVER
echo "Server got $(count) requests with circuit-id c1 and old, expected 1 1"
[ "$(count)" = "1 1" ] || { show_logs; FAIL; }

print "Request with option 82 on c2, trusted ..."
send c2 255.255.255.255 1 opts=$DISCOVER
echo "Server got $(count) requests with circuit-id c1 and old, expected 1 2"
[ "$(count)" = "1 2" ] || { show_logs; FAIL; }

OK
