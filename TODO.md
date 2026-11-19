TODO
====

Layer 2 relay mode
------------------

Insert option 82 without acting as a gateway, for switches that have no
IP address in the client VLAN.  Today every group needs a `giaddr`.

- Leave `giaddr` at zero, add option 82 with circuit-id/remote-id, and
  send the request on out the uplink port(s) instead of unicasting it to
  a server.
- Trap server replies (UDP port 68) as well, strip option 82 and send
  the reply out the port in the circuit-id only.
- Needs a way to tell uplink ports from client ports, e.g., per-port
  trusted flag.
- See RFC 3046, sec. 2.1, on relay agents that do not set `giaddr`.

Existing option 82
------------------

A request from a client that already carries option 82 gets a second
one appended.  Add a policy for requests with `giaddr` zero:

- `keep`: forward with the existing option
- `replace`: drop the existing option and add ours
- `drop`: discard the request

Requests already relayed (`giaddr` set) are forwarded as-is.

Remote-id
---------

Add a `mac` type, base MAC address of the system.  Today there is
`hostname`, `giaddr` and `manual`.

Per-port enable
---------------

Allow disabling relay on a port in a group, for ports towards other
switches or towards the servers.

Configuration file
------------------

Replace the JSON configuration with something easier to write by hand
and to generate, and drop the jansson dependency.  Reload on SIGHUP.

nftables
--------

Rules are applied by running `nft` with `system()`.  Use libnftables, or
document the rules so the system integrator can install them.

Tests
-----

- `make distcheck` fails, `tests/check.sh` is not in `EXTRA_DIST`.
- Replace the Travis CI and Python 2 integration tests with shell-based
  tests in network namespaces, like smcroute and mrouted: veth pairs,
  a bridge, dnsmasq as server and udhcpc as client, and checks of the
  option 82 contents with tshark or tcpdump.
- GitHub Actions workflow.
