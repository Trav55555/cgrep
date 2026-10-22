import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

import benchmark_tree as bench


class BenchmarkTest(unittest.TestCase):
    def test_deterministic_fixtures(self):
        with tempfile.TemporaryDirectory() as tmp:
            roots = [Path(tmp) / name for name in ('a', 'b')]
            info = [bench.fixtures(root, quick=True) for root in roots]
            def snapshot(root):
                return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in root.rglob('*') if p.is_file()}
            self.assertEqual(info[0], info[1])
            self.assertEqual(snapshot(roots[0]), snapshot(roots[1]))
            self.assertTrue(all(0 < p['planted_files'] < p['files'] for p in info[0]['shapes'].values()))

    def test_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def fake(data, code=0, stderr=''):
                return [sys.executable, '-c', f'import sys; sys.stdout.buffer.write({data!r}); sys.stderr.write({stderr!r}); sys.exit({code})']
            configs = {tool: fake(b'a\nb\na\n') for tool in bench.TOOLS}
            configs['rg-default'] = fake(b'a\na\nb\n')
            bench.validate(configs, root, root)
            for tool, command in [('cgrep-default', fake(b'a\na\nb\n')),
                                  ('rg-default', fake(b'a\nb\n')),
                                  ('cgrep-single', fake(b'', 1)),
                                  ('rg-single', fake(b'', 2)),
                                  ('rg-default', fake(b'a\nb\na\n', stderr='warning'))]:
                changed = dict(configs, **{tool: command})
                with self.subTest(tool=tool, command=command), self.assertRaises(ValueError):
                    bench.validate(changed, root, root)


if __name__ == '__main__':
    unittest.main()
