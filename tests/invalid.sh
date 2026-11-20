#!/bin/sh
# Verify that an invalid configuration makes the relay agent exit with
# an error at startup.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

print "Creating world ..."
topo routed

cat <<EOF > "$DIR/relay.json"
{
    "option82": { "policy": "bogus" },
    "server": [ { "address": "20.0.0.2" } ],
    "group":  [ { "giaddr": "10.0.0.1" } ]
}
EOF

print "Starting relay agent with invalid configuration ..."
"$DHCPRELAYD" -d -f "$DIR/relay.json"
rc=$?
echo "Relay agent exited with $rc, expected 1"
[ "$rc" -eq 1 ] || FAIL

OK
