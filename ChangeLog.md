Change Log
==========

All notable changes to the project are documented in this file.

- Versions >= v2.2, KernelKit, as dhcprelayd
- Versions >= v2.0, Westermo Teleindustri AB, as dhcp-helper
- Versions <= v1.2, Simon Kelley <simon@thekelleys.org.uk>

[v2.2][UNRELEASED]
---------------------
### Changes
- Fork of the archived Westermo dhcp-helper, renamed to dhcprelayd
- Default configuration file is now `/etc/dhcprelayd.json`
- No longer depends on libnl, only libev and jansson are needed
- New test suite, runs unprivileged in network namespaces, replaces the
  Python 2 tests.  See `tests/README.md` for host requirements
- README: diagrams of the use case and how a request is relayed
- New `option82` setting `policy`: what to do with requests from a
  client that already carry option 82.  The default, `replace`, removes
  the client's option and adds the relay agent's own.  Previously a
  second option 82 was added, and a client could pick its own
  circuit-id

### Fixes
- Requests on a routed interface were relayed twice, so the server got
  every DHCPDISCOVER and DHCPREQUEST two times
- Requests were relayed with 42 bytes of trailing garbage after the DHCP
  options
- Replies to clients were sent with trailing garbage, as many bytes as
  the option 82 that was removed from them
- Fix signed/unsigned comparison warning in the configuration parser

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
