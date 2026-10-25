# Polyomino Finder
*Polyomino Finder* is used to find polyominoes in specific classes. A polyomino is in a class when  
all of the groups of squares making it up with the same neighbour counts have an equal amount  
of squares. The software aims to find any undiscovered classes.

# Polyomino class search — C++20

A command-line implementation of the uploaded `polyomino_search_optimised.html`.
A shape must be **edge-connected**. Its cell labels count all **eight** neighbours,
including diagonals. A class such as `1/2/4` requires exactly those labels, each
appearing the same positive number of times. The 79 classes in the request are
built in as the default target set.

## Build and run

Requires a C++20 compiler and threads; no third-party libraries or network services.
Python 3 is needed only for the independent CLI tests.

```sh
make -j4
make test
./build/polyomino-search --threads 5 --seconds 300 --output results.jsonl
```

CMake is also supported when installed:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j4
ctest --test-dir build-cmake --output-on-failure
```

The binary writes JSON Lines to stdout or `--output`; diagnostics go to stderr.
The output file is replaced, so use a new path to retain an earlier run.
Ctrl-C writes final statuses and statistics, then exits with code 130. Normal
completion, including UNKNOWN, exits 0; invalid input or an internal failure exits 1.

### Search one class or many

```sh
# One witness for a selected class; choose multiplicities explicitly.
./build/polyomino-search --target 1/2/4 --min-each 2 --max-each 20 --threads 5 --seconds 300

# Repeat --target to restrict a batch. No --target means all 79 supplied classes.
./build/polyomino-search --target 2/5/8 --target 3/6/8 --seconds 300

# As many distinct witnesses as possible before the deadline.
./build/polyomino-search --target 1/3/4/5 --many --seconds 60

# Each worker stops after contributing three NEW, globally distinct witnesses.
./build/polyomino-search --many --per-thread 3 --threads 5 --seconds 300

# Stop after two distinct witnesses per target (across all workers).
./build/polyomino-search --solutions 2 --threads 5 --seconds 300
```

`--min-each`/`--max-each` default to 2/8 and support 1..64. This is a
multiplicity range, not a total-cell range. `--seconds 0` removes the deadline.
A quota is a **cap**, not a guarantee: a hard or impossible class can keep a worker
searching indefinitely with no deadline. In particular, unlimited `--many` without
a deadline deliberately keeps searching. Workers shuffle eligible class/size jobs
and spend three annealing restarts on each job per sweep. Found classes leave the
queue once their global solution quota is met. `--moves-per-thread N` supplies an exact per-worker proposal cap for repeatable
workloads. It can finish sooner if target/witness quotas are met. Search-only
options `--strategy mixed|anneal|cold` choose the original schedule, a shorter
colder schedule, or a portfolio (the default). Mixed mode assigns alternating
workers to the two schedules; one worker alternates between sweeps. Experiments
found neither schedule consistently best, so both remain available.

Workers use independent PRNG streams
and local search state; they lock only when publishing a witness. A single worker
with a fixed seed and fixed work budget/termination condition is reproducible;
wall-clock truncation and thread scheduling change the final result set.

Witnesses are independently checked for connectivity and equal neighbour counts
before output. Translations, rotations and reflections are deduplicated globally.
`--many` keeps canonical witness keys in memory, so memory grows with the number
of witnesses retained. The annealer has a dynamically padded board sized to fit
any shape with the chosen number of cells, rather than the browser's 22x22 board.
It preserves connectivity on accepted moves.

### Exact bounded proofs

```sh
# Exhaustively check 1..4 copies of each label, but only shapes with <=12 cells.
./build/polyomino-search --mode exact --target 1/2/4 --min-each 1 --max-each 4 \
    --max-cells 12 --threads 5 --seconds 0

# Easy bounded impossibility example: no connected 3-cell shape is all type 1.
./build/polyomino-search --mode exact --target 1 --min-each 3 --max-each 3
```

The exact solver enumerates fixed polyominoes on the infinite lattice, once per
translation. It includes rotations and reflections in the traversal, then
canonicalizes witnesses. It shares one traversal across all targets/sizes and
splits it at depth four into disjoint tasks retaining the full exclusion state.
Worker count changes neither coverage nor the proof. Partial shapes are pruned
when their existing degrees cannot fit any requested final degree histogram:
each old cell's degree can increase by at most the number of remaining cells.
An ordered interval-matching test checks the available final label slots.
`--no-completion-pruning` disables this optimization for comparison; it leaves
the simpler monotone maximum-degree check enabled. Exact mode ignores the
search-only `--per-thread`, `--moves-per-thread` and `--strategy` options. `--solutions` controls witness output; it does
not truncate exact enumeration.

Enumeration grows exponentially. The default ceiling is 12 cells; the supported
maximum is 24, which is a safety limit, **not** a claim that enumerating 24 cells
is practical. For example, a seven-label class with two of each label requires
14 cells and is outside a 12-cell proof. No finite search bound proves absence
at every multiplicity.

Final statuses:

| Status | Meaning |
| --- | --- |
| `FOUND` | At least one independently validated witness proves existence. |
| `UNKNOWN` | No witness found; randomized search or interrupted exact enumeration supplies no absence proof. |
| `IMPOSSIBLE_IN_MULTIPLICITY_RANGE` | Necessary mathematical conditions exclude every requested multiplicity. |
| `IMPOSSIBLE_WITHIN_CELL_LIMIT` | Complete exhaustive enumeration found no witness satisfying **both** the requested multiplicity range and cell ceiling. |

Every result includes the multiplicity bounds; exact results also include
`max_cells` and `enumeration_complete`. An incomplete enumeration can still report
an independently established mathematical exclusion. A `FOUND` result does not
mean every solution was enumerated. Search failures never become impossibility
claims. Necessary conditions include degree-sum parity, degree <= N-1, the
connected-graph edge bound, isolation of type 0, two distinct extreme
cells having at most four neighbours, and the geometric degree-sum bound
`sum(degrees) <= 8*N - 4*ceil(2*sqrt(N))`. The last bound follows by counting
occupied rows, columns and diagonals: in each direction, adjacent pairs number
at most `N - nonempty_lines`; the row/column line counts and the two diagonal
line counts each have product at least `N`. It excludes, for example, four-of-each
`4/7/8` without making any random proposals.

A second bound examines the highest-degree cell subsets. If `M` selected centres
have `D` missing neighbour incidences, their union of 3x3 neighbourhoods requires
at least `M + 2*ceil(2*sqrt(M)) + 4 - D` occupied cells. Checking all prefixes of
the degree-sorted target histogram excludes additional sizes, including eight-of-
each `4/7/8`. These are necessary conditions, so passing them remains UNKNOWN.

### Verify a witness separately

```sh
# Extract any emitted JSON witness as whitespace-separated coordinates.
python3 - <<'PY'
import json
with open('results.jsonl') as f:
    witness = next(e for line in f if (e := json.loads(line))['event'] == 'found')
with open('shape.txt', 'w') as f:
    for x, y in witness['cells']:
        print(x, y, file=f)
PY
./build/polyomino-search --verify shape.txt
```

The verifier accepts integer coordinate pairs, one per line, with optional `#`
comments. It rejects duplicates, disconnected shapes and unequal label counts.
Its implementation recomputes neighbours and connectivity from a coordinate set;
it does not trust the annealer's caches.

## Optimizations and validation

- Incremental eight-neighbour degrees, cardinal degrees and histogram score;
  constant-time active-cell and frontier insertion/removal using swap deletion.
- Connected boundary growth; swaps must attach to a retained cell.
- A precomputed 256-entry local deletion-connectivity table; accepted proposals
  with ambiguous local connectivity use an early-exit, timestamped flood fill.
- Read-only proposal scoring rejects unfavourable swaps before changing cached
  state; a fast path skips common-neighbour tests for disjoint neighbourhoods.
- Annealing acceptance tables refreshed every 256 proposals, avoiding `exp` in
  the move loop; deterministic xoshiro128++ streams seeded through SplitMix64.
- Fixed-size stack frontiers in exact recursion avoid per-node heap allocation;
  equal-histogram/mask lookup avoids constructing shapes for non-solutions.
- Safe monotone maximum-degree and final-degree-capacity pruning, plus a shared
  exact traversal, rather than
  enumerating the same polyominoes independently for each requested target.
- Independent search workers and dynamically scheduled exact suffix tasks.

`--self-test` checks the known fixed-polyomino counts
`1, 2, 6, 19, 63, 216, 760, 2725` through eight cells, checks the geometric bounds against every fixed polyomino through ten cells,
plus 18,000 mutation
proposals with independent histogram, connectivity, degree and frontier checks.
The Python suite independently generates small shapes by set expansion, compares
all witnesses with both exact worker counts, and checks timeouts, proof scope,
argument errors, quotas, and a nontrivial search witness.

The implementation has been profiled and tested with several search strategies
and mathematical bounds. It does not claim a universal optimum or a proof that
no further optimization exists.
Performance measurements and experiment decisions are in `BENCHMARKS.md`.

Ten independently validated witnesses from the requested batch are included in
[`examples/witnesses.json`](examples/witnesses.json). The local batch runs are in
`results.jsonl` and `results-latest.jsonl` (ignored generated output).
