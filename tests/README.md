Tests
=====

The tests run dhcprelayd between a DHCP server and one or more DHCP
clients, each in its own network namespace, connected with VETH pairs.
The server is dnsmasq, the clients are BusyBox udhcpc.  A test passes
when the clients get the address the server has for them.

Each test runs as an unprivileged user in its own user, mount and
network namespace, created by `unshare -mrun`, so neither root nor
sudo is needed and nothing on the host is changed.


Host requirements
-----------------

The following tools must be in `$PATH`:

  - `ip`, `bridge`: iproute2, not the BusyBox versions
  - `unshare`, `nsenter`: util-linux 2.38 or later, for `--map-auto`
  - `dnsmasq`: the DHCP server
  - `busybox`: with the `udhcpc` applet
  - `nft`: nftables, used by dhcprelayd on bridge ports
  - `tcpdump`: to check what is sent on the wire
  - `stdbuf`: coreutils
  - `python3`: sends the malformed and forged messages, see `send.py`

On Debian and Ubuntu:

    sudo apt install iproute2 util-linux dnsmasq-base busybox nftables tcpdump python3

The `dnsmasq-base` package has the binary without the system service.
A test that lacks a tool exits with 77 and is reported as SKIP.

The kernel must allow unprivileged user namespaces.  Ubuntu 24.04 and
later restrict them with AppArmor, lift that with:

    sudo sysctl kernel.apparmor_restrict_unprivileged_userns=0


Running
-------

    ./autogen.sh
    ./configure
    make check

The result of each test is in `tests/NAME.log`.  To run a single test
by hand, from the `tests/` directory:

    DHCPRELAYD=../src/dhcprelayd unshare -mrun --map-auto ./bridge.sh

Tests use fixed interface names and `/tmp/NAME/` for their files, so run
them one at a time.


Topologies
----------

### Routed

The relay agent routes between the client network and the server
network.

       netns c1         relay agent           netns s1
      .--------.     .---------------.      .----------.
      | eth0 --+-----+ c1         s1 +------+-- eth0   |
      '--------'     '---------------'      '----------'
      10.0.0.0/24   .1             .1        20.0.0.2/24

### Bridged

The relay agent is a switch with clients on bridge ports `c1` and `c2`,
a VLAN filtering bridge, and the gateway address on `vlan1`.

       netns c1     relay agent                     netns s1
      .--------.   .--------------------------.   .----------.
      | eth0 --+---+ c1                       |   |          |
      '--------'   |   \                      |   |          |
                   |    br0 -- vlan1      s1 -+---+-- eth0   |
      .--------.   |   /       10.0.0.1  .1   |   |          |
      | eth0 --+---+ c2                       |   |          |
      '--------'   '--------------------------'   '----------'
       netns c2                                   20.0.0.2/24


Tests
-----

### basic

A client on a routed interface gets an address from the server pool,
and each request reaches the server exactly once.

Topology: routed

### forged

Replies the relay agent must not relay: one for another relay agent,
which crashed it, and one from the client itself, which gave the relay
agent an ARP entry for the offered address.  Both are dropped, and the
client still gets a lease from the server.

Topology: routed

### giaddr

With `remote-id-type giaddr`, the client gets the static address the
server has for remote-id `10.0.0.1`, the gateway address of `c1`.

Topology: routed

### hex

A binary circuit-id and remote-id, set with `circuit-id-hex` and
`remote-id-hex`, reach the server intact, including the NUL byte the
circuit-id starts with.

Topology: routed

### invalid

Each of a set of invalid configurations, from a bad policy or hex value
to an unknown interface or a missing server, makes the relay agent exit
with status 1 at startup.

Topology: routed

### length

A request is relayed with the same UDP length as the client sent it,
when no option 82 is added.

Topology: routed

### local

A DHCP server on the same system: dnsmasq runs in the relay agent's
namespace with `--bind-dynamic` and is reached at `127.0.0.1`.  The
clients on ports `c1` and `c2` each get the static address the server
has for their port.

Topology: bridged

### malformed

Requests that crashed the relay agent with option 82 enabled: without
the end option, cut off in the BOOTP header, and with the end option
beyond 548 bytes.  The relay agent keeps running, the long request gets
option 82, and a request with a pad option between two options reaches
the server with both.

Topology: routed

### option82

With option 82 enabled on a routed interface, the client gets the
static address the server has for circuit-id `c1`, the interface name,
and remote-id `relay1`.

Topology: routed

### policy

A client sends its own option 82, circuit-id `old`.  With policy
`replace` the client gets the address for circuit-id `c1`, and a request
with two option 82 reaches the server with only the relay agent's.  With
`keep` the client gets the address for `old`, and with `drop` the server
gets no request.

Topology: routed

### bridge

IP-per-port: the clients on ports `c1` and `c2` each get the static
address the server has for their port.  Port `c1` uses the default
circuit-id, the port name, and port `c2` a configured one.

Topology: bridged

### flood

A request on port `c1` is relayed once, and not flooded to port `c2`.

Topology: bridged

### reply

Option 82 is removed from replies to the client, and nothing else is
lost: the reply is 14 bytes shorter than the one the server sent, and
the long domain name in it, which makes it larger than 300 bytes, is
still there.

Topology: routed

### reload

After changing the circuit-id of port `c1` and sending SIGHUP to the
relay agent, the client gets the address for the new circuit-id.

Topology: bridged

### trusted

`trusted` is set for the interface and turned off for port `c1`.  A
request that is already relayed, with giaddr set, is dropped from `c1`
and reaches the server unchanged from `c2`.  A request with option 82
and no giaddr gets the relay agent's option 82 from `c1` and keeps its
own from `c2`.

Topology: bridged
