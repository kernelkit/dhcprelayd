EXTRA_DIST           = README.md lib.sh send.py basic.sh bridge.sh flood.sh forged.sh giaddr.sh hex.sh invalid.sh length.sh local.sh malformed.sh option82.sh policy.sh reload.sh reply.sh trusted.sh
CLEANFILES           = *~ *.trs *.log
TEST_EXTENSIONS      = .sh
TESTS_ENVIRONMENT    = unshare -mrun --map-auto
AM_TESTS_ENVIRONMENT = DHCPRELAYD=$(abs_top_builddir)/src/dhcprelayd; export DHCPRELAYD;

TESTS                = basic.sh
TESTS               += bridge.sh
TESTS               += flood.sh
TESTS               += forged.sh
TESTS               += giaddr.sh
TESTS               += hex.sh
TESTS               += invalid.sh
TESTS               += length.sh
TESTS               += local.sh
TESTS               += malformed.sh
TESTS               += option82.sh
TESTS               += policy.sh
TESTS               += reload.sh
TESTS               += reply.sh
TESTS               += trusted.sh
