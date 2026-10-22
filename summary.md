| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 123.84 | 154.30 | 1.25x faster | 58.86 | 67.84 | 1.15x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 123.36 | 156.59 | 1.27x faster | 58.45 | 67.66 | 1.16x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 69.09 | 95.50 | 1.38x faster | 29.98 | 41.43 | 1.38x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 69.09 | 96.43 | 1.40x faster | 30.18 | 41.41 | 1.37x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 58.66 | 66.86 | 1.14x faster | 25.92 | 27.08 | 1.04x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 59.97 | 68.69 | 1.15x faster | 26.80 | 27.58 | 1.03x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (6976780, c-utils b74b8a9) at 6976780; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
