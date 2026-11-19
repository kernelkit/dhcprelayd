import pytest
import subprocess

@pytest.fixture()
def setup():
    print "[!] Start DHCP Relay Agent"
    args = ["../../dhcprelayd", "-d", "-f", "conf/basic.json"]
    proc = subprocess.Popen(args=args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    yield setup

    proc.kill()
    print "[!] Kill DHCP Relay Agent"
