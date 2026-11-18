#!/usr/bin/env python3
# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test validateaddress for native mainnet with BIP173/350-derived vectors."""

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import assert_equal

# Native-HRP adaptations preserve each BIP vector's data and diagnostic fault.
# Original foreign-HRP cases remain wrong-network negatives.
INVALID_DATA = [
    # BIP 173
    (
        "tc1qw508d6qejxtdg4y5r3zarvary0c5xw7kg3g4ty",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid hrp
        [],
    ),
    ("sugar1qw508d6qejxtdg4y5r3zarvary0c5xw7kjjlkpt", "Invalid Bech32 checksum", [44]),
    (
        "SUGAR13W508D6QEJXTDG4Y5R3ZARVARY0C5XW7KDQEFR4",
        "Version 1+ witness address must use Bech32m checksum",
        [],
    ),
    (
        "sugar1rw5tdqc7n",
        "Version 1+ witness address must use Bech32m checksum",  # Invalid program length
        [],
    ),
    (
        "sugar10w508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kw5c2p04s",
        "Version 1+ witness address must use Bech32m checksum",  # Invalid program length
        [],
    ),
    (
        "SUGAR1QR508D6QEJXTDG4Y5R3ZARVARYVR9PV3H",
        "Invalid Bech32 v0 address program size (16 bytes), per BIP141",
        [],
    ),
    (
        "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sL5k7",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Mixed case
        [],
    ),
    (
        "SUGAR1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KJJLKp2",
        "Invalid character or mixed case",  # bc1, Mixed case, not in BIP 173 test vectors
        [43],
    ),
    (
        "sugar1zw508d6qejxtdg4y5r3zarvaryvq6y5q8t",
        "Version 1+ witness address must use Bech32m checksum",  # Wrong padding
        [],
    ),
    (
        "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3pjxtptv",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Non-zero padding in 8-to-5 conversion
        [],
    ),
    ("sugar1wc95fw", "Empty Bech32 data section", []),
    # BIP 350
    (
        "tc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq5zuyut",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # Invalid human-readable part
        [],
    ),
    (
        "sugar1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq0s05uu",
        "Version 1+ witness address must use Bech32m checksum",  # Invalid checksum (Bech32 instead of Bech32m)
        [],
    ),
    (
        "tb1z0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqglt7rf",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Invalid checksum (Bech32 instead of Bech32m)
        [],
    ),
    (
        "SUGAR1S0XLXVLHEMJA6C4DQV22UAPCTQUPFHLXM9H8Z3K2E72Q4K9HCZ7VQV09N5W",
        "Version 1+ witness address must use Bech32m checksum",  # Invalid checksum (Bech32 instead of Bech32m)
        [],
    ),
    (
        "sugar1qw508d6qejxtdg4y5r3zarvary0c5xw7k8w06yg",
        "Version 0 witness address must use Bech32 checksum",  # Invalid checksum (Bech32m instead of Bech32)
        [],
    ),
    (
        "tb1q0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq24jc47",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Invalid checksum (Bech32m instead of Bech32)
        [],
    ),
    (
        "sugar1p38j9r5y49hruaue7wxjce0updqjuyyx0kh56v8s25huc6995vvpqnjuofl",
        "Invalid Base 32 character",  # Invalid character in checksum
        [62],
    ),
    (
        "SUGAR130XLXVLHEMJA6C4DQV22UAPCTQUPFHLXM9H8Z3K2E72Q4K9HCZ7VQXC96VJ",
        "Invalid Bech32 address witness version",
        [],
    ),
    ("sugar1pw564znqg", "Invalid Bech32 address program size (1 byte)", []),
    (
        "sugar1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v8n0nx0muaewav252hmxdn",
        "Invalid Bech32 address program size (41 bytes)",
        [],
    ),
    (
        "SUGAR1QR508D6QEJXTDG4Y5R3ZARVARYVR9PV3H",
        "Invalid Bech32 v0 address program size (16 bytes), per BIP141",
        [],
    ),
    (
        "tb1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq47Zagq",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Mixed case
        [],
    ),
    (
        "sugar1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v07qclp09g",
        "Invalid padding in Bech32 data section",  # zero padding of more than 4 bits
        [],
    ),
    (
        "tb1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vpggkg4j",
        "Invalid or unsupported Segwit (Bech32) or Base58 encoding.",  # tb1, Non-zero padding in 8-to-5 conversion
        [],
    ),
    ("sugar1wc95fw", "Empty Bech32 data section", []),
]
VALID_DATA = [
    # BIP 350
    (
        "SUGAR1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KJJLKP2",
        "0014751e76e8199196d454941c45d1b3a323f1433bd6",
    ),
    # (
    #   "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7",
    #   "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262",
    # ),
    (
        "sugar1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qqzz38q",
        "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262",
    ),
    (
        "sugar1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7ke3zz8t",
        "5128751e76e8199196d454941c45d1b3a323f1433bd6751e76e8199196d454941c45d1b3a323f1433bd6",
    ),
    ("SUGAR1SW50QJSCRPA", "6002751e"),
    ("sugar1zw508d6qejxtdg4y5r3zarvaryvmy0lvx", "5210751e76e8199196d454941c45d1b3a323"),
    # (
    #   "tb1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesrxh6hy",
    #   "0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    # ),
    (
        "sugar1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesv52lx6",
        "0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    ),
    # (
    #   "tb1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesf3hn0c",
    #   "5120000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    # ),
    (
        "sugar1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesxr2k7x",
        "5120000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433",
    ),
    (
        "sugar1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vq6vlce7",
        "512079be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
    ),
    # PayToAnchor(P2A)
    (
        "sugar1pfees2788ax",
        "51024e73",
    ),
]


# Keep the original valid BIP350 encodings as Bitcoin-mainnet negatives.
# A foreign string using only Base58 characters reaches the Base58 checksum error.
BITCOIN_MAINNET_ADDRESSES = [
    ("BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kt5nd6y", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("BC1SW50QGDZ25J", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1zw508d6qejxtdg4y5r3zarvaryvaxxpcs", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvses5wp4dt", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvses7epu4h", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", "Invalid or unsupported Segwit (Bech32) or Base58 encoding."),
    ("bc1pfeessrawgf", "Invalid checksum or length of Base58 address (P2PKH or P2SH)"),
]


class ValidateAddressMainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = ""  # main
        self.num_nodes = 1
        self.extra_args = [["-prune=899"]] * self.num_nodes

    def check_valid(self, addr, spk):
        info = self.nodes[0].validateaddress(addr)
        assert_equal(info["isvalid"], True)
        assert_equal(info["scriptPubKey"], spk)
        assert "error" not in info
        assert "error_locations" not in info

    def check_invalid(self, addr, error_str, error_locations):
        res = self.nodes[0].validateaddress(addr)
        assert_equal(res["isvalid"], False)
        assert_equal(res["error"], error_str)
        assert_equal(res["error_locations"], error_locations)

    def test_validateaddress(self):
        for (addr, error, locs) in INVALID_DATA:
            self.check_invalid(addr, error, locs)
        for (addr, spk) in VALID_DATA:
            self.check_valid(addr, spk)
        for addr, error in BITCOIN_MAINNET_ADDRESSES:
            self.check_invalid(addr, error, [])

    def run_test(self):
        self.test_validateaddress()


if __name__ == "__main__":
    ValidateAddressMainTest(__file__).main()
