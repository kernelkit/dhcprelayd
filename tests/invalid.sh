#!/bin/sh
# Verify that an invalid configuration makes the relay agent exit with
# an error at startup.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

# Start the relay agent with configuration $1, expect exit status 1
invalid()
{
    printf '%s\n' "$1" > "$DIR/relay.conf"
    print "Configuration: $1"
    "$DHCPRELAYD" -d -f "$DIR/relay.conf"
    rc=$?
    echo "Relay agent exited with $rc, expected 1"
    [ "$rc" -eq 1 ] || FAIL
}

invalid 'server 20.0.0.2 {} option82 { policy = bogus } interface c1 {}'
invalid 'server 20.0.0.2 {} option82 { remote-id-type = mac } interface c1 {}'
invalid 'server 20.0.0.2 {} option82 { remote-id = a remote-id-hex = 01 } interface c1 {}'
invalid 'server 20.0.0.2 {} option82 { remote-id-type = giaddr remote-id = a } interface c1 {}'
invalid 'server 20.0.0.2 {} interface c1 { port c1 { circuit-id-hex = 0:1 } }'
invalid 'server 20.0.0.2 {} interface c1 { port c1 { circuit-id-hex = 01: } }'
invalid 'server 20.0.0.2 {} interface c1 { port nope {} }'
invalid 'server 20.0.0.2 {} interface nope {}'
invalid 'server 20.0.0.2 { port = 0 } interface c1 {}'
invalid 'server 20.0.0.300 {} interface c1 {}'
invalid 'server 20.0.0.2 {}'
invalid 'interface c1 {}'
invalid 'interface c1 { giaddr = 10.0.0.256 server 20.0.0.2 {} }'
invalid 'server 20.0.0.2 {} interface c1 {} bogus = 1'

OK
