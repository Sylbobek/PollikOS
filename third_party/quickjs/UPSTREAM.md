# QuickJS upstream

Official release: https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz

Archive SHA-256: b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a

License: MIT; see LICENSE and source notices.

PollikOS integration targets x86-64 userspace. POLLIKOS excludes unused host
headers and uses UTC and the real SDK allocator. POLLIK_NO_ATOMICS disables
the upstream pthread-dependent Atomics implementation; it does not emulate it.
No upstream build script is executed by the PollikOS build.
