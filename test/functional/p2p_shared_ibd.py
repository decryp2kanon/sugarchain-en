#!/usr/bin/env python3
# Copyright (c) 2026 The Sugarchain En developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise the opt-in shared Regtest PRESYNC lead with ordinary SHA256d proof."""
from test_framework.messages import CBlock, CBlockHeader, msg_headers, uint256_from_compact
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class SharedIbdTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ["-minimumchainwork=0x3000", "-debug=net"],
            ["-minimumchainwork=0x3000", "-debug=net", "-test=sharedibd"],
        ]

    def setup_network(self):
        self.setup_nodes()

    def headers(self):
        genesis_hash = self.nodes[0].getblockhash(0)
        genesis = self.nodes[0].getblockheader(genesis_hash)
        previous = int(genesis_hash, 16)
        headers = []
        for height in range(1, 4001):
            header = CBlock()
            header.nVersion = 4
            header.nBits = int(genesis["bits"], 16)
            header.nTime = genesis["time"] + height
            header.hashPrevBlock = previous
            header.hashMerkleRoot = height
            header.solve()
            previous = header.hash_int
            headers.append(CBlockHeader(header))
        return headers

    def start_presync(self, node, headers):
        node.disconnect_p2ps()
        self.wait_until(lambda: not node.getpeerinfo())
        peer = node.add_p2p_connection(P2PInterface(), supports_v2_p2p=False)
        peer.send_and_ping(msg_headers(headers[:2000]))
        self.wait_until(lambda: node.getpeerinfo()[0]["presynced_headers"] == 2000)
        return peer

    def run_test(self):
        headers = self.headers()
        lead_logs = ["PRESYNC lead getheaders", "PRESYNC lead committed"]
        for node, opt_in in zip(self.nodes, (False, True)):
            peer = self.start_presync(node, headers)
            with node.assert_debug_log(lead_logs if opt_in else [],
                                       unexpected_msgs=[] if opt_in else lead_logs):
                peer.send_and_ping(msg_headers(headers[2000:]))
                self.wait_until(lambda: node.getpeerinfo()[0]["presynced_headers"] == 4000)
            assert_equal(node.getblockchaininfo()["headers"], 0)
            assert_equal(node.getblockcount(), 0)

        # Linkage, target and work-floor checks still permit a lead here.
        # Invalid tail proof must discard it without adding any accepted header.
        opt_in = self.nodes[1]
        peer = self.start_presync(opt_in, headers)
        invalid = CBlockHeader(headers[-1])
        target = uint256_from_compact(invalid.nBits)
        while invalid.hash_int <= target:
            invalid.nNonce += 1
        with opt_in.assert_debug_log(
            ["PRESYNC lead getheaders", "header with invalid proof of work", "PRESYNC lead discarded"],
            unexpected_msgs=["PRESYNC lead committed"],
        ):
            peer.send_without_ping(msg_headers(headers[2000:-1] + [invalid]))
            peer.wait_for_disconnect()
        assert_equal(opt_in.getblockchaininfo()["headers"], 0)
        assert_equal(opt_in.getblockcount(), 0)


if __name__ == "__main__":
    SharedIbdTest(__file__).main()
