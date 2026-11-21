Change Log
==========

All notable changes to the project are documented in this file.

- Versions >= v3.0, KernelKit, as dhcprelayd
- Versions >= v2.0, Westermo Teleindustri AB, as dhcp-helper
- Versions <= v1.2, Simon Kelley <simon@thekelleys.org.uk>

[v3.0][UNRELEASED]
---------------------
### Changes
- Fork of the archived Westermo dhcp-helper, renamed to dhcprelayd
- New configuration file format, `/etc/dhcprelayd.conf`, parsed with
  libConfuse, replaces the JSON format.  Each interface to relay from is
  an `interface` section, with its bridge ports as `port` sections.
  Binary circuit-id and remote-id values are set as colon separated hex
  with `circuit-id-hex` and `remote-id-hex`.  See the new
  dhcprelayd.conf(5) man page and the `examples/` directory
- Depends only on libev and libConfuse, no longer on libnl and jansson
- New test suite, runs unprivileged in network namespaces, replaces the
  Python 2 tests.  See `tests/README.md` for host requirements
- README: diagrams of the use case and how a request is relayed
- New `option82` setting `policy`: what to do with requests from a
  client that already carry option 82.  The default, `replace`, removes
  the client's option and adds the relay agent's own.  Previously a
  second option 82 was added, and a client could pick its own
  circuit-id
- The DHCP server can run on the same system as the relay agent, e.g.
  for addresses per port set up in dnsmasq.  The relay agent binds UDP
  port 67 only on its gateway addresses, and reaches the server at
  127.0.0.1.  dnsmasq needs `--bind-dynamic` or `--bind-interfaces`

### Fixes
- Requests on a routed interface were relayed twice, so the server got
  every DHCPDISCOVER and DHCPREQUEST two times
- Requests were relayed with 42 bytes of trailing garbage after the DHCP
  options
- Replies to clients were sent with trailing garbage, as many bytes as
  the option 82 that was removed from them
- Requests the relay agent did not add option 82 to, e.g., a DHCP
  request without the magic cookie, were relayed with a length of 0
- dhcprelayd exited with status 0 when the configuration file was
  invalid at startup, now it exits with status 1
- An invalid configuration file on SIGHUP stopped the relay agent, now
  it is rejected and the relay agent keeps running with the old one
- With `remote-id-type giaddr` the remote-id was `0.0.0.0`, unless
  `force-server-identity` was also set

[v2.1][] - 2018-06-11
---------------------
### Changes
- Add unittest
- Add ARP entry to be able to send unicast answer to clients
- Add FDB entry in bridge to fix issue with some switches
- Minor bugfixes

v2.0 - 2018-05-31
---------------------
### Changes
- Add support to run agent to run with bridges
- Add support for configuration via configuration file
- Add DHCP Option 82 support

[UNRELEASED]: https://github.com/kernelkit/dhcprelayd/compare/v2.1...HEAD
[v2.1]: https://github.com/westermo/dhcp-helper/compare/v2.0...v2.1
