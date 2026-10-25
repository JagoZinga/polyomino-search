# Measurements and optimization experiments

Measured in this cloud workspace with GCC 14.2, `-O3 -DNDEBUG -std=c++20
-pthread`, using a five-CPU execution allowance. These are local observations,
not guarantees for other hardware or evidence that an algorithm is optimal.

## Uploaded JavaScript versus the native CLI

An approximately three-second, single-worker `4/7/8`, four-of-each run of the
original uploaded worker reported 7,228,928 proposals in its last progress message
(3.020 seconds wall time). The initial native port completed 12,570,888 proposals
in 3.000 seconds. This is approximately **1.7x the reported proposal rate**.
The JavaScript worker reports periodically, so its final count can lag; the
random number generators and trajectories differ. This is a throughput check,
not a controlled comparison of success probability or time to solution.

## Fixed-work native experiments

A small harness ran 3,000,000 proposals per sample at a fixed target and
multiplicity, the same cooling schedule and seed, restarting at the same phase
limit or when a solution was reached. Median seconds across three serial runs:

| Class | Each | Transaction/rollback baseline | Read-only delta + disjoint-neighbour fast path | Speedup |
| --- | ---: | ---: | ---: | ---: |
| 4/7/8 | 4 | 0.704 | 0.680 | 1.03x |
| 4/7/8 | 8 | 0.840 | 0.722 | 1.16x |
| 1/2/4 | 8 | 0.885 | 0.743 | 1.19x |
| 1/2/3/4/6/7/8 | 8 | 1.119 | 0.981 | 1.14x |

The retained optimization computes a proposal's histogram change without writing
the frontier, degree caches or occupancy. It rejects unfavourable proposals
before a transaction. When the old and new neighbourhoods do not intersect,
it skips all common-neighbour tests. Only score-accepted proposals change the
board; ambiguous connectivity can still require rollback.

An earlier version without the disjoint-neighbour fast path slightly regressed
the small case (0.702 versus 0.691 seconds, five-run medians), with a modest gain
in the 56-cell case (1.076 versus 1.126 seconds). That result led to the additional
fast path rather than a blanket claim that prediction was faster. Rejection
without a transaction also avoids the baseline's active-list reorder on rollback,
so future random choices can follow a different trajectory. These measurements
do not hold every subsequent proposed shape identical.

Exact enumeration through 12 cells for `1/2/3/4/5/6` at two-of-each visited
685,968 nodes in approximately 0.043 seconds after the stack-frontier changes,
versus approximately 0.046 seconds before. Those very short timings are noisy;
the structural gain is eliminating recursive frontier allocations. The exact
node count and witnesses agree across worker counts in the regression suite.

## Requested target batch

A five-worker, 45-second run over all 79 requested classes at multiplicities
2..8, using the first read-only prediction version, completed:

- 473,954,179 mutation proposals, approximately 10.53 million per second overall.
- 52,707 restarts.
- Six validated witnesses, retained in `examples/witnesses.json`:
  `1/2/4/5/6`, `3/5/6/8`, `3/4/5/6/8`, `1/3/4/5/7/8`,
  `2/3/4/5/7/8`, `2/3/5/6/7/8`.

The other 73 classes remained **UNKNOWN**. This was not an exact enumeration
and supplies no impossibility evidence for them. The retained witnesses were
subsequently checked by the independent Python classifier and the C++ coordinate
verifier, in addition to the runtime validator before publication.

## Search-quality experiments and retained portfolio

A paired-seed harness (`benchmarks/search_quality.cpp`) compared the original
annealing schedule, best-state retention, cold best-state retention, and fresh
cold restarts. The latter halves each restart's proposal allowance and uses
`T = .15 + 1.5*f*f` instead of `.22 + 3.4*f*f`.

| Class | Each | Seeds | Per-seed proposal cap | Original hits | Fresh cold hits |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3/5/6/8 | 6 | 30 | 250,000 | 22 | 27 |
| 1/2/4/5/6 | 5 | 30 | 250,000 | 7 | 1 |
| 2/3/5/6/7/8 | 8 | 20 | 300,000 | 14 | 20 |
| 3/4/5/6/8 | 8 | 20 | 300,000 | 8 | 14 |
| 2/3/4/5/7/8 | 6 | 20 | 300,000 | 7 | 4 |

Retaining the best shape did not consistently beat fresh cold restarts. It was
therefore discarded rather than adding persistent state without a clear gain.
The retained default is `--strategy mixed`: even workers use original annealing,
odd workers use cold restarts. Single-worker mixed runs alternate per sweep.
These are small experiments; the portfolio preserves useful diversity instead of
assuming the wins generalize to every target.

Reproduce the harness (strategy 0 = original, 1 = warm retention, 2 = cold
retention, 3 = fresh cold):

```sh
g++ -O3 -DNDEBUG -std=c++20 -pthread benchmarks/search_quality.cpp -o /tmp/search-quality
/tmp/search-quality 3/5/6/8 6 0 30 250000
/tmp/search-quality 3/5/6/8 6 3 30 250000
```

The CLI also exposes `--moves-per-thread N` for repeatable bounded production
runs. It counts actual mutation proposals, including proposals that leave the
board unchanged. Witness quotas can stop a worker sooner.

## Mathematical pruning

Later setup-time filters improve on the reported initial port without spending
any proposals. Besides the line-count degree-sum inequality, the final solver
checks the required footprint of high-degree subsets. For instance, `4/7/8` at
eight-of-each would need at least 28 cells but has only 24. It is excluded outright.
The earlier throughput workloads intentionally bypassed these later filters in
the kernel harness. They no longer represent useful searches for the final CLI.

## Final exact capacity pruning

For `1/2/4/6`, four-of-each with a 16-cell ceiling, one worker:

| Completion pruning | Exact nodes | Seconds |
| --- | ---: | ---: |
| Disabled | 138,206,536 | 7.583 |
| Enabled | 5,012,855 | 0.450 |

Both return the same bounded impossibility result. That is about **16.9x** faster.
A witness-rich `2/3/4/5`, three-of-each run retained all 565 canonical witnesses,
reducing nodes from 621,167 to 527,884 but slightly increasing runtime (0.077 to
0.082 seconds). The extra bound is therefore useful but not universally cheaper.
`--no-completion-pruning` is retained for comparison and alternative workloads.

Reproduce:

```sh
./build/polyomino-search --mode exact --target 1/2/4/6 --min-each 4 --max-each 4 \
    --max-cells 16 --threads 1 --seconds 0
# Repeat with --no-completion-pruning.
```

## Portfolio batch

A later five-worker 45-second run with the final cooling portfolio and footprint
filters completed 548,960,408 proposals, about 12.20 million per second. It found
nine requested classes, including four absent from the first run, and
analytically excluded `4/7/8` throughout multiplicities 2..8. The union of the two
runs supplies ten classes in `examples/witnesses.json`. Results from different
runs are not a controlled throughput comparison: target allocation, filtering
and successful classes change the workload.

## Remaining limits

The move kernel, acceptance schedule, restart allocation, board size, compiler
and CPU all affect performance. More proposals per second do not necessarily
mean more solutions. The tested strategies trade speed and exploration differently. These measurements
do not establish optimality or make any guarantee about unsolved classes.
