EXTRA_DIST           = README.md lib.sh basic.sh bridge.sh flood.sh giaddr.sh hex.sh invalid.sh length.sh local.sh option82.sh policy.sh reload.sh reply.sh
CLEANFILES           = *~ *.trs *.log
TEST_EXTENSIONS      = .sh
TESTS_ENVIRONMENT    = unshare -mrun --map-auto
AM_TESTS_ENVIRONMENT = DHCPRELAYD=$(abs_top_builddir)/src/dhcprelayd; export DHCPRELAYD;

TESTS                = basic.sh
TESTS               += bridge.sh
TESTS               += flood.sh
TESTS               += giaddr.sh
TESTS               += hex.sh
TESTS               += invalid.sh
TESTS               += length.sh
TESTS               += local.sh
TESTS               += option82.sh
TESTS               += policy.sh
TESTS               += reload.sh
TESTS               += reply.sh
