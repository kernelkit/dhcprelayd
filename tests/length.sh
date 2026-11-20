#!/bin/sh
# Verify that a request is relayed with the same length as it was sent
# by the client, when no option 82 is added.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep tcpdump

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.conf"
server 20.0.0.2 {}
interface c1 {}
EOF

server
relay

print "Starting collectors on c1 and s1 ..."
capture c1
capture s1
sleep 1

client c1 || { show_logs; FAIL "No lease"; }
sleep 1

print "Analyzing ..."
sent=$(cap_lengths c1)
relayed=$(cap_lengths s1)
echo "Client sent requests of length $sent, relayed with length $relayed"
if [ -z "$sent" ] || [ "$sent" != "$relayed" ]; then show_logs; FAIL; fi

OK
