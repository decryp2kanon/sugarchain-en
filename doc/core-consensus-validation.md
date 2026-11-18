# Core consensus regression baseline

Base: `2ad1d18a20999d03115816fc37c09cd1bc40a394` (PR #19).

## Changes

Clang rejects proof-evidence code when existing caller-held locks are not declared. Declare `cs_main` requirements for proof restoration, block-index batch writes, and their lambdas. Match those contracts in unit and fuzz fixtures. Production lock acquisition and consensus rules are unchanged.

The functional transaction helpers used Bitcoin's 21-million-coin monetary ceiling. Use the existing production `MAX_MONEY` of 1,073,741,824 coins, including the helper's transaction validity check. The invalid-output and output-total tests now exercise the intended rejection paths.

Run the complete miner fixture on unmodified Sugarchain mainnet consensus, including YespowerSugar, CSV and Segwit active from height zero, the 42.94967296 SUGAR initial subsidy, and the configured 12,500,000-block halving interval. Replace the Bitcoin nonce table with 110 deterministic Sugarchain `BLOCKINFO` entries. Each entry must produce a genuine Yespower proof for the exact native header and coinbase; stale entries fail verification instead of silently searching from unrelated Bitcoin seeds. Preserve the complete witness commitment, alternating production block-submission entry points, explicit active-tip checks, transaction-selection scenarios, and sequence-lock checks.

Derive transaction funding from the actual native coinbase amounts. Under active CSV, a relative-nonfinal transaction forced into the mempool must make template validation fail; it must not be treated as valid by postponing activation. After the fixture advances the required height and median time, require successful selection of the four now-final transactions plus coinbase. Restore the accepted tip, UTXO best-block hash, and modified timestamps before later scenarios. Keep the mock clock deterministic. After synchronous fee changes, poll once rather than waiting on a frozen clock; fee thresholds and template assertions remain enforced.

Check exact subsidy values immediately before, at, and after the genuine 12,500,000-block halving boundary through the production subsidy function. Separately require the real-height mainnet assembler reward to match the configured subsidy and selected fees. These checks replace millions of fabricated indexes; they do not model a full active chain at the halving height. Retain the invalid-script rejection scenario following those checks.

Retain `mainnet_yespower_block_submission` as an independent algorithm and submission regression under the same unchanged mainnet parameters. Preserve the assembler coinbase and witness commitment, reject a SHA256d-valid but Yespower-invalid solution, solve genuine Yespower for two blocks, and submit through both `ProcessNewBlock` and `submitSolution`. Require each accepted block to become the script-valid active tip. No production consensus parameters are overridden.

Add `mainnet_future_header_boundary` using the authentic first mainnet header. The production header-validation path rejects an otherwise-valid header at +61 seconds, then accepts the identical header at the inclusive +60-second boundary.

## Validation

| Check | Result |
|---|---|
| Original Clang 21 proof-evidence compilation | Five thread-safety errors reproduced in two translation units |
| Same translation units after lock-contract fixes | Pass with `-Werror` and thread-safety checks enabled |
| Clang 21 daemon, CLI, and complete unit-test executable build | Pass, including the final native miner rebuild |
| Selected core CTest suites | Earlier revision: all 11 pass; final native revision: four relevant suites rerun and pass |
| Complete native miner suite | Both cases pass: 1,060 native fixture assertions and 52 algorithm/submission assertions |
| Native core rerun | 34 cases, 407,527 assertions pass across mining, PoW, SugarShield, and subsidy/validation suites |
| Integrated complete unit executable | Latest integrated rerun: 822 cases pass, zero failures, with pinned external assets; five existing subprocess-helper cases inactive |
| External script-asset case | With pinned QA data, the previously warned case passes all 141,917 assertions without warnings |
| Linux library and recovery CTest group | Six tests pass, including genuine isolated Yespower process/WAL recovery; not a physical power-loss test |
| Default functional configuration | 289 invocations: 273 pass after an isolated port retry; 16 retain their build/dependency skips |
| Shared-kernel C API | All 16 cases and 3,582 assertions pass with the public Sugarchain block-1 fixture |
| Chainstate-tool functional test | Complete script passes through both JSON-RPC and CLI using the actual shared-kernel sample |
| Block-index fuzz corpus | Both original CI failures reproduced; the repaired fixture passes both inputs and all 561 corpus files |
| Complete benchmark sanity check | All 173 registered benchmarks pass with native stateless-validation and disk-read fixtures |
| Integrated CI lint | 375 Python files under all 39 selected Ruff rules, plus file/executable, header-guard and locale checks, pass |
| Independent official reference Yespower | All 110 serialized fixture headers valid; known genesis oracle passes |
| New mainnet future-header case | One case, 12 assertions pass |
| Changed block-index fuzz translation unit | Syntax check passes with Clang thread-safety checks and `-Werror` |
| `feature_block.py` | Pass |
| `p2p_invalid_block.py`, v1 and v2 | Both pass |
| `p2p_sugarchain_header_pow.py` | Pass using real mainnet headers and block 1 |

The 11 selected suites are `amount_tests`, `chainstate_write_tests`, `header_pow_tests`, `miner_tests`, `pow_tests`, `script_tests`, `sugarshield_tests`, `txvalidation_tests`, `txvalidationcache_tests`, `validation_block_tests`, and `validation_tests`. The results above retain evidence from the earlier revision: ten suites passed the initial run and the earlier miner fixture passed its targeted rerun. The four functional invocations also passed before the complete native miner conversion. The final native revision separately passes all 34 selected cases in `miner_tests`, `pow_tests`, `sugarshield_tests`, and `validation_tests`: 407,527 assertions in 23.71 seconds. Within that run, the two miner cases pass 1,112 assertions in 2.14 seconds. These are selected-suite results, not a claim that every registered case or platform passes.

The local build uses `CMAKE_COMPILE_WARNING_AS_ERROR=ON`. Both C and C++ warnings remain errors, with no C warning exception. Informational Yespower optimization notices use portable messages. The vendor patch preserves proof output and per-thread workspaces; actual Windows and ARM results remain the responsibility of the platform CI jobs.

## Reproduction

Configure a Release Ninja build with Clang 21, `BUILD_GUI=OFF`, `ENABLE_IPC=OFF`, `BUILD_TESTS=ON`, `WITH_ZMQ=ON`, `WITH_USDT=ON`, `CMAKE_COMPILE_WARNING_AS_ERROR=ON`. No additional C or C++ warning suppression is required. Build `bitcoind`, `bitcoin-cli`, and `test_bitcoin`.

Run the selected CTest suites with `--output-on-failure --no-tests=error`, and run the three functional scripts above through the generated `test/functional/test_runner.py`. Use fresh isolated test directories. Run `test_bitcoin --run_test=miner_tests --report_level=detailed` for both mining cases. The direct boundary selection is `test_bitcoin --run_test=validation_block_tests/mainnet_future_header_boundary`.

The integrated check additionally runs the complete unit executable and default functional runner. The unit run initially warned because `DIR_UNIT_TEST_DATA` was unset. Rerun `script_assets_tests` with the 9,243,520-byte `script_assets_test.json` from QA-assets revision `2e9ddaf8b12b11485104d9b09eac0540ca6c0914`, verifying Git blob `6a69755a5e53f4212f265374e14f590dcbf86496` before execution. All 141,917 additional assertions pass without warnings.

The functional runner's first complete run exited unsuccessfully only because `rpc_bind.py --ipv6` encountered an occupied local port. All three `rpc_bind.py` variants passed on isolated rerun below the operating system's ephemeral-port range. The 16 default-runner skips require optional build components, tracing permissions, prior releases or other unavailable dependencies; no test skip was added. The separately built shared-kernel sample passes its complete C API test and `tool_bitcoin_chainstate.py` through both RPC and CLI modes.

## Native fixture provenance

The 110-block fixture is an isolated fork assembled from the actual mainnet genesis under unchanged production consensus, not a replay of the public historical chain. Generate each header with `VERSIONBITS_TOP_BITS`, timestamp `genesis.nTime + height * 5`, extranonce `height - 1`, the actual coinbase subsidy, and its witness commitment. Search once for a valid Yespower nonce, then record that nonce in `BLOCKINFO`. Normal fixture execution verifies the recorded proof and fails on stale data; it never searches for a replacement. The expected final header hash additionally binds the whole deterministic chain.

All 110 serialized headers were independently verified with the official revision's `yespower-ref.c` and `sha256.c`, independently of En's optimized implementation. The verifier uses N=2048, r=32, the official 74-byte personalization, and the decoded `0x1f3fffff` target. A known genesis proof checks byte order; invalid-nonce, truncated-input, and empty-input self-checks reject correctly. The serialized 110-header input has SHA256 `ecd2b2520dc8cd1f17e6b4bb73c98d04bea680409da2519102f39041d65e7148`.

## Reference and limits

Official reference revision: `64bc05ccc1dc2dcd4db9e86715c14dee12be6460`. Its [60-second allowance](https://github.com/sugarchain-project/sugarchain/blob/64bc05ccc1dc2dcd4db9e86715c14dee12be6460/src/chain.h#L36) and [strict greater-than rejection](https://github.com/sugarchain-project/sugarchain/blob/64bc05ccc1dc2dcd4db9e86715c14dee12be6460/src/validation.cpp#L3309) support the boundary regression.

The native miner fixture exercises mainnet block assembly, genuine Yespower validation, block submission, transaction selection, active sequence locks, and actual reward amounts across its 110-block chain. The independent two-block case verifies algorithm dispatch and both submission interfaces. Existing authentic mainnet-header fixtures and the new future-time case provide complementary evidence. Pure subsidy checks cover the real halving arithmetic; no full-height chain or block assembly at height 12,500,000 is constructed. The separate 512-block functional fixture covers the first SugarShield adjustments. These fixtures do not cover every activation boundary. Legacy upstream uses adjusted time; En uses `NodeClock`. The future-time case validates the allowance, not equivalence of clock-adjustment policy.

The official [miner case](https://github.com/sugarchain-project/sugarchain/blob/64bc05ccc1dc2dcd4db9e86715c14dee12be6460/src/test/miner_tests.cpp#L205-L523) retains its nonce table but comments out the test body. This change keeps executable assertions and replaces those Bitcoin-shaped proofs with native fixtures. Its one-time proof-generation method follows the active [official mining helper](https://github.com/sugarchain-project/sugarchain/blob/64bc05ccc1dc2dcd4db9e86715c14dee12be6460/src/test/test_bitcoin.cpp#L119-L160), which uses genuine Yespower. The production [official chain parameters](https://github.com/sugarchain-project/sugarchain/blob/64bc05ccc1dc2dcd4db9e86715c14dee12be6460/src/chainparams.cpp) remain the protocol reference.

En's separate REGTEST settings still retain Bitcoin-style genesis, SHA256d, subsidy, and halving fixtures. Official Sugarchain REGTEST differs. The native MAIN miner conversion does not establish official REGTEST compatibility or complete mainnet equivalence.

The isolated Yespower process/WAL crash-recovery test passes; this does not simulate a physical power loss. The full inherited CI matrix stays enabled. Production wallet/address compatibility and release readiness remain separate work.

## CI prerequisites and native expectations

The complete unit run exposed inherited Bitcoin assumptions outside the mining fixture. Keep all checks and adapt their inputs and expected values to Sugarchain:

- Regenerate all seven BIP324 packet vectors for mainnet magic `9feb4b9d` using the independent Python implementation. The generator first reproduced the original Bitcoin vectors before generating the Sugarchain outputs. Preserve every packet index, payload, associated-data value, ignore flag and rekey boundary. Align Python's mainnet magic with the production chain parameters.
- Check the native `MAX_MONEY` parsing boundary, including acceptance of 1,073,741,824 coins and rejection of one atomic unit more. Update the two invalid transaction vectors to exceed that same limit instead of Bitcoin's limit. Preserve both single-output and aggregate-output rejection.
- Expect the branded `sugarchain-cli` command in RPC examples while retaining argument and quoting checks.

Strict cross-platform builds also require portable Yespower pointer parameters and thread-local declarations. Optimization notices remain visible without becoming compilation errors. Replace only the small-container sorting fallback with `std::sort`; preserve the existing two-pass radix sort for large block-index sets. The existing sorting tests cover signed heights, extrema, repeated pointers and container sizes around the radix threshold.

Fix benchmark imports, executable modes, locale-dependent conversions, direct includes and header guards without excluding files or weakening lint rules. The header-guard checker normalizes punctuation in versioned paths to valid identifier characters.

The optimized Yespower implementation matches the independent official reference hash bytes for all 110 headers under Clang, GCC, generic C99 and a simulated MSVC preprocessor configuration. Each configuration also passes four-thread workspace reuse checks. The simulated configurations do not establish a real Windows or ARM platform pass.

## Functional mining and build coverage

Replace the Bitcoin mainnet difficulty fixture used by `mining_mainnet.py` with 512 fixed Sugarchain blocks. An independent integer oracle implements the configured 510-block SugarShield window, median-time endpoints, damping, adjustment limits and compact-target rounding. Normal tests load fixed proofs without mining replacements. Check full block acceptance and current, next and historical RPC difficulty, bits and targets across the first adjustments at heights 511 and 512. All 512 headers also pass the unmodified official reference Yespower implementation. The original Bitcoin fixture remains available alongside the native fixture and its offline generator.

Preserve functional error assertions while updating native monetary boundaries, fee units and branded tool messages. Wait for asynchronous orphan processing before checking the same rejection log and exact mempool contents. Retain strict `getblockchaininfo` field checks, including the existing `historicalpow` extension. Local reruns use separate test ports because running user applications occupy ports in the default range.

The common library owns the application-side `cs_main` definition required by PoW helpers. The standalone kernel keeps its independent definition. Startup text uses the kernel notification interface, with application UI forwarding outside the kernel library. Existing progress and logging assertions remain, with additional checks for callback delivery.

The kernel C API's MAIN validation case now uses the existing public Sugarchain block-1 fixture, including its witness commitment output. Its original validation, predecessor, transaction and reindex assertions remain. Network-neutral Bitcoin serialization vectors remain unchanged.

The block-index fuzzer already writes all generated entries under one key, so only the last survives. Reconstruct that surviving header as the loader does and adjust only its nonce for the existing deterministic fuzz proof predicate. Preserve all corpus decoding and the loader success assertion. This fuzz predicate is not a genuine native-target mining check; the separate 110- and 512-header fixtures provide that coverage.

The large block-validation and disk-read benchmarks retain all 1,557 transactions and 999,887 serialized bytes from the upstream fixture. Adapt only the header target and fixed nonce, and verify the resulting Yespower hash against the independent official implementation. Build this stateless fixture outside the timed validation loop. Keep MAIN parameters and the original validation/read assertions. This fixture is not an on-chain Sugarchain block; the original serialization-only benchmark remains unchanged.

The standalone allocation probe compiles its driver and PoW implementation separately. A macro used only by this probe enables synthetic cache population; normal builds do not expose that helper. The growth smoke test releases proof storage from 151,138,016 to 32,736 bytes, flushes coin-cache entries, and successfully performs 100,000 subsequent coin lookups. A below-threshold cache remains unchanged as intended.

## Final platform prerequisites

The block-index-tree fuzz target initializes both its wall clock and mock steady clock. Constructing TestingSetup connects genesis and updates the tip log, so steady time must be mocked before setup as well as reset for each input. The original target reproduces the nondeterminism guard failure without any input; the repaired target passes empty initialization and all 349 pinned corpus inputs, with each downloaded Git blob verified. Keep all chain, pruning and cleanup assertions and the global nondeterminism guard.

Attach the existing Windows application manifest helper to the header-sync benchmark executable. Windows compilation and linking had succeeded before the unmodified manifest gate rejected the executable with no resource section. Configure-only resource probes confirm the manifest wiring; actual MSVC execution remains a CI requirement.

The header-PoW tests use a local RAII joining thread rather than C++20 stop-token types missing from older platform libraries. The scanner cancellation callback sets its existing interrupt and wakes both condition-variable gates before joining. Preserve every existing interleaving and assertion; add a caught-exception cleanup regression. Nine focused cases pass 14,377 assertions, and the translation unit compiles strictly with instrumentation both enabled and disabled. A counted two-pass loop keeps the same cache-pressure checks without an unused variable in non-instrumented builds.

The LevelDB bytewise comparator now expresses its three intended byte/character conversions explicitly. The existing unsigned ordering and 0xff increment guard remain unchanged. The original code reproduces three integer-sanitizer diagnostics; the fixed code passes 2,124,524 comparisons and boundary checks in each of signed- and unsigned-char modes under address, undefined and integer sanitizers. Original unsanitized and fixed sanitized transcripts are byte-identical.

Use the count/character string insertion overload in both progress grouping helpers. This preserves output while avoiding GCC 12.2's optimized overlapping-copy diagnostic. The exact original warning is reproduced; strict GCC 12.2 compilation and boundary checks through UINT64_MAX pass after the two-line change.

Historical, checksum-verified Bitcoin compatibility fixtures retain their executable filenames and expect a different default configuration filename. Resolve historical names only inside an explicitly selected release directory and pass that fixture's configuration explicitly. Prefer native names when present and retain modern binary overrides and configuration-default tests. The complete unsupported-UTXO test passes with actual v0.14.3 daemon and CLI, using both RPC modes; broader historical compatibility still requires validation. These archived binaries exercise the shared REGTEST consensus and database/wallet formats, not historical Sugarchain MAIN; native MAIN validation remains separate.

The genuine crash-recovery harness uses a 120-second inactivity watchdog refreshed only by new, validated protocol events, plus a finite overall budget that respects the existing CI timeout factor. Preserve every original assertion and all 6,000 real headers, eight crash points, both worker counts, repeated crashes, repair and rejection checks. Ten bounded watchdog regression scenarios pass. The full genuine test passes with the final exported I/O wrappers: 16 boundary recoveries and all remaining cases in 190.95 seconds after the scratch-ownership integration. Full C++ sanitizer runs remain final-CI requirements.

Expose only the two Linux test I/O wrapper symbols needed by musl shared-library references. Keep production visibility flags unchanged. A standalone shared-library probe reproduces the hidden-symbol link failure and passes after the export, retaining static-call interception and successful real shared-library I/O.

## Yespower scratch ownership and cleanup

Replace the nontrivial C++ thread-local workspace with explicit ownership that passes the existing custom lint rule. Applications retain native thread/fiber slots, release each workspace on worker exit, and clean the current thread at process exit. Failed key creation, cleanup registration, allocation and publication preserve ownership. A later static destructor may allocate another workspace and register cleanup again. The published application key remains valid until process exit so surviving workers never use a deleted key.

The kernel target privately selects a bounded pool instead of registering callbacks that could outlive an unloaded library. Eight lazy slots have exclusive RAII leases; saturation uses a temporary workspace released at the end of the call. Initialization and proof failures release their leases, and cleanup permits later recreation. At the measured maximum scratch size, eight retained workspaces occupy 67,961,344 bytes, about 64.8 MiB. Library callers must complete active API calls before unload; previously calling threads may remain alive and idle.

Both actual production branches compile strictly and match all 512 independent official proofs serially and with four workers. Independent address, undefined and leak sanitizer checks cover application thread exit, main-thread and late cleanup, registration and allocation failures, and kernel proof failures. Nine simultaneous kernel calls verify eight retained slots plus a freed overflow allocation. Three real unload/reload cycles with the same surviving idle caller thread leave all 17 successful scratch allocations freed and no sanitizer findings. A single sequential timing comparison measured 1.50620 seconds for native-slot reuse and 1.54737 seconds for the bounded pool; this one sample does not establish general performance.

The actual final translation unit also cross-compiles to Windows COFF using genuine Windows and C++ library headers. The final shared-kernel integration build passes with C/C++ warnings as errors; its 16 C API cases pass 3,582 assertions, all 173 benchmark sanity cases pass, and the complete chainstate-tool script passes through both RPC and CLI. The final application unit run passes 822 cases and 28,219,615 assertions with pinned external assets. Native MSVC execution and the complete final CI matrix remain separate validation gates.
