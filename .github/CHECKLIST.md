Release Checklist
=================

Steps to cut a new dhcprelayd release.  The version number is `X.Y` for
a release and `X.Y-betaN` or `X.Y-rcN` for a pre-release; the git tag
is the same with a leading `v`.

Everything below happens on `main`, in the order given.  Nothing is
pushed until the whole list is ticked off -- a pushed tag is a released
tag, and the only recovery from a bad release is the next one.


Before Tagging
--------------

 - [ ] `main` is green in [Bob the Builder][]
 - [ ] `ChangeLog.md`: turn `[vX.Y][UNRELEASED]` into `[vX.Y][] -
       YYYY-MM-DD` with the Changes and Fixes for *this* release, and
       replace the `[UNRELEASED]` link at the bottom with
       `[vX.Y]: .../compare/vX.Y-1...vX.Y`
 - [ ] `configure.ac`: set the version in `AC_INIT()` to `X.Y`
 - [ ] Commit the two together: `git commit -s -m "Bump version for
       vX.Y release"`


Verify
------

 - [ ] `./autogen.sh && ./configure`
 - [ ] `make check`, see [tests/README.md][] for what the suite needs
 - [ ] `make distcheck`


Tag and Push
------------

 - [ ] `git tag -a vX.Y -m "dhcprelayd vX.Y"`
 - [ ] `git push`
 - [ ] `git push --tags`

Pushing the tag starts [Release General].  It checks that the tag
matches the version in `configure.ac`, runs `make distcheck`, and
publishes the tarball and its `.md5` with the top `ChangeLog.md` section
as release notes.  Tags ending in `-alphaN`, `-betaN` or `-rcN` are
published as pre-releases and are not marked latest.

 - [ ] Check the workflow went green and the release page looks right


Start the Next Cycle
--------------------

Right after a release, so `main` never sits on a released version:

 - [ ] `configure.ac`: set the version to the next `X.Y-pre`
 - [ ] `ChangeLog.md`: open a `[vX.Y][UNRELEASED]` section and point
       `[UNRELEASED]` at `compare/vX.Y-1...HEAD`


[Bob the Builder]:  https://github.com/kernelkit/dhcprelayd/actions/workflows/build.yml
[Release General]:  https://github.com/kernelkit/dhcprelayd/actions/workflows/release.yml
[tests/README.md]:  https://github.com/kernelkit/dhcprelayd/blob/main/tests/README.md
