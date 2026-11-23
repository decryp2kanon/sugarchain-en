### QoS (Quality of service) ###

> This inherited helper targets Bitcoin traffic on port 8333. It is not configured for Sugarchain En networking; the examples below describe the upstream helper.

This is a Linux bash script that will set up tc to limit the outgoing bandwidth for connections to the Bitcoin network. It limits outbound TCP traffic with a source or destination port of 8333, but not if the destination IP is within a LAN.

This means one can have an always-on bitcoind instance running, and another local bitcoind/bitcoin-qt instance which connects to this node and receives blocks from it.
