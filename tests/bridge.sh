#!/bin/sh
# Verify IP-per-port on a VLAN filtering bridge: two clients on bridge
# ports c1 and c2 each get the static address the server has for their
# port.  Port c1 uses the default circuit-id, the port name, and port c2
# a configured one.

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

check_dep nft

print "Creating world ..."
topo bridged

cat <<EOF > "$DIR/relay.json"
{
    "option82": {
        "remote-id": { "type": "manual", "data": "switch1" }
    },
    "server": [ { "address": "20.0.0.2" } ],
    "group":  [
        {
            "ifname": "vlan1",
            "giaddr": "10.0.0.1",
            "iface":  [
                { "ifname": "c1" },
                { "ifname": "c2", "circuit-id": "cabinet-3" }
            ]
        }
    ]
}
EOF

server --dhcp-circuitid=set:p1,c1 --dhcp-circuitid=set:p2,cabinet-3	\
       --dhcp-host=tag:p1,*:*:*:*:*:*,10.0.0.41			\
       --dhcp-host=tag:p2,*:*:*:*:*:*,10.0.0.42
relay

client c1 || { show_logs; FAIL "No lease for c1"; }
client c2 || { show_logs; FAIL "No lease for c2"; }

print "Analyzing ..."
echo "c1 got $(lease c1), expected 10.0.0.41"
echo "c2 got $(lease c2), expected 10.0.0.42"
if [ "$(lease c1)" != 10.0.0.41 ] || [ "$(lease c2)" != 10.0.0.42 ]; then show_logs; FAIL; fi

OK
