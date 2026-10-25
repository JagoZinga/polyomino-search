"""Independent small-shape oracle, proof-scope checks, CLI and witness validation."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())


def run(*args, ok=True):
    p = subprocess.run([EXE, *map(str, args)], text=True, capture_output=True, timeout=30)
    if ok and p.returncode:
        raise AssertionError(p.stderr)
    return p


def events(*args):
    return [json.loads(line) for line in run(*args).stdout.splitlines()]


def normalize(cells):
    x = min(x for x, y in cells)
    y = min(y for x, y in cells)
    return tuple(sorted((a-x, b-y) for a, b in cells))


def canonical(cells):
    return min(normalize([(sx*(y if swap else x), sy*(x if swap else y))
                          for x, y in cells])
               for swap in [False, True] for sx in [-1, 1] for sy in [-1, 1])


def classify(cells):
    cells = set(map(tuple, cells))
    hist = {}
    for x, y in cells:
        d = sum((x+dx, y+dy) in cells for dx in [-1, 0, 1]
                for dy in [-1, 0, 1] if dx or dy)
        hist[d] = hist.get(d, 0) + 1
    seen = {next(iter(cells))}
    queue = list(seen)
    for x, y in queue:
        for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
            c = x+dx, y+dy
            if c in cells and c not in seen:
                seen.add(c)
                queue.append(c)
    if len(seen) != len(cells) or len(set(hist.values())) != 1:
        return None
    return '/'.join(map(str, sorted(hist))), next(iter(hist.values()))


class Tests(unittest.TestCase):
    def test_exact_against_independent_enumerator(self):
        shapes = {((0, 0),)}
        expected = set()
        for n in range(1, 7):
            self.assertEqual(len(shapes), [0, 1, 2, 6, 19, 63, 216][n])
            for shape in shapes:
                classification = classify(shape)
                if classification:
                    expected.add((*classification, canonical(shape)))
            grown = set()
            for shape in shapes:
                cells = set(shape)
                for x, y in shape:
                    for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
                        c = (x+dx, y+dy)
                        if c not in cells:
                            grown.add(normalize(cells | {c}))
            shapes = grown
        targets = sorted({x[0] for x in expected} | {'2/4'})
        target_args = [v for t in targets for v in ['--target', t]]
        for threads in [1, 4]:
            records = events('--mode', 'exact', '--min-each', 1, '--max-each', 6,
                             '--max-cells', 6, '--seconds', 0, '--many',
                             '--threads', threads, *target_args)
            actual = {(e['target'], e['each'], canonical(e['cells']))
                      for e in records if e['event'] == 'found'}
            self.assertEqual(actual, expected)
            results = [e for e in records if e['event'] == 'result']
            self.assertTrue(all(e['enumeration_complete'] for e in results))
            self.assertEqual(next(e for e in results if e['target'] == '2/4')['status'],
                             'IMPOSSIBLE_WITHIN_CELL_LIMIT')

    def test_parallel_exact_agrees(self):
        args = ['--mode', 'exact', '--target', '1/2/3', '--min-each', 1,
                '--max-each', 3, '--max-cells', 9, '--seconds', 0, '--many']
        one = events(*args, '--threads', 1)
        many = events(*args, '--threads', 4)
        def witnesses(es):
            return {canonical(e['cells']) for e in es if e['event'] == 'found'}
        self.assertEqual(witnesses(one), witnesses(many))
        self.assertEqual(one[-1]['exact_nodes'], many[-1]['exact_nodes'])

    def test_completion_pruning_equivalence(self):
        args = ['--mode', 'exact', '--target', '2/3/4/5', '--min-each', 3,
                '--max-each', 3, '--max-cells', 12, '--seconds', 0, '--many',
                '--threads', 3]
        pruned = events(*args)
        full = events(*args, '--no-completion-pruning')
        def witnesses(es):
            return {canonical(e['cells']) for e in es if e['event'] == 'found'}
        self.assertEqual(witnesses(pruned), witnesses(full))
        self.assertGreater(len(witnesses(pruned)), 100)
        for e in pruned:
            if e['event'] == 'found':
                self.assertEqual(classify(e['cells']), (e['target'], e['each']))
        self.assertLess(pruned[-1]['exact_nodes'], full[-1]['exact_nodes'])
        # A proof-producing negative case must retain its result, too.
        args = ['--mode', 'exact', '--target', '1/2/4', '--min-each', 4,
                '--max-each', 4, '--max-cells', 12, '--seconds', 0, '--threads', 1]
        pruned, full = events(*args), events(*args, '--no-completion-pruning')
        self.assertEqual(pruned[-2]['status'], 'IMPOSSIBLE_WITHIN_CELL_LIMIT')
        self.assertEqual(pruned[-2], full[-2])
        self.assertLess(pruned[-1]['exact_nodes'], full[-1]['exact_nodes']//2)

    def test_actual_search_and_independent_witness_check(self):
        es = events('--target', '1/3/4/5', '--min-each', 4, '--max-each', 4,
                    '--seconds', 5, '--threads', 1, '--seed', 1)
        found = [e for e in es if e['event'] == 'found']
        self.assertTrue(found, 'deterministic search did not find known feasible class')
        for e in found:
            self.assertEqual(classify(e['cells']), (e['target'], e['each']))
        self.assertGreater(es[-1]['moves'], 0)

    def test_quota(self):
        es = events('--target', '1', '--min-each', 2, '--max-each', 2,
                    '--many', '--per-thread', 1, '--seconds', 0, '--threads', 1)
        self.assertEqual(sum(e['event'] == 'found' for e in es), 1)

    def test_exact_proposal_budget(self):
        for strategy in ['mixed', 'anneal', 'cold']:
            es = events('--target', '2/5/8', '--min-each', 8, '--max-each', 8,
                        '--seconds', 0, '--threads', 3, '--moves-per-thread', 1024,
                        '--strategy', strategy)
            self.assertEqual(es[-1]['moves'], 3072)
            self.assertEqual(es[-2]['status'], 'UNKNOWN')
        # Duplicate immediate successes must still consume the proposal budget.
        es = events('--target', '1', '--min-each', 2, '--max-each', 2, '--many',
                    '--seconds', 0, '--threads', 2, '--moves-per-thread', 257)
        self.assertEqual(es[-1]['moves'], 514)
        self.assertEqual(sum(e['event'] == 'found' for e in es), 1)

    def test_timeout_is_unknown(self):
        for mode in ['search', 'exact']:
            es = events('--mode', mode, '--target', '2/5/8', '--min-each', 8,
                        '--max-each', 8, '--max-cells', 24, '--seconds', 0.000001,
                        '--threads', 2)
            self.assertEqual(es[-2]['status'], 'UNKNOWN')
            if mode == 'exact':
                self.assertFalse(es[-2]['enumeration_complete'])

    def test_necessary_conditions(self):
        for t, m in [('0/1', 2), ('1', 3), ('5/6/7/8', 3), ('1/2/4', 3), ('4/7/8', 4), ('4/7/8', 8), ('1/5', 20)]:
            es = events('--target', t, '--min-each', m, '--max-each', m,
                        '--seconds', 0, '--threads', 1)
            self.assertEqual(es[-2]['status'], 'IMPOSSIBLE_IN_MULTIPLICITY_RANGE')

    def test_verify(self):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)/'shape.txt'
            path.write_text('# domino\n0 0\n1 0\n')
            self.assertIn('VALID target=1 each=2', run('--verify', path).stdout)
            for bad in ['0 0\n0 0\n', '0 0\n2 2\n', '0 0\n1 0\n2 0\n', '2147483648 0\n']:
                path.write_text(bad)
                self.assertNotEqual(run('--verify', path, ok=False).returncode, 0)

    def test_saved_witnesses(self):
        path = pathlib.Path(__file__).resolve().parents[1]/'examples/witnesses.json'
        witnesses = json.loads(path.read_text())
        self.assertGreaterEqual(len(witnesses), 6)
        for e in witnesses:
            self.assertEqual(len(e['cells']), len(set(map(tuple, e['cells']))))
            self.assertEqual(classify(e['cells']), (e['target'], e['each']))
            with tempfile.NamedTemporaryFile(mode='w', suffix='.txt') as f:
                for x, y in e['cells']:
                    f.write(f'{x} {y}\n')
                f.flush()
                self.assertIn('VALID target='+e['target']+' ', run('--verify', f.name).stdout)

    def test_signal_preserves_unknown(self):
        import signal
        import time
        p = subprocess.Popen([EXE, '--target', '2/5/8', '--min-each', '8',
                              '--max-each', '8', '--seconds', '0', '--threads', '2'],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(0.08)
        p.send_signal(signal.SIGINT)
        out, err = p.communicate(timeout=10)
        self.assertEqual(p.returncode, 130, err)
        records = [json.loads(line) for line in out.splitlines()]
        self.assertEqual(records[-2]['status'], 'UNKNOWN')
        self.assertEqual(records[-1]['event'], 'stats')

    def test_output_write_failure(self):
        if pathlib.Path('/dev/full').exists():
            p = run('--target', '1', '--min-each', 2, '--max-each', 2,
                    '--output', '/dev/full', ok=False)
            self.assertNotEqual(p.returncode, 0)

    def test_bad_arguments(self):
        for args in [['--target', '1/1'], ['--target', '9'], ['--target', '1/'],
                     ['--threads', '0'], ['--seconds', 'nan'], ['--mode', 'oops'], ['--strategy', 'oops'],
                     ['--max-cells', '25'], ['--min-each', '-1'], ['--target']]:
            self.assertNotEqual(run(*args, ok=False).returncode, 0)

    def test_default_target_list(self):
        es = events('--seconds', 0.000001, '--threads', 1)
        results = [e for e in es if e['event'] == 'result']
        self.assertEqual(len(results), 79)
        self.assertEqual(len({e['target'] for e in results}), 79)


if __name__ == '__main__':
    unittest.main()
