#!/usr/bin/env python3
# Copyright (c) 2026 The Sugarchain En developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Regenerate packet vectors using the independent Python BIP324 implementation.

Only network-dependent expected outputs change. Inputs, packet indices, payloads,
AAD, ignore flags and expected ciphertext lengths remain unchanged.
"""

import argparse
import ast
from pathlib import Path
import re
import sys


PATTERN = re.compile(r"^    TestBIP324PacketVector\(\n(.*?)\);", re.MULTILINE | re.DOTALL)


def generate(args, magic, state_class, magic_bytes):
    """Derive all five expected outputs without calling the native cipher."""
    index, private, ours, theirs, initiating, content, multiply, aad, ignore = args[:9]
    state = state_class(initiating=initiating, net="vector")
    magic_bytes["vector"] = magic
    shared = state.v2_ecdh(bytes.fromhex(private), bytes.fromhex(theirs), bytes.fromhex(ours), initiating)
    state.initialize_v2_transport(shared)
    for _ in range(index):
        state.v2_enc_packet(b"", ignore=True)
    encrypted = state.v2_enc_packet(bytes.fromhex(content) * multiply, bytes.fromhex(aad), ignore)
    full_length, suffix_length = len(args[12]) // 2, len(args[13]) // 2
    assert not full_length or full_length == len(encrypted)
    return [
        state.peer["send_garbage_terminator"].hex(),
        state.peer["recv_garbage_terminator"].hex(),
        state.peer["session_id"].hex(),
        encrypted.hex() if full_length else "",
        encrypted[-suffix_length:].hex() if suffix_length else "",
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="replace expected outputs in the C++ fixture")
    parser.add_argument("--verify-bitcoin-input", action="store_true",
                        help="first verify the original BIP324 vectors against their Bitcoin mainnet oracle")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "test" / "functional"))
    from test_framework.messages import MAGIC_BYTES
    from test_framework.v2_p2p import EncryptedP2PState

    target = root / "src" / "test" / "bip324_tests.cpp"
    source = target.read_text(encoding="utf-8")
    count = 0

    def replace(match):
        nonlocal count
        args = ast.literal_eval("[" + match[1].replace("true", "True").replace("false", "False") + "]")
        if options.verify_bitcoin_input:
            assert generate(args, bytes.fromhex("f9beb4d9"), EncryptedP2PState, MAGIC_BYTES) == args[9:], count
        expected = generate(args, MAGIC_BYTES["mainnet"], EncryptedP2PState, MAGIC_BYTES)
        count += 1
        if not options.write:
            assert expected == args[9:], f"vector {count} differs from the independent Python implementation"
            return match[0]
        lines = match[1].splitlines()
        assert len(lines) == 14
        for i, value in enumerate(expected, start=9):
            lines[i] = '        "' + value + '"' + ("," if i < 13 else "")
        return "    TestBIP324PacketVector(\n" + "\n".join(lines) + ");"

    output = PATTERN.sub(replace, source)
    assert count == 7, f"expected seven complete packet vectors, got {count}"
    if options.write:
        target.write_text(output, encoding="utf-8")
    print(f"Verified {count} BIP324 packet vectors; Sugarchain mainnet magic={MAGIC_BYTES['mainnet'].hex()}")


if __name__ == "__main__":
    main()
