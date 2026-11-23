# Various test vectors

> The Bitcoin fixture-generation examples below are retained as upstream test-data references. They are not instructions for accessing the Sugarchain mainnet or modifying a user wallet.

## sugarchain_block_1.hex

The complete 262-byte Sugarchain mainnet block at height 1, including its witness,
extracted from the bootstrap's previously validated 6000-block offline fixture.
Its hash is `ce8a0df339f2edceb99c5325c95b2b0ae752e29de1193f6113549f0e1cae7c91`;
its first 80 bytes match the first header in `src/test/data/sugarchain_headers.raw`.
The functional regression uses it to verify that cached header PoW does not
accept a mutated block body or poison subsequent acceptance of the genuine body.

## sugarchain_mainnet_alt.json

A 512-block isolated fork from the real Sugarchain mainnet genesis, used by
`mining_mainnet.py`. It is not a replay of the public chain. Every stored nonce
satisfies native Yespower 1.0 (N=2048, r=32, official 74-byte personalization).
The normal test submits fixed blocks and never mines, changes consensus
parameters, or substitutes SHA256d for Yespower.

Headers use version `0x20000000` and timestamps `1565881200 + height`, with
one-second spacing to reach the first SugarShield clamp. Coinbase outputs pay
`2^32` smallest units (42.94967296 SUGAR), use the BIP34 height encoding, final
sequence, and zero locktime. These blocks contain no witness data, so they do
not require a witness commitment despite Segwit being active from height zero.

Expected targets are calculated independently from the official 510-target
average and endpoint median times, with five-second target spacing, 16% upward
and 32% downward clamps, signed division truncating toward zero, and integer
division before multiplication. The first endpoint span is 505 seconds;
damping gives 2039 seconds and the 16% clamp raises it to 2142 seconds.

| Height | Compact target |
| --- | --- |
| 1 through 510 | `0x1f3fffff` |
| 511 | `0x1f35c28e` |
| 512 | `0x1f35be3d` |
| 513, next target checked without mining | `0x1f35b9ea` |

RPC difficulty retains the upstream diff-1 reference unit; the native initial
difficulty is not asserted to be 1. Current, next, and historical target fields
and their difficulty ratios are checked, with only relative `1e-12` floating
point tolerance.

The final block hash is
`0546accc8ff280e4291dad4f4b0775ceb1f55c4501cba80418e6a327f57eb371`.
The SHA256 of the concatenated 512 serialized headers is
`48024f246e47c44bd62a6a9f2b87234984d37281df120d67642a8ae8341333fc`.
All 512 proofs were independently verified against the unmodified official
`yespower-ref.c` and `sha256.c` from
`sugarchain-project/sugarchain@64bc05ccc1dc2dcd4db9e86715c14dee12be6460`.
The independent verifier also reproduced the official mainnet genesis proof
`0031205acedcc69a9c18f79b84790179d68fb90588bedee6587ff701bdde04eb` and checked
the exact compact targets before and after the first window. The complete
functional test accepted all 512 blocks and passed the mining/historical RPC
checks against the native node.

Offline regeneration requires Python 3 and a C11 compiler on Linux. Allocate a
registered disk workspace first, then run:

```sh
python3 test/functional/data/generate_sugarchain_mainnet_alt.py \
  --build-dir /path/to/registered-nvme-artifacts \
  --output /path/to/new-sugarchain-mainnet-alt.json
```

`generate_sugarchain_mainnet_alt.c` is an offline helper, not a node build target.
Four persistent workers search genuine nonce ranges and retain separate TLS;
generation took about 504 seconds on the recorded host. Parallel first-valid
selection can produce different valid nonces on regeneration. The fixed checked-in
fixture and header digest identify the exact chain used by the regression.
The generator also writes all serialized headers to the supplied build directory
for independent reference verification. The Bitcoin fixture below is preserved.

## mainnet_alt.json

For easier testing the difficulty is maximally increased in the first (and only)
retarget period, by producing blocks approximately 2 minutes apart.

The alternate mainnet chain was generated as follows:
- use faketime to set node clock to 2 minutes after genesis block
- mine a block using a CPU miner such as https://github.com/pooler/cpuminer
- restart node with a faketime 2 minutes later

```sh
for i in {1..2016}
do
 t=$(( 1231006505 + $i * 120 ))
 faketime "`date -d @$t  +'%Y-%m-%d %H:%M:%S'`" \
 bitcoind -connect=0 -nocheckpoints -stopatheight=$i
done
```

The CPU miner is kept running as follows:

```sh
./minerd -u ... -p ... -o http://127.0.0.1:8332 --no-stratum \
        --coinbase-addr 1NQpH6Nf8QtR2HphLRcvuVqfhXBXsiWn8r \
        --algo sha256d --no-longpoll --scantime 3 --retry-pause 1
```

The payout address is derived from first BIP32 test vector master key:

```
pkh(xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi/44h/0h/0h/<0;1>/*)#fkjtr0yn
```

It uses `pkh()` because `tr()` outputs at low heights are not spendable (`unexpected-witness`).

This makes each block deterministic except for its timestamp and nonce, which
are stored in `mainnet_alt.json` and used to reconstruct the chain without
having to redo the proof-of-work.

The timestamp was not kept constant because at difficulty 1 it's not sufficient
to only grind the nonce. Grinding the extra_nonce or version field instead
would have required additional (stratum) software. It would also make it more
complicated to reconstruct the blocks in this test.

The `getblocktemplate` RPC code needs to be patched to ignore not being connected
to any peers, and to ignore the IBD status check.

On macOS use `faketime "@$t"` instead.
