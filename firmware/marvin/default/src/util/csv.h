#ifndef MARVIN_UTIL_CSV_H
#define MARVIN_UTIL_CSV_H

#include <stddef.h>

/* Minimal CSV field helpers shared by the SD-card consumers (results, catalog).
 * Quote-aware and zero-alloc: csv_split rewrites the line in place and stores
 * pointers into it, csv_quote writes into a caller buffer. Fits the
 * static-allocation rule and FF_FS_MAX_FILES=1 (one line at a time). */

/* Write s into out as a quoted CSV field ("..."), doubling any embedded quote.
 * Always quoted, so embedded commas are safe. Truncates to fit. */
void csv_quote(const char *s, char *out, size_t n);

/* Split a CSV line in place into fields[], honoring double-quoted fields
 * (commas inside quotes, "" -> "). Returns the field count. */
int csv_split(char *line, char *fields[], int maxf);

#endif
