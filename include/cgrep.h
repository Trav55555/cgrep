/*
 * cgrep: ripgrep reimplemented on c-utils, as c-utils' first consumer.
 *
 * Usage: cgrep [-n] [-j N] PATTERN [PATH...]
 *        cgrep --version
 *
 * Prints the lines containing PATTERN (a literal byte string, for now),
 * using ripgrep-style non-terminal text output: a bare line for
 * one input, "PATH:" before each line for several, "N:" with -n. With no
 * PATH or a path of "-", reads in from its current position and leaves it
 * open. Input is blocking and read-ahead may consume unprinted bytes.
 *
 * Refills are checked for NUL before searching their text. Binary inputs
 * with a match print one warning with the first NUL's delivered-byte offset
 * and stop; text printed from previous refills remains. Newline patterns,
 * text lines longer than 8 MiB excluding LF, and delivered-byte counts
 * above ULONG_MAX are errors. No BOM stripping or encoding conversion.
 *
 * Exit status: 0 if any line matched, 1 if none did, 2 if any error
 * occurred. Input failures are reported on err and later paths searched;
 * observed output failures stop serial input or cancel prefetched workers.
 * Explicit -j1 through -j8 preserve argv-order output; default is serial.
 * Linux/glibc >= 2.34 regular-file workers have private input windows and
 * two bounded 1 MiB output slots each. Other targets, stdin and nonregular
 * inputs stay serial. Every started worker is joined before returning; a regular
 * file read may delay cancellation. POSIX SIGPIPE may terminate the CLI
 * instead of returning 2. Earlier output is not rolled back.
 */
#ifndef CGREP_H
#define CGREP_H

/* --version also prints the source revisions the build recorded. */
#define CGREP_VERSION "0.1.0"

#include <stdio.h>

int cgrep_main(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#endif
