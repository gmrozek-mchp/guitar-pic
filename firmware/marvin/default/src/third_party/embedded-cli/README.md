# embedded-cli (vendored)

Single small-footprint command-line library used by marvin's serial console
(`default/src/console/console.c`). Used in static-allocation mode (the CLI is given a
fixed `CLI_UINT` buffer, so no `malloc`/`free` is exercised — satisfies the project's
static-only memory rule).

- **Upstream:** https://github.com/funbiscuit/embedded-cli
- **License:** MIT — see [LICENSE](LICENSE)
- **Pinned at:** `master` commit `8e796cbf2263f055d43b6ad99b9ddc8feece4cfd` (fetched 2026-06-12)

Files:
- `embedded_cli.h` — API (`lib/include/embedded_cli.h` upstream).
- `embedded_cli.c` — implementation (`lib/src/embedded_cli.c` upstream).

Vendored verbatim; do not edit. To update, re-fetch both files at a chosen upstream
commit and update the pin above.
