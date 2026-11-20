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

Option 82 policy per port
-------------------------

The policy for requests that already carry option 82 is global.  Allow
setting it per port, e.g., `keep` on a port towards a trusted device
that adds its own option 82.

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
and to generate, and drop the jansson dependency.

nftables
--------

Rules are applied by running `nft` with `system()`.  Use libnftables, or
document the rules so the system integrator can install them.

Operational data
----------------

Count, per interface and port: requests and replies relayed, requests
dropped by the option 82 policy, requests that already carry option 82,
and send errors.  Export them for the system, e.g., over a UNIX socket
or to a file on SIGUSR1.

Tests
-----

- Renewals through the relay with `force-server-id`.
- Several servers, global and per group.
- Memory leaks with valgrind, including SIGHUP reload.
