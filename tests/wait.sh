#!/bin/sh
# Verify that the relay agent waits for what is not there when it
# starts: the interface has no address, port c1 is not yet a bridge
# port, and port nope does not exist.  When the address is set and c1
# is added to the bridge, c1 gets a lease and its requests are not
# flooded to c2.  That still holds after c1 has left the bridge and
# joined it again, with one set of rules for c1 in the bridge filter.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft
check_dep tcpdump

print "Creating world ..."
topo bridged
ip addr del 10.0.0.1/24 dev vlan1
ip link set c1 nomaster

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
interface vlan1 {
    port c1 {}
    port nope {}
}
interface nope {}
EOF

server
relay

print "Setting address and adding c1 to the bridge ..."
ip addr add 10.0.0.1/24 dev vlan1
ip link set c1 master br0
sleep 3
alive "when the interface got its address"

print "Starting collector on c2 ..."
capture c2
sleep 1

client c1 || { show_logs; FAIL "No lease"; }
sleep 1

print "Removing c1 from the bridge and adding it again ..."
ip link set c1 nomaster
sleep 3
ip link set c1 master br0
sleep 3

client c1 || { show_logs; FAIL "No lease after c1 joined the bridge again"; }
sleep 1

flooded=$(wc -l < "$DIR/c2.cap")
rules=$(nft list table bridge filter | grep -c '"c1"')
echo "c2 saw $flooded requests from c1, expected 0"
echo "The bridge filter has $rules rules for c1, expected 2"
if [ "$flooded" -ne 0 ] || [ "$rules" -ne 2 ]; then nft list table bridge filter; show_logs; FAIL; fi

OK
