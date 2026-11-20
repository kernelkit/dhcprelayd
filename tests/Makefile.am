EXTRA_DIST           = README.md lib.sh basic.sh bridge.sh flood.sh length.sh option82.sh reload.sh reply.sh
CLEANFILES           = *~ *.trs *.log
TEST_EXTENSIONS      = .sh
TESTS_ENVIRONMENT    = unshare -mrun --map-auto
AM_TESTS_ENVIRONMENT = DHCPRELAYD=$(abs_top_builddir)/src/dhcprelayd; export DHCPRELAYD;

TESTS                = basic.sh
TESTS               += bridge.sh
TESTS               += flood.sh
TESTS               += length.sh
TESTS               += option82.sh
TESTS               += reload.sh
TESTS               += reply.sh
