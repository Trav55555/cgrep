| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 134.34 | 164.42 | 1.22x faster | 61.24 | 68.79 | 1.12x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 133.61 | 166.03 | 1.24x faster | 61.45 | 70.13 | 1.14x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 74.66 | 104.87 | 1.40x faster | 31.50 | 40.77 | 1.29x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 74.90 | 106.85 | 1.43x faster | 30.99 | 41.33 | 1.33x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 63.68 | 71.85 | 1.13x faster | 26.91 | 28.32 | 1.05x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 65.25 | 73.87 | 1.13x faster | 27.63 | 29.69 | 1.07x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (fa732b1, c-utils b74b8a9) at fa732b1; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
