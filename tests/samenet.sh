#!/bin/sh
# Verify relaying to a server on the same subnet as the clients, behind
# bridge port s1.  The relay agent is there only to add option 82.
#
#  - with s1 not a port of the interface, c1 gets the address the
#    server has for its port
#  - with s1 a trusted port of the interface, as towards the next switch
#    in a chain of relay agents, the replies from the server arrive on a
#    port that is relayed from, and c1 still gets its address
#  - the server gets every request once, relayed

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft

config()
{
    cat <<-EOF > "$DIR/relay.conf"
	server 10.0.0.2 {}
	option82 {}
	interface vlan1 {
	    port c1 {}
	    port c2 {}
	    $1
	}
	EOF
}

print "Creating world ..."
topo bridged
ip addr flush dev s1
ip link set s1 master br0
nsrun s1 ip addr flush dev eth0
nsrun s1 ip addr add 10.0.0.2/24 dev eth0

config
server --dhcp-circuitid=set:p1,c1 --dhcp-host=tag:p1,*:*:*:*:*:*,10.0.0.41
relay

client c1 || { show_logs; FAIL "No lease for c1"; }
echo "c1 got $(lease c1), expected 10.0.0.41"
[ "$(lease c1)" = 10.0.0.41 ] || { show_logs; FAIL; }

reload config "port s1 { trusted = true }"
client c1 || { show_logs; FAIL "No lease for c1 with s1 as port"; }
echo "c1 got $(lease c1), expected 10.0.0.41"
[ "$(lease c1)" = 10.0.0.41 ] || { show_logs; FAIL; }

discover=$(server_count "DHCPDISCOVER(")
tagged=$(server_count "tags: .*p1")
echo "Server got $discover DISCOVER, expected 2"
echo "Server got $tagged requests with circuit-id c1, expected 4"
if [ "$discover" -ne 2 ] || [ "$tagged" -ne 4 ]; then show_logs; FAIL; fi

OK
