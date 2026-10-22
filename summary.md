| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 105.23 | 126.38 | 1.20x faster | 43.89 | 51.25 | 1.17x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 102.03 | 126.25 | 1.24x faster | 41.89 | 51.33 | 1.23x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 59.45 | 80.50 | 1.35x faster | 22.15 | 27.45 | 1.24x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 58.70 | 82.41 | 1.40x faster | 21.64 | 28.09 | 1.30x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 107.17 | 47.35 | 2.26x slower | 48.59 | 19.80 | 2.45x slower |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 43.68 | 49.16 | 1.13x faster | 17.08 | 20.46 | 1.20x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (5cbc639, c-utils b74b8a9) at 5cbc639; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
