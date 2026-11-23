#!/bin/sh
# Verify that the relay agent only relays replies from its servers:
#
#  - a reply for another relay agent, with the broadcast flag, is
#    dropped and the relay agent keeps running
#  - a reply from a host that is not a server, here the client itself,
#    is dropped and gives no ARP entry for the address it offers
#  - a reply from the server is relayed, the client gets a lease

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep python3

print "Creating world ..."
topo routed
nsrun c1 ip addr add 10.0.0.50/24 dev eth0

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
interface c1 {}
EOF

server
relay

print "Reply for another relay agent ..."
send s1 10.0.0.1 2 giaddr=192.0.2.1 flags=0x8000 sport=1067
alive "on a reply for another relay agent"

print "Reply from the client ..."
send c1 10.0.0.1 2 giaddr=10.0.0.1 yiaddr=10.0.0.77 chaddr=02:00:00:00:00:77 sport=67
alive "on a reply from the client"
neigh=$(ip neigh show dev c1)
echo "ARP entries on c1: ${neigh:-none}, expected none for 10.0.0.77"
case "$neigh" in
    *10.0.0.77*)
	show_logs
	FAIL
	;;
esac
nsrun c1 ip addr del 10.0.0.50/24 dev eth0

client c1 || { show_logs; FAIL "No lease"; }

OK
