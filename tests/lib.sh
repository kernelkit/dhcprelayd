#!/bin/sh
# Helper functions for the dhcprelayd tests.  Each test runs in its own
# user, mount and network namespace (unshare -mrun), the namespace the
# test starts in is the relay agent.  Servers and clients are separate
# network namespaces, bind mounted to files in $DIR.

# Test name, used everywhere as /tmp/$NM/foo
NM=$(basename "$0" .sh)
DIR=/tmp/$NM
DHCPRELAYD=${DHCPRELAYD:-../src/dhcprelayd}

print()
{
    printf "\e[7m>> %-80s\e[0m\n" "$1"
}

SKIP()
{
    print "TEST: SKIP"
    [ $# -gt 0 ] && echo "$*"
    exit 77
}

FAIL()
{
    print "TEST: FAIL"
    [ $# -gt 0 ] && echo "$*"
    exit 1
}

OK()
{
    print "TEST: OK"
    [ $# -gt 0 ] && echo "$*"
    exit 0
}

check_dep()
{
    command -v "$1" >/dev/null || SKIP "Cannot find $1, skipping test."
}

# Create network namespace $DIR/$1 with eth0, the other end of the VETH
# pair is $1 in the relay namespace.
netns()
{
    ns=$1

    touch "$DIR/$ns"
    echo "$DIR/$ns" >> "$DIR/mounts"

    unshare --net="$DIR/$ns" -- ip link set lo up
    nsenter --net="$DIR/$ns" -- ip link add eth0 type veth peer "$ns"
    nsenter --net="$DIR/$ns" -- ip link set "$ns" netns $$
    nsenter --net="$DIR/$ns" -- ip link set eth0 up
    ip link set "$ns" up
}

# Run command in network namespace $1
nsrun()
{
    ns=$1
    shift
    nsenter --net="$DIR/$ns" -- "$@"
}

# Routed topology, the relay agent is the router between the client
# network and the server network.
#
#   netns c1         relay agent           netns s1
#  .--------.     .---------------.      .----------.
#  | eth0 --+-----+ c1         s1 +------+-- eth0   |
#  '--------'     '---------------'      '----------'
#  10.0.0.0/24   .1             .1        20.0.0.2/24
topo_routed()
{
    netns c1
    netns s1

    ip addr add 10.0.0.1/24 dev c1
    ip addr add 20.0.0.1/24 dev s1
    nsrun s1 ip addr add 20.0.0.2/24 dev eth0
    nsrun s1 ip route add default via 20.0.0.1
}

# Bridged topology, the relay agent is a switch with the clients on
# bridge ports c1 and c2, and the gateway address on vlan1.
#
#   netns c1     relay agent                     netns s1
#  .--------.   .--------------------------.   .----------.
#  | eth0 --+---+ c1                       |   |          |
#  '--------'   |   \                      |   |          |
#               |    br0 -- vlan1      s1 -+---+-- eth0   |
#  .--------.   |   /       10.0.0.1  .1   |   |          |
#  | eth0 --+---+ c2                       |   |          |
#  '--------'   '--------------------------'   '----------'
#   netns c2                                   20.0.0.2/24
topo_bridged()
{
    netns c1
    netns c2
    netns s1

    ip link add br0 type bridge vlan_filtering 1 mcast_snooping 0
    ip link set c1 master br0
    ip link set c2 master br0
    bridge vlan add vid 1 dev br0 self
    ip link set br0 up
    ip link add link br0 vlan1 type vlan id 1
    ip link set vlan1 up

    ip addr add 10.0.0.1/24 dev vlan1
    ip addr add 20.0.0.1/24 dev s1
    nsrun s1 ip addr add 20.0.0.2/24 dev eth0
    nsrun s1 ip route add default via 20.0.0.1
}

topo()
{
    case "$1" in
	routed)
	    topo_routed
	    ;;
	bridged)
	    topo_bridged
	    ;;
	*)
	    FAIL "No such topology: $1"
	    ;;
    esac
    ip -br link
    ip -br addr
}

# Start dnsmasq as DHCP server in netns s1, serving 10.0.0.0/24 to
# relayed clients.  Extra arguments are passed to dnsmasq.
server()
{
    print "Starting DHCP server ..."
    nsrun s1 dnsmasq -k --conf-file=/dev/null --port=0 --user=root	\
	  --pid-file="$DIR/dnsmasq.pid" --dhcp-leasefile="$DIR/leases"	\
	  --log-dhcp --log-facility="$DIR/dnsmasq.log" --no-ping	\
	  --dhcp-range=10.0.0.100,10.0.0.150,255.255.255.0,2m		\
	  --dhcp-option=3,10.0.0.1 "$@" &
    echo $! >> "$DIR/PIDs"
    sleep 1
}

# Start the relay agent with configuration file $DIR/relay.json
relay()
{
    print "Starting relay agent ..."
    cat "$DIR/relay.json"
    stdbuf -oL -eL "$DHCPRELAYD" -d -f "$DIR/relay.json" > "$DIR/relay.log" 2>&1 &
    echo $! > "$DIR/relay.pid"
    sleep 1
    kill -0 "$(cat "$DIR/relay.pid")" 2>/dev/null || {
	cat "$DIR/relay.log"
	FAIL "Relay agent failed to start"
    }
}

# Request a lease with udhcpc in netns $1, the address is saved in
# $DIR/$1.lease.  Returns non-zero if no lease was acquired.
client()
{
    ns=$1

    print "Requesting lease in $ns ..."
    cat <<-EOF > "$DIR/udhcpc.sh"
	#!/bin/sh
	[ "\$1" = bound ] && echo "\$ip" > "$DIR/\$(basename \$0 .sh).lease"
	exit 0
	EOF
    chmod +x "$DIR/udhcpc.sh"
    ln -sf "$DIR/udhcpc.sh" "$DIR/$ns.sh"
    rm -f "$DIR/$ns.lease"

    nsrun "$ns" busybox udhcpc -f -q -n -t 3 -T 2 -i eth0 -s "$DIR/$ns.sh"
    [ -s "$DIR/$ns.lease" ] || return 1
    echo "$ns got lease $(cat "$DIR/$ns.lease")"
}

# Print the address leased to netns $1
lease()
{
    cat "$DIR/$1.lease" 2>/dev/null
}

# Count lines in the dnsmasq log matching $1
server_count()
{
    grep -c "$1" "$DIR/dnsmasq.log"
}

# Log DHCP requests sent or received on interface $1 to $DIR/$1.cap
capture()
{
    tcpdump -lni "$1" 'udp dst port 67' > "$DIR/$1.cap" 2>/dev/null &
    echo $! >> "$DIR/PIDs"
}

# Print the unique UDP lengths captured on interface $1
cap_lengths()
{
    awk '{print $NF}' "$DIR/$1.cap" | sort -u | xargs
}

show_logs()
{
    print "Relay agent log"
    cat "$DIR/relay.log" 2>/dev/null
    print "DHCP server log"
    cat "$DIR/dnsmasq.log" 2>/dev/null
}

teardown()
{
    if [ -f "$DIR/relay.pid" ]; then
	kill "$(cat "$DIR/relay.pid")" 2>/dev/null
    fi
    while read -r pid; do
	kill "$pid" 2>/dev/null
    done < "$DIR/PIDs"
    sleep 0.2

    while read -r mnt; do
	umount "$mnt"
	rm -f "$mnt"
    done < "$DIR/mounts"

    rm -rf "$DIR"
}

signal()
{
    [ "$1" != "EXIT" ] && print "Got signal, cleaning up"
    teardown
}

trapit()
{
    func="$1" ; shift
    for sig ; do
	# shellcheck disable=SC2064
	trap "$func $sig" "$sig"
    done
}

check_dep busybox
check_dep dnsmasq
check_dep ip
check_dep bridge
check_dep nsenter

rm -rf "$DIR"
mkdir -p "$DIR"
touch "$DIR/PIDs" "$DIR/mounts"
trapit signal INT TERM QUIT EXIT
