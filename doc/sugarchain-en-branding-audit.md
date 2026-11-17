# Sugarchain En branding audit and retained names

Current names are `Sugarchain En` (product), `SugarchainEn` (CMake project), and
`decryp2kanon/sugarchain-en` (repository). The default branch remains `core31`.
Executable names, `.sugarchain`, payment URI scheme, coin units, consensus,
network identifiers, and the P2P user-agent name are outside this rename.

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
