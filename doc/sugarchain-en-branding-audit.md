# Sugarchain En branding audit and retained names

Current names are `Sugarchain En` (product), `SugarchainEn` (CMake project), and
`decryp2kanon/sugarchain-en` (repository). The default branch is `en`.
Executable names, `.sugarchain`, payment URI scheme, coin units, consensus,
network identifiers, and the P2P user-agent name are outside this rename.

## Remaining product branding completed on 2026-11-18

The current project branch is `en`; the former branch name in earlier migration
reports and immutable backup/tag identifiers is historical evidence. New work is
submitted from a topic branch as a pull request against `en`.

The remaining active product labels were corrected in the wallet encryption
warning, payment-URI dialog, fee tooltip, and relative-data-directory diagnostic.
Project contribution/issue routes and current clone instructions now point to
this repository and its `en` branch.

The application and installer already used the official Sugarchain images.
The remaining Bitcoin image in the generated developer documentation was replaced
with an unmodified official Sugarchain PNG. Its source and license are recorded
in `share/pixmaps/SUGARCHAIN-ASSETS.md`. Debian package identity metadata now names
this fork while preserving the original authors and notices.

The translation audit matched **37 active source keys** to the actual Qt sources
and forms. **2,180 messages across 100 catalogs** now use the current source keys.
Only unambiguous proper-name edits were reused in existing translations: 1,375
messages retain their localized text with the product name corrected. For 772
messages whose spelling or grammar could not be changed reliably, the translation
is empty so Qt falls back to the correctly branded English source; another 33
were already empty. This is not a claim of newly completed translations for all
100 languages. The 52 catalog entries for a removed payment-handler warning were
deleted after confirming that no active Qt source uses that message.

`Partially Signed Bitcoin Transaction` remains the standard technical expansion
of **PSBT**. Its presence does not identify the application's coin or network.
Internal class/resource names, unit enums, the signed-message prefix, the P2P
user-agent, historical references, and third-party copyright/license notices
remain unchanged. No consensus, wallet serialization, payment URI scheme, amount
conversion, or operational data directory is changed by this work.

### Validation of the remaining branding changes

- Linux Release configuration and builds of `sugarchain-qt` and
  `test_bitcoin-qt` completed successfully, including all 100 translation catalogs.
- An independent `lupdate` extraction confirmed that all 37 updated source keys
  occur in the current Qt sources/forms. XML checks confirmed unchanged message
  placeholders, plural-form counts and translation attributes, and no changes to
  unrelated messages or PSBT terminology.
- A Qt `QTranslator` runtime check loaded all 100 generated QM catalogs and made
  11,100 lookups, checking preserved localized text, English fallback and removal
  of the old Bitcoin source keys.
- An offscreen GUI with a fresh, isolated regtest data/configuration directory
  initialized, answered `getblockchaininfo`, and exited with status 0 after RPC
  `stop`. It did not open the operational node's data directory or wallets.
- The full Qt test executable reported 36 passes and the same three failures
  already recorded in `sugarchain-en-renaming-results.md`: `URITests::uriTests`,
  `RPCNestedTests::rpcNestedTests` and `WalletTests::walletTests`. Settings
  migration (9) and payment IPC (8) checks passed. The complete suite is therefore
  **not** reported as passing.
- Template YAML, UI/translation XML, resource paths, official asset hashes and
  `git diff --check` were checked. Protected consensus, wallet, network, signed
  message, amount-conversion and P2P user-agent files were unchanged.

Doxygen is not installed in the validation environment, so actual HTML
generation was not tested. The new PNG and configured resource path were checked
directly. Windows/macOS native runtime and package installation were not tested;
their existing resource metadata was inspected. Strict parsing of the complete
Debian copyright file still reports the pre-existing missing Roboto Mono
copyright field; the changed identity/asset paragraphs passed separately.

## Permitted historical references

These references intentionally retain the former repository name so that the
original evidence remains identifiable. They are not current clone instructions:

| File | Retained reference | Reason |
| --- | --- | --- |
| `src/node/blockstorage.h` | Commit `c6ad3c0c5f` in the former repository URL | Immutable performance-design reference |
| `doc/block-index-write-memory.md` | Commit `90faec5afbe9a17cf0a04f4128d2e1576ef3b1bd` | Immutable experiment reference |
| `doc/ibd-rss-management.md` | Commit `90faec5afbe9a17cf0a04f4128d2e1576ef3b1bd` | Immutable experiment reference |
| `doc/pr11-memory-split.md` | Former repository PR 11 | Historical source attribution |
| `doc/sugarchain-en-renaming-plan.md` | Before/after names, old paths and PR 1 | Approved migration plan and rollback reference |
| This document | Former names in the audit scope | Explicit audit documentation |

If GitHub redirects cease working, repair access separately while preserving the
original commit IDs and historical context. Do not rewrite old commits or tags.

## Settings and installation migration

Former product strings are also necessary as **read-only migration keys**. Their
presence is permitted only when identifying old Qt profiles, old payment IPC
servers, old autostart entries, Windows installation records, or macOS identities.
Current labels, new profile names, package outputs, and current clone URLs must
use En. Migration fixtures may use former strings to exercise this compatibility.

Review these locations after integration rather than permitting every occurrence
in a directory: `src/qt/bitcoin.cpp`, `src/qt/guiconstants.h`,
`src/qt/guiutil.cpp`, `src/qt/paymentserver.cpp`, their dedicated migration helpers
and tests, and `share/setup.nsi.in`. `doc/files.md` describes the former Qt
application name solely to explain the first-run settings migration.

A retained former name must have an explicit historical or migration purpose;
new occurrences elsewhere require review. In particular, `Komorebi` in current
help output or generated manpage headings is not allowed.

## Preserved copyright notices

The exact notice `Copyright (c) 2026 The Sugarchain Komorebi developers` is
retained in the following 30 files. It identifies original authorship and must
not be changed as a product-label edit. No other live product text in these files
is exempt from review.

- `contrib/bench/block-download-ab.py`
- `contrib/bench/block-download.py`
- `contrib/bench/build-proof-trace.py`
- `contrib/bench/header-pow-ab.py`
- `contrib/bench/ibd_download.py`
- `contrib/bench/indexed-block-ab.py`
- `contrib/bench/indexed-block-import.cpp`
- `contrib/bench/observe-ibd.py`
- `contrib/bench/offline-ibd.py`
- `contrib/bench/offline-proof-restart.py`
- `contrib/bench/pow-cache-retention.cpp`
- `contrib/bench/presync-peer-overlap.py`
- `contrib/bench/test-observe-ibd.py`
- `contrib/bench/warm-block-import.py`
- `contrib/bench/yespower-call-profile.cpp`
- `contrib/bench/yespower-proof-trace.cpp`
- `contrib/bench/yespower-throughput.cpp`
- `doc/benchmarks/post-ibd-cache-probe.cpp`
- `src/bench/header_pow.cpp`
- `src/bench/headers_sync.cpp`
- `src/node/ibd_download.h`
- `src/test/header_pow_tests.cpp`
- `src/test/historical_pow_crash.h`
- `src/test/historical_pow_crash_tests.cpp`
- `src/test/ibd_tip_logging_tests.cpp`
- `src/test/sugarshield_tests.cpp`
- `test/functional/feature_fast_startup.py`
- `test/functional/p2p_ibd_download_limits.py`
- `test/functional/p2p_sugarchain_header_pow.py`
- `test/lazy_pow_crash_recovery.py`
