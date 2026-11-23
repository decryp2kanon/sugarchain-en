# Sugarchain En draft release notes

Linux is the first distribution target. This draft records candidate validation for Linux, Windows and macOS. Publication and the supported Linux distribution and architecture matrix require separate approval.

## Version

The current client metadata is 31.1.0, based on Bitcoin Core 31.1 (`CLIENT_VERSION_RC=0`, `CLIENT_VERSION_IS_RELEASE=false`). The upstream base version and RC values are retained; En is marked as a prerelease candidate rather than an approved release. En release approval, final artifact provenance, and any future version/tag policy are separate decisions.

## Native compatibility

- Restore official Sugarchain mainnet address encodings: SegWit `sugar1...`, legacy P2PKH `S...`, and P2SH `s...`. These prefixes replace the inherited Bitcoin mainnet address domain in En. Check any addresses obtained from an earlier En build before reusing them.
- Restore the native message-signature prefix, `Sugarchain Signed Message:` followed by a newline, for legacy address-based signatures. This aligns native Sugarchain message hashing; signatures made under the Bitcoin message domain are a different domain. The address/script support of the legacy signature API is unchanged.
- Keep the legacy-wallet migration path: migration-only Berkeley DB reading converts legacy wallets to descriptor/SQLite wallets through `migratewallet`. The operation creates a `.legacy.bak` backup and may return separate watch-only/solvable wallets. Ordinary legacy-wallet loading is not a substitute for migration. Retain the original backup, compare the migrated contents, and create fresh backups afterward. This draft does not claim validation of every legacy script configuration or production transaction history.
- Reject unsupported public testnet3, testnet4, and signet startup modes in En applications. Mainnet and isolated regtest are supported application modes; regtest is a development fixture, not an official public Sugarchain testnet.
- Respect the native mainnet no-Taproot deployment policy in wallet generation. Mainnet automatic receive/change selection and configured `bech32m` generation are rejected rather than advertised as deployed. Existing descriptor import/generic derivation and regtest behavior remain available. No Taproot deployment or new mainnet consensus rule is introduced.
- Repair production proof-cache lock annotations and common application linking while retaining native consensus selection.

## Linux candidate scope

The [Linux candidate guide](sugarchain-en-linux-release.md) defines the production build recipe and required package manifest. The current validated Linux configuration is monolithic (`ENABLE_IPC=OFF`) with seven installed executables. IPC-enabled packaging is not validated by that candidate; the product default is unchanged. Candidate integration includes the Sugarchain executable names, application/URI entry and AppStream ID `org.sugarchain.SugarchainEn`, installed icons, and project/bundled-asset notices under `share/doc/SugarchainEn`.

Four Korean/Japanese translation gaps in the encryption, address-signing, Tor-proxy, and fee explanations are filled. Autostart setup rejects executable paths containing `%` or `=`.

GUI settings migration retains the previous Komorebi profile and avoids overwriting an existing En profile. Wallet-format migration is separate. Packaging and message work must be reviewed at the integrated candidate revision before these notes become final.

Host-built Linux candidates may require matching shared libraries and Qt platform plugins.

## Validation and publication gates

- Complete the integrated production build and isolated installed-runtime/package checks.
- Adapt and run broad native regression coverage against the finished core; closed PR #20 is reference material, not a completed validation result for the final source.
- Reuse earlier IBD/startup evidence only where its source and scope still apply; repeat affected mainnet-scale and performance measurements separately. No new speedup or throughput figure is announced here.
- Record exact source/environment/options, inspected package contents, and private candidate checksums after validation.
- Require `CI` and `En Linux Qt` to pass on the integrated source and on the final `en` commit. Tagging and publication require separate approval.

Upstream [Bitcoin Core 31.1 release notes](release-notes.md) are preserved as history. Their download links, external infrastructure, supported platforms, and upgrade claims are not an En release announcement.
