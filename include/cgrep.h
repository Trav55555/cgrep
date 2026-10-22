/*
 * cgrep: ripgrep reimplemented on c-utils, as c-utils' first consumer.
 *
 * Usage: cgrep [-cilnvw] [-j N] PATTERN [PATH...]   (a PATH may be a directory)
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
 * Output keeps input order at every -j; the default is the online CPU
 * count, at most 32, where Linux/glibc >= 2.34 workers exist, else 1.
 * Workers start as listed inputs pile up, claim batches of consecutive
 * regular files, and have private input windows and two bounded 1 MiB
 * output slots each. Other targets, stdin and nonregular inputs stay
 * serial. Every started thread is joined before returning; a regular file
 * read may delay cancellation. POSIX SIGPIPE may terminate the CLI instead
 * of returning 2. Earlier output is not rolled back.
 */
#ifndef CGREP_H
#define CGREP_H

/* --version also prints the source revisions the build recorded. */
#define CGREP_VERSION "0.1.0"

#include <stdio.h>

int cgrep_main(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#endif
