#!/bin/sh

ip netns add dhcprelayd-ns1
ip netns exec dhcprelayd-ns1 bash -c "ip link set lo up"

ip netns exec dhcprelayd-ns1 bash -c "(cd tests/integration; pytest)"
status=$?

ip netns del dhcprelayd-ns1

exit $status
