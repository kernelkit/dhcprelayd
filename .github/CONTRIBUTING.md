Contributing to dhcprelayd
==========================

Thank :heart: you for taking the time to read this!

We welcome any help in the form of bug reports, fixes, or patches to add
new features.  We *prefer* GitHub pull requests, but are open to other
forms of collaboration as well.

If you are unsure how to start implementing an idea or fix:

- :bug: open an issue, there are templates for _bugs_ and _feature
  requests_ at <https://github.com/kernelkit/dhcprelayd/issues>
- :speech_balloon: use the [Discussions][discuss] forum
- :memo: check [TODO.md][todo], it lists known bugs and planned work

> [!IMPORTANT]
> Talking about code and problems first is often the best way to get
> started before submitting a pull request.  It saves time, yours and
> ours.


General Guidelines
------------------

When submitting bug reports or patches, please state which version the
change is made against, what it does, and, more importantly, *why*.
Start with why.

- :bug: Bug reports need the version, `dhcprelayd -v`, or a git commit
- :adhesive_bandage: Bug fixes should come with a test that fails
  without the fix, see [Testing](#testing) below
- :new: New features need tests, an update to the README and man page,
  and an entry in the [ChangeLog][]


Coding Style
------------

> [!TIP]
> Adapt your code to what the surrounding code looks like!

dhcprelayd is written in C and follows the [Linux kernel coding
style][kstyle]: tabs for indentation, braces on the same line except
for functions.  Lines up to about 100 characters are fine.

Comments say what the code is or why, in one line, or not at all.  The
history of a change, what was tried, and what it fixes, belongs in the
commit message.


Testing
-------

The tests are in `tests/`, run them with:

    ./autogen.sh
    ./configure
    make check

Each test runs as an unprivileged user in its own network namespaces,
no root or sudo needed.  See [tests/README.md][tests] for the tools the
host needs and how to run a single test.  The same tests run in GitHub
Actions for every push and pull request.


Commit Messages
---------------

Commit messages exist to track *why* a change was made.  Describe the
problem, its cause, and the fix.  Keep the subject line short, and
always set up a proper git identity for your commits:

    git config --global user.name "Jane Doe"
    git config --global user.email jane.doe@example.com

Use `git commit -s` to add a `Signed-off-by:` line, it certifies that
you wrote the change, or have the right to submit it under the project
license, see the [Developer Certificate of Origin][DCO].

    Fix length of requests read from the raw socket

    Requests read from the raw socket on an interface were relayed with
    42 bytes of trailing garbage.  The length passed on was the size of
    the whole frame, including the Ethernet, IP and UDP headers.

    Use the length from the UDP header, and drop frames too short for it.

    Signed-off-by: Jane Doe <jane.doe@example.com>

One logical change per commit.  Before a pull request is merged, fold
fixups into the commit they fix.


Code of Conduct
---------------

It is expected of everyone engaging in the project to, in the words of
Bill & Ted, [be excellent to each other][conduct].


[discuss]:   https://github.com/orgs/kernelkit/discussions
[todo]:      https://github.com/kernelkit/dhcprelayd/blob/main/TODO.md
[ChangeLog]: https://github.com/kernelkit/dhcprelayd/blob/main/ChangeLog.md
[tests]:     https://github.com/kernelkit/dhcprelayd/blob/main/tests/README.md
[kstyle]:    https://www.kernel.org/doc/html/latest/process/coding-style.html
[DCO]:       https://developercertificate.org/
[conduct]:   CODE_OF_CONDUCT.md
