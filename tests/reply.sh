#!/bin/sh
# Verify that option 82 is removed from replies to the client, and that
# nothing else is lost.  The server sends a long domain name, so the
# reply is larger than the minimum DHCP message size of 300 bytes.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep tcpdump

DOMAIN=$(printf 'a%.0s' $(seq 1 60)).$(printf 'b%.0s' $(seq 1 60)).$(printf 'c%.0s' $(seq 1 60)).example

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
option82 {
    remote-id = relay1
}
interface c1 {}
EOF

server --dhcp-option=15,"$DOMAIN"
relay

print "Starting collectors on s1 and c1 ..."
capture s1 'udp src port 67' -v
capture c1 'udp src port 67' -v
sleep 1

client c1 || { show_logs; FAIL "No lease"; }
sleep 1

print "Analyzing ..."
# Length of the first reply, the OFFER, in BOOTP/DHCP bytes
offer()
{
    grep -m1 -o 'BOOTP/DHCP, Reply, length [0-9]*' "$DIR/$1.cap" | awk '{print $NF}'
}

from=$(offer s1)
to=$(offer c1)
echo "Server sent OFFER of $from bytes, client got $to bytes, expected $((from - 14))"
if [ -z "$from" ] || [ "$to" != "$((from - 14))" ]; then show_logs; FAIL; fi

agent=$(grep -c "Agent-Information" "$DIR/c1.cap")
echo "Client got $agent replies with option 82, expected 0"
[ "$agent" -eq 0 ] || { show_logs; FAIL; }

grep -q "$DOMAIN" "$DIR/c1.cap" || { show_logs; FAIL "Domain name missing in reply to client"; }

OK
