| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 133.82 | 164.46 | 1.23x faster | 61.38 | 69.24 | 1.13x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 134.10 | 166.78 | 1.24x faster | 61.46 | 69.90 | 1.14x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 76.90 | 107.15 | 1.39x faster | 32.39 | 42.08 | 1.30x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 76.74 | 107.45 | 1.40x faster | 32.01 | 42.15 | 1.32x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 63.28 | 71.85 | 1.14x faster | 26.38 | 28.50 | 1.08x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 65.03 | 74.16 | 1.14x faster | 27.36 | 30.09 | 1.10x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (591e40c, c-utils b74b8a9) at 591e40c; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
