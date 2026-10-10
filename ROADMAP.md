# PollikOS roadmap

The supported architecture is x86_64 only. The separate legacy kernel/runtime,
ELF32 API, Ring 0 desktop, build target and CI branch were removed. The exact
change list and acceptance results are in [X64_ONLY.md](docs/migration/X64_ONLY.md).
A source component is not considered verified solely because it exists.

## Acceptance for this migration

- Build developer, SELFTEST and Production ELF64 from clean source inputs.
- QEMU: memory, user-copy, ELF64, processes/scheduler, VFS/storage and resource balance.
- Argon2id/login/session/elevation; native desktop/terminal/browser and app I/O.
- Guest TinyCC compile/link/run, pipes, signals, cleanup and high-memory cases.
- Confirm the legacy OS cannot be built and existing private images remain unchanged.

## Subsequent work

- Improve native application parity and measured interaction/graphics performance.
- Design and test native AHCI/NVMe and UEFI support separately.
- Extend the fixed-size filesystem with versioned format/compatibility design.
- Add multi-user policy, SMP/threading and a more general handle/object model.
- Improve Web standards coverage without moving parser execution into Ring 0.

These are future tasks, not claims of implementation or requirements to keep
a second operating-system target alive.
