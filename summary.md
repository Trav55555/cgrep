| Tree | Query | One thread: cgrep (ms) | rg (ms) | cgrep vs rg | Default threads: cgrep (ms) | rg (ms) | cgrep vs rg |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | -l WORD | 53.45 | 76.03 | 1.42x faster | 31.89 | 40.45 | 1.27x faster |
| wide: 20,000 files, 1 dir | WORD (lines) | 48.27 | 72.08 | 1.49x faster | 31.14 | 38.15 | 1.23x faster |
| manydirs: 9,330 files, 1,555 dirs | -l WORD | 23.92 | 38.89 | 1.63x faster | 14.21 | 21.01 | 1.48x faster |
| manydirs: 9,330 files, 1,555 dirs | WORD (lines) | 23.94 | 39.35 | 1.64x faster | 14.22 | 20.68 | 1.45x faster |
| mixed: 5,016 files, 52 dirs | -l WORD | 48.00 | 56.93 | 1.19x faster | 22.68 | 25.34 | 1.12x faster |
| mixed: 5,016 files, 52 dirs | WORD (lines) | 48.95 | 59.03 | 1.21x faster | 22.90 | 25.78 | 1.13x faster |

Median wall time of 15 warm runs, process startup included. cgrep 0.1.0 (163a178, c-utils f7cb1fc) at 163a178; ripgrep 15.2.0 (rev e89fff89ac). One thread: cgrep -j1, rg -j1 --sort path. Default threads: no thread option, so each tool uses its own default thread count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, Intel(R) Xeon(R) 6975P-C. Shared CI runners are noisy: compare ratios, not absolute times.
