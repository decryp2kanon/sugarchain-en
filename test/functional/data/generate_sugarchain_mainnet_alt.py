#!/usr/bin/env python3
# Copyright (c) 2026 The Sugarchain En developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/MIT.
"""Generate the native MAIN fixture offline using real Yespower on Linux.

Normal functional tests never run this generator. Allocate --build-dir on disk;
mining may take several minutes. Four persistent workers retain separate TLS.
"""

import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mining_mainnet import (  # noqa: E402
    FIXTURE_BLOCKS, GENESIS_HASH, GENESIS_TIME, INITIAL_BITS, INITIAL_SUBSIDY,
    fixture_block, next_bits, target_from_bits,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('sugarchain_mainnet_alt.json'))
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    repository = Path(__file__).resolve().parents[3]
    vendor = repository / 'src/crypto/yespower-1.0.1'
    library = args.build_dir / 'generate_sugarchain_mainnet_alt.so'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-O3', '-fPIC', '-shared', '-Wall', '-Wextra', '-Werror',
        '-I', str(vendor), str(Path(__file__).with_suffix('.c')),
        str(vendor / 'yespower-opt.c'), str(vendor / 'sha256.c'), '-o', str(library),
    ], check=True)
    miner = ctypes.CDLL(str(library.resolve()))
    miner.mine_header.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.POINTER(ctypes.c_ubyte),
                                  ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_int)]
    miner.mine_header.restype = ctypes.c_uint64
    stop = ctypes.c_int(0)
    timestamps, bits = [GENESIS_TIME], [INITIAL_BITS]
    nonces, headers = [], []
    previous_hash = GENESIS_HASH
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for height in range(1, FIXTURE_BLOCKS + 1):
            required_bits = next_bits(timestamps, bits)
            timestamp = GENESIS_TIME + height
            block = fixture_block(height, previous_hash, timestamp, required_bits, 0)
            header = (ctypes.c_ubyte * 80).from_buffer_copy(block.serialize(with_witness=False)[:80])
            target = (ctypes.c_ubyte * 32).from_buffer_copy(target_from_bits(required_bits).to_bytes(32, 'little'))
            stop.value = 0
            results = list(pool.map(lambda worker: miner.mine_header(header, target, worker, 4, ctypes.byref(stop)), range(4)))
            if (1 << 64) - 1 in results:
                raise RuntimeError('Yespower worker allocation/hash failure')
            winners = [result - 1 for result in results if result]
            if len(winners) != 1:
                raise RuntimeError(f'Expected one valid proof, got {results}')
            block.nNonce = winners[0]
            previous_hash = block.hash_hex
            nonces.append(block.nNonce)
            headers.append(block.serialize(with_witness=False)[:80])
            timestamps.append(timestamp)
            bits.append(required_bits)
            if height % 50 == 0 or height == FIXTURE_BLOCKS:
                print(f'Generated {height}/{FIXTURE_BLOCKS} native blocks', flush=True)
    data = {
        'genesis_hash': GENESIS_HASH, 'timestamps': timestamps[1:], 'nonces': nonces,
        'bits': bits[1:], 'final_hash': previous_hash,
        'headers_sha256': hashlib.sha256(b''.join(headers)).hexdigest(),
        'generation': {
            'algorithm': 'Yespower 1.0', 'N': 2048, 'r': 32, 'personalization_bytes': 74,
            'interval_seconds': 1, 'coinbase_value': INITIAL_SUBSIDY, 'workers': 4,
            'source': 'Sugarchain mainnet consensus and independent integer oracle',
        },
    }
    args.output.write_text(json.dumps(data, indent=2) + '\n', encoding='utf8')
    (args.build_dir / 'sugarchain_mainnet_alt.headers').write_bytes(b''.join(headers))
    print(f'Final hash: {previous_hash}')


if __name__ == '__main__':
    main()
