#!/usr/bin/env python3
"""Render validated tree-search medians as SVG, a badge and Markdown."""
import argparse
import json
import math
from pathlib import Path
from xml.sax.saxutils import escape

SHAPES = ('wide', 'manydirs', 'mixed')
QUERIES = ('files', 'lines')
TOOLS = ('cgrep', 'rg')
MODES = (('single', 'One thread'), ('default', 'Default threads'))


def first_line(text):
    return (text or '').strip().splitlines()[0] if (text or '').strip() else 'unknown'


def rows(report):
    if report.get('status') != 'SUCCESS':
        raise ValueError('report status is not SUCCESS')
    result = []
    for shape in SHAPES:
        for query in QUERIES:
            row = dict(shape=shape, query=query, **report['fixture']['shapes'][shape])
            for mode, _ in MODES:
                cell = {}
                for tool in TOOLS:
                    label = f'{shape}-{query}-{tool}-{mode}'
                    try:
                        ms = report['medians'][label]['elapsed_ns'] / 1e6
                    except KeyError:
                        raise ValueError(f'missing median: {label}') from None
                    if not math.isfinite(ms) or ms <= 0:
                        raise ValueError(f'invalid median: {label}')
                    cell[tool] = ms
                cell['ratio'] = cell['rg'] / cell['cgrep']
                row[mode] = cell
            result.append(row)
    return result


def modes_of(table):
    return [(key, title) for key, title in MODES if key in table[0]]


def geomean(values):
    return math.exp(sum(math.log(v) for v in values) / len(values))


def count(number, noun):
    return f'{number:,} {noun}' + ('' if number == 1 else 's')


def describe(row):
    tree = f"{row['shape']}: {count(row['files'], 'file')}, {count(row['directories'], 'dir')}"
    query = '-l WORD' if row['query'] == 'files' else 'WORD (lines)'
    return tree, query


def verdict(ratio):
    if ratio >= 1:
        return f'{ratio:.2f}x faster', 'good'
    return f'{1 / ratio:.2f}x slower', 'muted'


def meta(single, commit, runner):
    tools = single['tools']
    return dict(repeats=single['requested']['repeats'],
                rg=first_line(tools['rg']['version']),
                cgrep=first_line(tools['cgrep']['version']),
                cpu=single.get('cpu') or 'unknown CPU',
                commit=commit[:7], runner=runner)


def badge(table):
    """Summarize defaults against defaults when measured, else one thread each."""
    key, title = modes_of(table)[-1]
    message, tone = verdict(geomean([row[key]['ratio'] for row in table]))
    label = 'cgrep vs rg (defaults)' if key == 'default' else 'cgrep vs rg (1 thread)'
    return {'schemaVersion': 1, 'label': label, 'message': message + ' (geomean)',
            'color': '2ea44f' if tone == 'good' else 'lightgrey'}


def markdown(table, info):
    modes = modes_of(table)
    head = ['Tree', 'Query']
    for _, title in modes:
        head += [f'{title}: cgrep (ms)', 'rg (ms)', 'cgrep vs rg']
    lines = ['| ' + ' | '.join(head) + ' |',
             '| --- | --- |' + ' ---: | ---: | ---: |' * len(modes)]
    for row in table:
        cells = list(describe(row))
        for key, _ in modes:
            cell = row[key]
            cells += [f"{cell['cgrep']:.2f}", f"{cell['rg']:.2f}",
                      verdict(cell['ratio'])[0]]
        lines.append('| ' + ' | '.join(cells) + ' |')
    lines += ['', footer(info, modes), '']
    return '\n'.join(lines)


def footer(info, modes):
    text = (f"Median wall time of {info['repeats']} warm runs, process startup included. "
            f"{info['cgrep']} at {info['commit']}; {info['rg']}. "
            'One thread: cgrep -j1, rg -j1 --sort path. ')
    if len(modes) > 1:
        text += ('Default threads: no thread option, so each tool uses its own '
                 'default thread count. ')
    return text + (f"{info['runner']}, {info['cpu']}. "
                   'Shared CI runners are noisy: compare ratios, not absolute times.')


STYLE = """
.bg{fill:#fcfcfb}.rule{stroke:#e1e0d9}.ring{fill:none;stroke:rgba(11,11,11,0.10)}
.t1{fill:#0b0b0b}.t2{fill:#52514e}.t3{fill:#898781}.good{fill:#006300}.muted{fill:#52514e}
@media (prefers-color-scheme: dark){
.bg{fill:#1a1a19}.rule{stroke:#2c2c2a}.ring{stroke:rgba(255,255,255,0.10)}
.t1{fill:#f0efec}.t2{fill:#c3c2b7}.good{fill:#0ca30c}.muted{fill:#c3c2b7}}
text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
font-variant-numeric:tabular-nums}
.title{font-size:16px;font-weight:600}.group{font-size:12px;font-weight:600}
.head{font-size:12px;font-weight:600}
.cell{font-size:13px}.sub{font-size:12px}.note{font-size:11px}.strong{font-weight:600}
"""


def svg(table, info):
    modes = modes_of(table)
    pad, row_h, label_w, group_w = 24, 34, 400, 260
    width = label_w + group_w * len(modes) + pad
    group_y, head_y = 84, 104
    body_y = head_y + 14
    height = body_y + row_h * len(table) + 98
    # Right edges of the cgrep, rg and ratio columns in each group.
    columns = []
    for i in range(len(modes)):
        ratio = label_w + (i + 1) * group_w
        columns.append((ratio - 195, ratio - 125, ratio))
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
           '<title id="title">cgrep vs rg benchmark</title>',
           f'<desc id="desc">{escape(footer(info, modes))}</desc>',
           f'<style>{STYLE}</style>',
           f'<rect class="bg" width="{width}" height="{height}" rx="8"/>',
           f'<rect class="ring" x="0.5" y="0.5" width="{width - 1}" height="{height - 1}" rx="8"/>',
           f'<text class="t1 title" x="{pad}" y="36">cgrep vs rg, warm median wall time in ms</text>',
           f'<text class="t2 sub" x="{pad}" y="58">Lower is better. Commit '
           f'{escape(info["commit"])}; {escape(info["rg"])}.</text>',
           f'<text class="t3 head" x="{pad}" y="{head_y}">Tree and query</text>']
    for (key, title), (cgrep, rg, ratio) in zip(modes, columns):
        left = ratio - group_w + 20
        out.append(f'<text class="t2 group" x="{left}" y="{group_y}">{title}</text>')
        out.append(f'<line class="rule" x1="{left}" x2="{ratio}" y1="{group_y + 6}" '
                   f'y2="{group_y + 6}"/>')
        for x, label in ((cgrep, 'cgrep'), (rg, 'rg'), (ratio, 'cgrep vs rg')):
            out.append(f'<text class="t3 head" x="{x}" y="{head_y}" text-anchor="end">'
                       f'{label}</text>')
    for index, row in enumerate(table):
        top = body_y + index * row_h
        base = top + 22
        tree, query = describe(row)
        out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{top}" y2="{top}"/>')
        out.append(f'<text class="cell" x="{pad}" y="{base}"><tspan class="t1">{escape(tree)}</tspan>'
                   f'<tspan class="t2"> · {escape(query)}</tspan></text>')
        for (key, _), (cgrep, rg, ratio) in zip(modes, columns):
            cell = row[key]
            text, tone = verdict(cell['ratio'])
            out.append(f'<text class="t1 cell strong" x="{cgrep}" y="{base}" '
                       f'text-anchor="end">{cell["cgrep"]:.2f}</text>')
            out.append(f'<text class="t1 cell" x="{rg}" y="{base}" '
                       f'text-anchor="end">{cell["rg"]:.2f}</text>')
            out.append(f'<text class="{tone} cell strong" x="{ratio}" y="{base}" '
                       f'text-anchor="end">{text}</text>')
    end = body_y + row_h * len(table)
    out.append(f'<line class="rule" x1="{pad}" x2="{width - pad}" y1="{end}" y2="{end}"/>')
    notes = [f'{info["repeats"]} warm runs per cell, process startup included. '
             'One thread: cgrep -j1, rg -j1 --sort path.']
    if len(modes) > 1:
        notes[0] += ' Default: no thread option for either tool.'
    notes.append(f'{info["cgrep"]}; {info["rg"]}.')
    notes.append(f'{info["runner"]}, {info["cpu"]}. Shared runners are noisy: compare ratios.')
    for offset, note in enumerate(notes):
        out.append(f'<text class="t3 note" x="{pad}" y="{end + 24 + offset * 17}">{escape(note)}</text>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--report', type=Path, required=True, help='tree benchmark report')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--runner', default='GitHub-hosted ubuntu runner')
    args = parser.parse_args(argv)
    single = json.loads(args.report.read_text())
    table = rows(single)
    info = meta(single, args.commit, args.runner)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'bench.svg').write_text(svg(table, info))
    (args.out / 'badge.json').write_text(json.dumps(badge(table), indent=2) + '\n')
    (args.out / 'summary.md').write_text(markdown(table, info))


if __name__ == '__main__':
    main()
