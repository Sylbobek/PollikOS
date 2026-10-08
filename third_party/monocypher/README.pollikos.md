# Monocypher 4.0.2

Unmodified upstream scalar implementation, used for Argon2id in the kernel's
shared account module. No host hashing, allocator or threading is used in the
guest. PollikOS supplies and releases the work area and validates parameters.

Source: https://github.com/LoupVaillant/Monocypher/tree/4.0.2/src
License: upstream `LICENCE.md` (BSD-2-Clause or CC0-1.0).

SHA-256 of the downloaded sources:

- monocypher.c: `02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d`
- monocypher.h: `fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3`

The RFC 9106 Argon2id vector, including secret and associated data, is checked
by `tests/security_account_native.py`. x86_64 discards unused crypto functions
with function sections and linker garbage collection to retain the existing
bootstrap memory boundary.
