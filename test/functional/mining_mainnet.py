#!/usr/bin/env python3
# Copyright (c) 2025-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test native Sugarchain mining RPCs across the first SugarShield adjustments.

The alternate MAIN chain uses fixed, independently checked Yespower proofs.
Expected targets are calculated locally from the official consensus equations,
without asking the node which target the test should expect.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.blocktools import DIFF_1_TARGET, create_coinbase, nbits_str, target_str
from test_framework.messages import CBlock, SEQUENCE_FINAL

import hashlib
import json
import math
import os

GENESIS_HASH = "7d5eaec2dbb75f99feadfa524c78b7cabc1d8c8204f79d4f3a83381b811b0adc"
GENESIS_TIME = 1565881200
INITIAL_BITS = 0x1f3fffff
INITIAL_SUBSIDY = 1 << 32
HALVING_INTERVAL = 12_500_000
AVERAGING_WINDOW = 510
TARGET_SPACING = 5
MAX_ADJUST_UP = 16
MAX_ADJUST_DOWN = 32
POW_LIMIT = (1 << 246) - 1
FIXTURE_BLOCKS = 512
COINBASE_SCRIPT_PUBKEY = "76a914eadbac7f36c37e39361168b7aaee3cb24a25312d88ac"


def target_from_bits(bits):
    """Decode a positive compact target, independently of node RPC values."""
    assert not bits & 0x00800000
    exponent, mantissa = bits >> 24, bits & 0x007fffff
    return mantissa >> (8 * (3 - exponent)) if exponent <= 3 else mantissa << (8 * (exponent - 3))


def bits_from_target(target):
    exponent = (target.bit_length() + 7) // 8
    mantissa = target << (8 * (3 - exponent)) if exponent <= 3 else target >> (8 * (exponent - 3))
    if mantissa & 0x00800000:
        mantissa >>= 8
        exponent += 1
    return exponent << 24 | mantissa


def median_time(timestamps, height):
    recent = sorted(timestamps[max(0, height - 10):height + 1])
    return recent[len(recent) // 2]


def next_bits(timestamps, bits):
    """Official SugarShield: endpoint MTP, dampening, clamping, integer order.

    The first adjustment follows 510 complete blocks after genesis. C++ signed
    division truncates toward zero; Python's negative floor division differs.
    """
    height = len(bits) - 1
    if height < AVERAGING_WINDOW:
        return INITIAL_BITS
    expected_span = AVERAGING_WINDOW * TARGET_SPACING
    actual_span = median_time(timestamps, height) - median_time(timestamps, height - AVERAGING_WINDOW)
    delta = actual_span - expected_span
    damped_span = expected_span + (abs(delta) // 4 if delta >= 0 else -(abs(delta) // 4))
    minimum = expected_span * (100 - MAX_ADJUST_UP) // 100
    maximum = expected_span * (100 + MAX_ADJUST_DOWN) // 100
    clamped_span = max(minimum, min(maximum, damped_span))
    total = sum(target_from_bits(value) for value in bits[-AVERAGING_WINDOW:])
    target = ((total // AVERAGING_WINDOW) // expected_span) * clamped_span
    return bits_from_target(min(target, POW_LIMIT))


def fixture_block(height, previous_hash, timestamp, bits, nonce):
    block = CBlock()
    block.nVersion = 0x20000000
    block.hashPrevBlock = int(previous_hash, 16)
    block.nTime, block.nBits, block.nNonce = timestamp, bits, nonce
    coinbase = create_coinbase(height=height, script_pubkey=bytes.fromhex(COINBASE_SCRIPT_PUBKEY), nValue=0)
    coinbase.vout[0].nValue = INITIAL_SUBSIDY >> (height // HALVING_INTERVAL)
    coinbase.nLockTime = 0
    coinbase.vin[0].nSequence = SEQUENCE_FINAL
    block.vtx = [coinbase]
    block.hashMerkleRoot = block.calc_merkle_root()
    return block


class MiningMainnetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.chain = ""

    def add_options(self, parser):
        parser.add_argument('--datafile', default='data/sugarchain_mainnet_alt.json',
                            help='Native fixed-proof block fixture (default: %(default)s)')

    def check_target(self, information, expected_bits):
        target = target_from_bits(expected_bits)
        assert_equal(information['bits'], nbits_str(expected_bits))
        assert_equal(information['target'], target_str(target))
        # RPC difficulty retains the upstream diff-1 reference unit. Compare
        # the native target ratio, allowing only double/JSON rounding error.
        expected_difficulty = DIFF_1_TARGET / target
        assert math.isclose(float(information['difficulty']), expected_difficulty, rel_tol=1e-12, abs_tol=0), (information['difficulty'], expected_difficulty)

    def check_mining_info(self, timestamps, bits):
        information = self.nodes[0].getmininginfo()
        height = len(bits) - 1
        assert_equal(information['blocks'], height)
        assert_equal(information['chain'], 'main')
        self.check_target(information, bits[-1])
        assert_equal(information['next']['height'], height + 1)
        self.check_target(information['next'], next_bits(timestamps, bits))

    def run_test(self):
        node = self.nodes[0]
        node.stderr.seek(0)
        node.stderr.truncate()
        path = os.path.join(os.path.dirname(os.path.realpath(__file__)), self.options.datafile)
        with open(path, encoding='utf8') as source:
            fixture = json.load(source)
        assert_equal(fixture['genesis_hash'], GENESIS_HASH)
        assert_equal(len(fixture['nonces']), FIXTURE_BLOCKS)
        assert_equal(len(fixture['bits']), FIXTURE_BLOCKS)
        assert_equal(fixture['timestamps'], list(range(GENESIS_TIME + 1, GENESIS_TIME + FIXTURE_BLOCKS + 1)))
        previous_hash = node.getbestblockhash()
        assert_equal(previous_hash, GENESIS_HASH)
        timestamps, bits = [GENESIS_TIME], [INITIAL_BITS]
        header_digest = hashlib.sha256()
        self.check_mining_info(timestamps, bits)
        self.log.info("Submit native Yespower blocks through the first SugarShield adjustments")
        for height, nonce in enumerate(fixture['nonces'], start=1):
            expected_bits = next_bits(timestamps, bits)
            assert_equal(fixture['bits'][height - 1], expected_bits)
            block = fixture_block(height, previous_hash, fixture['timestamps'][height - 1], expected_bits, nonce)
            serialized = block.serialize(with_witness=False)
            header_digest.update(serialized[:80])
            assert_equal(node.submitblock(serialized.hex()), None)
            previous_hash = node.getbestblockhash()
            assert_equal(previous_hash, block.hash_hex)
            assert_equal(node.getblockcount(), height)
            timestamps.append(block.nTime)
            bits.append(expected_bits)
            if height in (1, AVERAGING_WINDOW - 1, AVERAGING_WINDOW, AVERAGING_WINDOW + 1, FIXTURE_BLOCKS):
                self.check_mining_info(timestamps, bits)
        assert_equal(previous_hash, fixture['final_hash'])
        assert_equal(header_digest.hexdigest(), fixture['headers_sha256'])
        assert_equal(bits[AVERAGING_WINDOW + 1], 0x1f35c28e)
        assert_equal(bits[AVERAGING_WINDOW + 2], 0x1f35be3d)
        assert_equal(next_bits(timestamps, bits), 0x1f35b9ea)
        self.log.info("Check historical blocks retain their native targets and difficulty")
        for height in (1, AVERAGING_WINDOW - 1, AVERAGING_WINDOW, AVERAGING_WINDOW + 1, FIXTURE_BLOCKS):
            block_info = node.getblock(node.getblockhash(height))
            assert_equal(block_info['height'], height)
            self.check_target(block_info, bits[height])


if __name__ == '__main__':
    MiningMainnetTest(__file__).main()
