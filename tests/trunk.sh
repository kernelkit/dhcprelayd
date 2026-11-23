#!/bin/sh
# Verify relaying from a trunk port, a bridge port that is a tagged
# member of two VLANs and so a port of two interfaces.  Port c1 is the
# trunk, with VLAN 10 and 20, port c2 is an untagged member of VLAN 20.
#
#  - a request tagged with VLAN 10 on c1 gets an address in 10.0.10.0/24
#  - a request tagged with VLAN 20 on c1 gets an address in 10.0.20.0/24
#  - an untagged request on c2 gets an address in 10.0.20.0/24
#  - every request is relayed once, and not flooded to c2

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft
check_dep tcpdump

print "Creating world ..."
topo bridged
for vid in 10 20; do
    bridge vlan add vid $vid dev br0 self
    bridge vlan add vid $vid dev c1
    ip link add link br0 vlan$vid type vlan id $vid
    ip link set vlan$vid up
    ip addr add 10.0.$vid.1/24 dev vlan$vid
    nsrun c1 ip link add link eth0 eth0.$vid type vlan id $vid
    nsrun c1 ip link set eth0.$vid up
done
bridge vlan del vid 1 dev c2
bridge vlan add vid 20 dev c2 pvid untagged
bridge vlan show

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {}
interface vlan10 {
    port c1 {
        tagged = true
    }
}
interface vlan20 {
    port c1 {
        tagged = true
        circuit-id = trunk-20
    }
    port c2 {}
}
EOF

server --dhcp-range=10.0.10.100,10.0.10.150,255.255.255.0,2m	\
       --dhcp-range=10.0.20.100,10.0.20.150,255.255.255.0,2m	\
       --dhcp-circuitid=set:t10,c1 --dhcp-circuitid=set:t20,trunk-20
relay

print "Starting collector on c2 ..."
mac=$(nsrun c1 ip -br link show eth0 | awk '{print $3}')
capture c2 "udp dst port 67 and ether src $mac"
sleep 1

# Lease in netns $1 on interface $2 must be in subnet $3
check()
{
    client "$1" -i "$2" || { show_logs; FAIL "No lease for $1 on $2"; }
    addr=$(lease "$1")
    echo "$1 got $addr on $2, expected an address in $3.0/24"
    case "$addr" in
	"$3".*)
	    ;;
	*)
	    show_logs
	    FAIL "$1 got $addr on $2"
	    ;;
    esac
}

check c1 eth0.10 10.0.10
check c1 eth0.20 10.0.20
check c2 eth0    10.0.20
sleep 1

print "Analyzing ..."
flooded=$(wc -l < "$DIR/c2.cap")
discover=$(server_count "DHCPDISCOVER(")
t10=$(server_count "tags: .*t10")
t20=$(server_count "tags: .*t20")
echo "c2 saw $flooded requests from c1, expected 0"
echo "Server got $discover DISCOVER, expected 3"
echo "Server got $t10 requests with circuit-id c1 and $t20 with trunk-20, expected 2 each"
if [ "$flooded" -ne 0 ] || [ "$discover" -ne 3 ] || [ "$t10" -ne 2 ] || [ "$t20" -ne 2 ]; then
    cat "$DIR/c2.cap"
    show_logs
    FAIL
fi

OK
