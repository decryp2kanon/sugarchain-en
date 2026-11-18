# Sugarchain En native benchmarks — 2026-11-18

Local Linux x86_64 observations on AMD Ryzen 9 5950X 16-Core Processor (32 logical CPUs), Clang 21.1.8, Release, warnings as errors. Measurements used parent `4cff05555d0c722a899a0098f3e423dfc3496c11` plus the two native fixture adjustments in this change. The [structured record](benchmarks/sugarchain-en-native-2026-11-18-summary.json) includes process time and peak child RSS; RSS is not a sum across concurrent processes.

## Workload preservation

All 173 registered benchmarks passed the existing `tool_bench_sanity_check.py` functional entry point. The original 1,557 transactions and 999,887 serialized bytes remain. PoW-requiring validation/read/write fixtures change only header difficulty and nonce, using native genesis difficulty and nonce 330. Construction and the fixed Yespower proof assertion occur outside timing. This derivative is a stateless validation fixture, not an on-chain Sugarchain block. Pure deserialization keeps the original upstream bytes.

## Block measurements

One invocation used `-min-time=250` and the five-benchmark filter below. Values are nanobench medians in milliseconds per operation. The existing proof cache is warm after initial iterations; these are steady-state parse/check/read/write measurements, not cold Yespower costs. Transaction-script/UTXO connection is outside these stateless checks.

| Benchmark | Median ms/op |
|---|---:|
| DeserializeAndCheckBlockTest | 1.709 |
| DeserializeBlockTest | 1.306 |
| ReadBlockBench | 1.431 |
| ReadRawBlockBench | 0.067 |
| WriteBlockBench | 0.590 |

## Header and recovery components

One invocation per worker setting used 6,000 genuine headers and an 8 MiB cache. Cold batch verification took 17.4437s with one worker and 3.44577s with eight; both made exactly 6,000 Yespower calls. Repeating the batch took 0.00640344s and 0.00642682s respectively, with zero Yespower calls. The separate serial block-proof pass also reused all warm entries and recomputed 6,000 proofs after cache clearing. Worker count applies to batch verification, not that serial pass.

The presync/redownload component completed three rounds in 0.0457158s, after initial input proof verification outside its timed region. This is a bounded component measurement, not network IBD or full-node startup. Persistent interruption/recovery coverage is in the [native validation record](sugarchain-en-native-validation-2026-11-18.md).

These observations describe existing verifier/cache behavior. CPU affinity and frequency governor were not fixed. No before/after production comparison or new optimization is claimed; item 12 requires independent evidence before proposing one. CI, Windows and macOS were not run.

## Reproduction

Enable `BUILD_BENCH` in the Linux candidate build and build `bench_bitcoin`, `bench_header_pow`, and `bench_headers_sync`. Run `tool_bench_sanity_check.py` through the generated functional runner. Select `(DeserializeBlockTest|DeserializeAndCheckBlockTest|WriteBlockBench|ReadBlockBench|ReadRawBlockBench)` with `-min-time=250` and `-output-json`/`-output-csv`. Run `bench_header_pow src/test/data/sugarchain_headers.raw 6000 1 8 --block-stage`, then the same command with worker count 8. Run `bench_headers_sync src/test/data/sugarchain_headers.raw 6000 3`.
