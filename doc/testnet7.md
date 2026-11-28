# Sugarchain Testnet7

Start the CLI or Qt node with `-testnet` or `-chain=test`. The configuration section is `[test]`, and `getblockchaininfo` reports `"chain": "test"`.

| Parameter | Value |
| --- | --- |
| Genesis block hash | `14f06e88eec9b21214a1eb54eee3cc03ceaf78ead1b16e40d8a383a5ff3e5e24` |
| Genesis time (UTC) | 2026-11-25 03:00:00 |
| Genesis coinbase text | `Sugarchain En Testnet7 2026-11-25` |
| Genesis bits and nonce | `1f3fffff`, `1320` |
| Genesis Yespower hash | `001c572d18bd3bda32894e8a13f3a0440a46d4eb62a24d31dd91bdd2f77545a4` |
| Data and wallet subdirectory | `testnet7` |
| P2P message start bytes | `c7 ee 5a 71` |
| Default P2P and RPC ports | `47230`, `47229` |
| Bech32 receive address prefix | `tugar1q` |
| First mined block subsidy | 42.94967296 SUGAR |

Testnet7 starts from a fresh chain. Its ledger, peers, and data directory are separate from Testnet5. Existing Testnet5 balances do not carry over.

Testnet7 uses Yespower and SugarShield with a five-second target block interval. There are currently no built-in peer seeds. To connect to a known Testnet7 peer, use `-addnode=<host>:47230`.

The functional test starts two isolated nodes with the default chain-work policy. It verifies the genesis identity, address handling, subsidy, mining, relay, a confirmed wallet transfer, and wallet restoration after restart.


## Shared initial synchronization policies

Testnet7 shares the bounded initial-block-download policies used on mainnet. Platform eligibility, explicit argument overrides, peer permissions, proof validation and timeout caps are retained.

- On Windows x86-64 and Intel/Apple Silicon macOS, automatic per-peer IBD request limits are 64 with at least 4 GiB RAM and 128 with at least 8 GiB. Lower-memory and other-platform defaults remain 16; `-maxibdblocksinflight` overrides the automatic selection within the existing bounds.
- On Windows x86-64 and Apple Silicon macOS, eligible initial header synchronization can align its starting batch, overlap a subsequent header request with local Yespower checking, and account for bounded local proof time when evaluating peer deadlines.
- On all platforms, an already timely, matching full block at the head of the receive queue gets the same single dispatch opportunity before an IBD download timeout disconnects its peer. This does not extend permission, proof or repeated-deferral limits.

The Testnet7 default minimum chain work remains zero. A diagnostic requiring genuine two-pass header pre-synchronization must use a valid chain longer than the header batch and set the receiver's `-minimumchainwork` to the verified target's actual cumulative work. Confirm positive `presynced_headers`, subsequent header redownload, exact target hash/work and `initialblockdownload=false`; an ordinary short-chain download does not establish PRESYNC coverage.

For isolated correctness tests, `-regtest -test=sharedibd` opts into the shared policies on every platform, including hosts where production platform gates are normally inactive. Regtest's SHA256d proof algorithm, genesis and consensus rules are retained. Its normal behavior is unchanged without this option; `-test` options remain rejected outside Regtest. This mode covers control flow and cannot measure Yespower verification throughput.

Disk I/O optimizations and common Yespower verification caches are separate from these network eligibility policies. This change does not enable the inherited Bitcoin Signet or Testnet4 modes.
