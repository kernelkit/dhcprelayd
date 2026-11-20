Security Policy
===============

Reporting a Vulnerability
-------------------------

Please report security issues privately, using the *Report a
vulnerability* button under [Security][advisories] on GitHub.  That
opens a draft advisory only you and the maintainers can see, and it is
where a CVE is requested if one is warranted.  Please do not open a
public issue for something you believe is exploitable until a fix is
available.

Include the version, or git commit, you tested, the configuration
needed to reach the problem, and how to trigger it.  A packet capture or
a small reproducer is very welcome.  You will be credited in the
advisory and in the [ChangeLog][], unless you would rather not be.


Supported Versions
------------------

Only the latest release is supported.  Fixes go on the `main` branch and
into the next release; there are no maintenance branches and no
backports to older releases.  Distributors are welcome to cherry-pick.


Exposure
--------

dhcprelayd runs as root and parses every DHCP request broadcast on its
configured interfaces, and every reply sent to it.  DHCP has no
authentication, so any client on a client network can send it crafted
packets, and packet parsing bugs are remotely triggerable by design.
Only list the interfaces that need a relay agent in the configuration.


[advisories]: https://github.com/kernelkit/dhcprelayd/security
[ChangeLog]:  https://github.com/kernelkit/dhcprelayd/blob/main/ChangeLog.md
