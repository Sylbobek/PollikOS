# Architektura pamięci PollikOS

Szczegółowy, aktualny opis adresów pozostaje w `ARCHITECTURE.md` (sekcja 1.4) i nagłówkach. Ten dokument rozróżnia targety.

## BIOS/i386

- PMM czyta mapę BIOS E820 i zarządza ramkami 4 KiB wyłącznie poniżej `KERNEL_DIRECT_MAP_TOP` (1 GiB). Wyższy RAM jest wykrywany, ale poza zasięgiem tego targetu.
- VMM używa 32-bitowych katalogów i tablic stron, ustawia paging oraz `CR0.WP`, współdzieli supervisor mappings kernela poza zakresem user.
- Każdy ELF32 ma katalog stron; userspace zajmuje `[0x40000000, 0xC0000000)`, w tym stos i guard page. `user_range_valid` i copy helpers ograniczają syscalle do user mappings.
- Bez PAE/NX sprzęt nie egzekwuje ochrony no-execute na stronach. Kernel heap (`mem.c`) jest osobny od page allocatora.

## x86_64

`kernel/arch/x86_64/{physical,pmm,vmm}.c` to osobny target: 64-bitowe adresy fizyczne, mapa E820 powyżej 4 GiB, czteropoziomowe tablice stron, przestrzenie procesów i guarded stacks. Brakuje jeszcze SMP, remote TLB shootdowns oraz wspólnej ścieżki ABI z i386. Szczegóły własności mapowań i zweryfikowany zakres są w `kernel/arch/x86_64/MEMORY.md`.

## Kierunek migracji

Najpierw mierzyć i testować własność/rollback istniejących mapowań. Następnie wiązać alokacje z obiektami procesu, dodać kontrolowane memory mapping i shared memory przez handle rights. Nie zmieniać granic PollikFS ani mapy danych z powodu migracji VMM; user pages muszą być odseparowane przed przeniesieniem GUI do Ring 3.
