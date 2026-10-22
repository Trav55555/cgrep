import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import render_bench as render


def report():
    return dict(status='SUCCESS', requested=dict(repeats=15), cpu='CPU <A&B>',
                tools={tool: dict(version=tool + ' 1.0\nextra') for tool in render.TOOLS},
                fixture=dict(shapes={shape: dict(files=20000, directories=1) for shape in render.SHAPES}),
                medians={f'{shape}-{query}-{tool}-{mode}': dict(elapsed_ns=ms * 1e6)
                         for shape in render.SHAPES for query in render.QUERIES
                         for mode, _ in render.MODES for tool, ms in [('cgrep', 2), ('rg', 4)]})


class RenderTest(unittest.TestCase):
    def test_medians_and_rejections(self):
        data = report()
        table = render.rows(data)
        self.assertEqual(len(table), 6)
        self.assertEqual(table[0]['single']['cgrep'], 2)
        self.assertEqual(table[0]['default']['ratio'], 2)
        for value in (0, -1, float('nan')):
            data['medians']['wide-files-rg-single']['elapsed_ns'] = value
            with self.assertRaises(ValueError):
                render.rows(data)
        del data['medians']['wide-files-rg-single']
        with self.assertRaisesRegex(ValueError, 'wide-files-rg-single'):
            render.rows(data)
        data['status'] = 'FAILED'
        with self.assertRaises(ValueError):
            render.rows(data)

    def test_verdict_and_badge(self):
        self.assertEqual(render.verdict(2), ('2.00x faster', 'good'))
        self.assertEqual(render.verdict(.5), ('2.00x slower', 'muted'))
        table = render.rows(report())
        for row, ratio in zip(table, [8, .5, 1, 1, 1, 1]):
            row['default']['ratio'] = ratio
        badge = render.badge(table)
        self.assertEqual(badge['label'], 'cgrep vs rg (defaults)')
        self.assertEqual(badge['message'], '1.26x faster (geomean)')

    def test_svg_and_main(self):
        data = report()
        image = render.svg(render.rows(data), render.meta(data, 'abcdef123', 'Runner <&>'))
        text = ''.join(ET.fromstring(image).itertext())
        self.assertIn('CPU <A&B>', text)
        self.assertIn('Runner <&>', text)
        self.assertIn('abcdef1', text)
        self.assertNotIn('extra', text)
        self.assertIn('prefers-color-scheme: dark', image)
        self.assertEqual(text.count('2.00x faster'), 12)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'report.json'
            path.write_text(json.dumps(data))
            out = Path(tmp) / 'site'
            render.main(['--report', str(path), '--out', str(out), '--commit', 'abc'])
            self.assertEqual(sorted(p.name for p in out.iterdir()), ['badge.json', 'bench.svg', 'summary.md'])
            self.assertIn('| 2.00 | 4.00 | 2.00x faster |', (out / 'summary.md').read_text())


if __name__ == '__main__':
    unittest.main()
