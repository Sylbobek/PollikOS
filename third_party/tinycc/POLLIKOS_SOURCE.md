# Vendored PollikOS TinyCC

This is the existing PollikOS port of TinyCC, pinned at local source commit
`1a40ee0c2375204964ded51fe500535105e80126`. The native PollikOS compiler uses
`TCC_TARGET_X86_64`; generic x86 encoding and upstream compiler definitions
are library internals, not additional PollikOS kernel targets.

Migration replaced an unmapped gitlink with these unchanged source files so
ordinary checkouts contain the compiler. Original Git metadata and a bundle
were preserved locally in `build/x64-only-migration/`.

The upstream licence remains in COPYING.
