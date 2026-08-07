# qrcodegen (vendored)

QR Code generator library (C) by Project Nayuki — used by `ui/qr_art.c` to build the
System Info screen's product-page QR codes at boot.

- Upstream: https://github.com/nayuki/QR-Code-generator (`c/qrcodegen.c`, `c/qrcodegen.h`)
- Home: https://www.nayuki.io/page/qr-code-generator-library
- Pinned at commit `2c9044de6b049ca25cb3cd1649ed7e27aa055138` (2025-01-23)
- License: MIT. Upstream has no separate LICENSE file; the full text is in the header
  comment of each source file, which is retained verbatim.

Vendored as a copy rather than a submodule: two files, effectively frozen upstream, used
by one screen in one firmware project. (`third_party/oa-tc6-lib` is a submodule because
it is a shared Microchip library tracked across every node's firmware.)

Unmodified. No heap, no floating point — the caller supplies both buffers, so the
static-allocation rule holds. `qr_art.c` sizes them with
`qrcodegen_BUFFER_LEN_FOR_VERSION`.
