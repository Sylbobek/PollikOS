# Vendored dependencies

PollikOS's own sources use `AGPL-3.0-only`. This does not replace the licences
or copyright notices of the vendored components below. Keep their licence files
and notices with sources and distributed artifacts. Local adaptations are
documented in the component's upstream or PollikOS notes where available.

- BearSSL: `bearssl/LICENSE.txt`; kernel header copies retain upstream notices.
- Elk: `elk/LICENSE`; still used by the i386 browser and optional legacy x86_64 backend.
- QuickJS: `quickjs/LICENSE`, `quickjs/VERSION`, `quickjs/UPSTREAM.md`.
- OpenLibm: `openlibm/LICENSE.md`, `openlibm/UPSTREAM.md`.
- TinyCC: `tinycc/COPYING`, `tinycc/VERSION`; additional test notices remain in that tree.
- Monocypher: `monocypher/LICENCE.md`, `monocypher/README.pollikos.md`.
- libdom: `libdom/COPYING`, `libdom/UPSTREAM.md`; one pinned tree is retained.
- libcss: `libcss/COPYING`, `libcss/UPSTREAM.md`.
- libhubbub: `libhubbub/COPYING`, `libhubbub/UPSTREAM.md`.
- libparserutils: `libparserutils/COPYING`, `libparserutils/UPSTREAM.md`.
- libwapcaplet: `libwapcaplet/COPYING`, `libwapcaplet/UPSTREAM.md`.
- h264bsd: `h264bsd/LICENSE.md`, `h264bsd/UPSTREAM.md`.
- FAAD2: `faad2/COPYING`, `faad2/UPSTREAM.md`.
- minimp3: `minimp3/LICENSE`, `minimp3/UPSTREAM.md`.
- stb: licence and copyright notices are embedded in its vendored headers.

The NetSurf libraries are vendored with build generators but are not yet
linked into the normal Browser's HTML/CSS/layout pipeline. QuickJS is linked
into the normal x86_64 Browser. Source availability alone is not integration.
