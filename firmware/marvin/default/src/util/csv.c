#include "util/csv.h"

void csv_quote(const char *s, char *out, size_t n)
{
    size_t j = 0;
    if (n == 0) { return; }
    if (s == NULL) { s = ""; }

    if (j < n - 1) { out[j++] = '"'; }
    for (size_t i = 0; s[i] != '\0' && j < n - 2; i++)
    {
        if (s[i] == '"' && j < n - 3) { out[j++] = '"'; }   /* escape by doubling */
        out[j++] = s[i];
    }
    if (j < n - 1) { out[j++] = '"'; }
    out[j] = '\0';
}

int csv_split(char *line, char *fields[], int maxf)
{
    int nf = 0;
    char *p = line;

    while (nf < maxf)
    {
        if (*p == '"')
        {
            p++;
            char *w = p;            /* unescape in place */
            fields[nf++] = w;
            while (*p != '\0')
            {
                if (*p == '"')
                {
                    if (p[1] == '"') { *w++ = '"'; p += 2; }
                    else { p++; break; }   /* closing quote */
                }
                else { *w++ = *p++; }
            }
            *w = '\0';
            while (*p != '\0' && *p != ',' && *p != '\n' && *p != '\r') { p++; }
        }
        else
        {
            fields[nf++] = p;
            while (*p != '\0' && *p != ',' && *p != '\n' && *p != '\r') { p++; }
        }

        if (*p == ',') { *p = '\0'; p++; }
        else { *p = '\0'; break; }
    }
    return nf;
}
