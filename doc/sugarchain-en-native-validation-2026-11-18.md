# Sugarchain En native validation — 2026-11-18

Scope: Linux x86_64, Clang 21, Release, warnings as errors, wallet and Qt enabled. All runtime data was disposable and isolated. CI remained disabled. This ports the relevant native regression coverage from closed PR #20 without importing its unrelated production lifetime refactor.

## Native coverage

- Keep all 110 miner proof fixtures and the original mining, mempool, priority and sequence checks. Add real mainnet-rule block assembly/submission: a SHA256d-only solution fails and genuine Yespower solutions succeed.
- Keep the original table layout. Subsidy checks cover height 12,500,000 minus one, the halving height and the following height. They are scalar consensus checks, separate from real ordinary-height block assembly; no complete chain or template at the halving height is claimed.
- Keep all 70 positive key/address vectors and all original invalid vectors; add 24 native and cross-network negatives. Preserve the external BIP341 reference JSON and compare its witness program using the native `sugar` HRP.
- Use independent native signed-message expectations, genuine BIP324 vectors and 512 genuine-proof functional block fixtures, with reproducible generators and provenance.
- Check native mainnet wallet output policy, failed Taproot reservations without key consumption, automatic change fallback, and unchanged regtest Taproot availability.
- Preserve every directory-lock assertion and normal static-destructor lock release. The fork child now explicitly shuts down Boost.Test before `exit(0)`: debugger evidence identified a Boost.Test 1.90 output-sink teardown use-after-destruction. Four isolated output configurations passed after the fix. Full-suite reporting uses an XML result report and ordinary text logging; forked writers cannot produce a single well-formed shared XML event log.
- Test unsupported public-network CLI/config rejection before directory initialization and retain positive regtest startup/config coverage.

## Mainnet-scale evidence

The existing [IBD comparison](ibd-mainnet-comparison-2026-10-16.md) and [startup evidence](benchmarks/all-verified-startup-20261011.json) remain historical evidence. They are not measurements of this revision. In particular, loading a private 44,806,391-entry index and cancelling after 50,000 proof calls does not establish a complete current full-node startup. Current bounded crash/recovery measurements are recorded separately below.

## Validation results

| Local check | Result |
|---|---|
| Strict native build | Passed, Clang 21, warnings as errors |
| Core unit suite | 824 ordinary cases passed; the optional script-asset case completed separately below |
| External script corpus | Passed, 141,917 assertions; SHA-256 `cd789a58ec45916e1721cdd14e82ca4c93100959f1cef4e229b22e3bf539f095` |
| Qt tests | 39 passed, zero failures or skips |
| Functional suite, four jobs | 271 passed, 18 unavailable, zero failures |
| Bounded persistent proof recovery | 16 recovery cases: eight interruption points across 1/8 workers; repeated interruption, damaged-evidence repair and invalid-header rejection passed |

The complete unit run returned zero in 228.933s. Its sole optional-corpus warning was resolved by the separate corpus run. Functional tests returned zero in 457.518s. These are local elapsed times, not performance comparisons. Recovery used 6,000 genuine headers and returned zero in 185.915s.

The functional profile explicitly matches the application's mainnet/regtest policy. The two inherited Signet-only scripts remain intact but are unavailable for this candidate; they are not counted as passes. Six cross-chain wallet rejection assertions run against mainnet and regtest instead of Signet. Sixteen other component/platform/previous-binary prerequisites were unavailable in this run, including the benchmark executable, which is covered separately in work item 11. Legacy native-wallet migration and backup/restore were independently exercised in PRs #23/#25.

A running GUI and other existing local listeners occupied the default functional port range. They were preserved; the successful run used `TEST_RUNNER_PORT_MIN=18000` with initially unoccupied ports. All 107 utility scenarios, 26 existing address-error scenarios, nine witness-script expectations and nine added foreign-network negatives remain exercised.

Reproduction follows the Linux candidate build guide, enabling `BUILD_TESTS` and `BUILD_GUI_TESTS`. Run the unit binary, the external corpus case with `DIR_UNIT_TEST_DATA` pointing to the frozen corpus, `test_bitcoin-qt` with `QT_QPA_PLATFORM=minimal`, and `build/test/functional/test_runner.py --jobs=4`. The recovery driver is `test/lazy_pow_crash_recovery.py --test-binary=build/bin/test_bitcoin --output=/path/to/new/disposable-dir`; it deliberately kills only its own child processes.


## Limits

No Windows/macOS execution, CI run, IPC build, experimental kernel C API build or fuzz campaign is claimed. The five pre-existing disabled mock-process cases are helper entry points, not newly skipped regressions. Original wallets and operating-node data were not used. See the [Linux candidate guide](sugarchain-en-linux-release.md) for the supported candidate configuration.
