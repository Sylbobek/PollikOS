# x86_64 memory contract

Only the x86_64 target uses this implementation. The i386 PMM/VMM, ELF32 loader,
process ABI, heap and DMA drivers retain their existing types and behavior.

## Types and physical allocator

`memory.h` defines unsigned 64-bit `phys_addr_t`, `pfn_t` and `page_count_t`;
`virt_addr_t` is the architecture's `uintptr_t`; sizes use 64-bit `size_t`.
Physical addresses are never directly cast to dereferenceable pointers. CPUID
sets the address limit (up to 52 bits), independently of i386 pointer width.

The audited i386 `system.h` types, ELF32 `p_paddr/p_vaddr`, process frames, heap
addresses, page-table entries and RTL8139 address programming remain i386-only
interfaces. Future ports must convert them explicitly; the newer ELF64 loader
uses separate 64-bit structures rather than importing these i386 definitions.

PMM extends the existing bitmap approach with dynamically allocated sparse
metadata. Each metadata page tracks up to 15,872 frames using two-bit states:
unavailable, free, caller-owned or VMM-owned. Metadata scales with usable RAM,
not the highest E820 hole/address. Metadata frames are themselves unavailable.
Invalid, reserved, unaligned and duplicate frees fail without changing counts.

E820 normalization is order-independent: usable overlaps count once, reserved
overlaps win. Usable ranges round inward; reservations round outward. Disabled
and empty entries are ignored; overflowing enabled entries reject initialization.
No fallback RAM is invented. Memory beyond the CPU address limit is excluded.

Reservations cover the initial 2 MiB (image/BSS, boot data, old tables/stacks,
BIOS/VGA and access aperture), VBE framebuffer pitch-times-height, and legacy
local/I/O APIC pages. E820 non-RAM and holes never enter the allocator. Future
drivers must reserve additional device ranges before PMM initialization if
firmware does not already identify them as non-usable.

`pmm64_alloc(PMM_NORMAL)` prefers frames at/above 4 GiB, then falls back below.
`PMM_DMA32` only returns full frames below 4 GiB, using the same allocator.
`pmm64_alloc_contiguous(count, zone)` returns the first physically consecutive
run of `count` free frames for DMA descriptor lists; a run never crosses a
metadata chunk (physical continuity is only guaranteed inside one `BitmapPage`),
so oversized requests fail instead of silently returning a non-contiguous set.
`pmm64_free_contiguous` validates the whole range before freeing, so a
partially-owned argument frees nothing. `managed` counts eligible
E820 frames after platform reservations, including metadata; `free` excludes
metadata and allocations; `metadata` counts allocator backing pages; `above4g`
counts managed high frames, including high metadata. All accounting is 64-bit.
Metadata capacity is checked before any backing frames are installed. The allocator was tested with QEMU guests configured for 16 MiB, 64 MiB, 256 MiB, 5 GiB and 32 GiB; at 32 GiB it managed over 30 GiB of usable frames and allocated page tables above 4 GiB.

## Runtime paging

A supervisor RW/NX aperture at virtual `0x1ff000` temporarily maps physical
frames. `0x1fe000` maps the PT controlling that aperture. A `physical_view`
pointer expires on the next call. Table walkers never keep that pointer across
another access. Runtime initialization allocates all four identity-map levels
and a shared kernel PDPT from PMM, preserves boot protections and switches CR3.
The PML4 and all other runtime tables may live above 4 GiB. Original static
tables are only a boot bridge and remain reserved with the low region.

Internal virtual layout, not yet a published process ABI:

| PML4 slots | Use |
| --- | --- |
| 0 | Shared supervisor bootstrap/identity region; only low 2 MiB mapped |
| 1–255 | Private user mappings: `0x8000000000` to `0x800000000000` exclusive |
| 256–510 | Reserved/unmapped |
| 511 | Shared supervisor kernel allocations from `0xffffff8000000000` |

New roots share slots 0/511 by reference. Slot 511's PDPT stays anchored, so later
kernel mappings propagate to existing spaces. Normal APIs cannot mutate slot 0
or install user mappings in kernel slots. Addresses must be canonical/aligned,
physical frames within the CPU limit, and flags recognized. W+X is rejected.
Device mappings are borrowed, supervisor-only, NX and uncached (PCD/PWT).
Only 4 KiB pages are supported.

## Ownership and failure contracts

- Zero-initialize `AddressSpace` and `GuardedStack` handles. Do not copy live
  handles or use stale handles. These are trusted kernel APIs.
- `vmm64_alloc_page` allocates/zeros a frame and transfers sole ownership to the
  space; direct `pmm64_free` is then rejected.
- `vmm64_map_borrowed` leaves lifetime with the caller. User mappings require
  caller-owned PMM RAM. Kernel callers can map external/MMIO frames. The caller
  must keep borrowed frames alive until all their mappings are removed.
- `vmm64_unmap(..., release=1)` accepts only owned mappings, invalidates before
  freeing, and reclaims empty intermediate tables. `release=0` accepts only
  borrowed mappings and optionally returns their frame without freeing it.
  Owned-frame detachment is deliberately unsupported.
- `vmm64_destroy` recursively releases private tables/owned frames and ignores
  borrowed ownership. It preserves shared kernel mappings, refuses active/kernel
  roots, and clears the destroyed handle.
- Stack creation reserves non-present guard PTEs that normal mapping cannot
  overwrite, then maps zeroed RW/NX pages. Creation rolls back on failure;
  destruction preflights the range first. Address-space destruction also frees
  stacks. Never destroy a stack still referenced by a CPU or TSS.

Kernel execution, RSP0, double-fault IST1 and NMI IST2 use dynamic guarded stacks.
Failure rollback removes only newly created tables and never modifies an existing
leaf. Owned data and partially constructed stacks also roll back. Allocation
failure injection exists only in self-test builds.

## TLB and concurrency boundary

This target executes on one BSP with interrupts disabled. Aperture/PMM/VMM work
requires serialized IF=0 execution, checked on physical access. NMI handlers
must not call these APIs. SMP, locks and remote TLB shootdowns are not implemented;
add those and per-CPU apertures before enabling concurrent memory operations.

Active-page and shared-kernel changes use INVLPG. Inactive user roots retain no
TLB entries across switches: PCID and global pages remain disabled, and loading
CR3 flushes prior translations. Active-root destruction is prohibited.

## Verified checkpoint

`python tests/x86_64_boot.py` includes 16/64/256/5120/32768 MiB guests, original fault
tests and memory lifecycle/failure checks. `--skip-high-memory` explicitly omits
the 5 GiB and 32 GiB proofs on constrained hosts; it is not full milestone verification.

The 5 GiB guest reported 524,288 managed frames above 4 GiB. Its active root was
`0x100000000`; a tested data frame was `0x100013000`. The CPU accessed high data
and used the high page tables. DMA32 allocations stayed below 4 GiB and were
freed. After 100 lifecycles, free frames remained `0x13fd79` before and after
(persistent kernel stacks/tables are excluded from the transient baseline).

Other checks cover zero filling, RX execution, dynamic RO/NX/unmap/guard faults,
independent roots, ownership, MMIO lifetime, shared-kernel survival, and injected
failure at every allocation in kernel-root initialization, a new page walk and
stack creation. Normal and self-test builds succeed.
