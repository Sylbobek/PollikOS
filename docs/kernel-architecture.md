# Architektura kernela PollikOS

Dokument opisuje bieżący kod. Głównym indeksem statusu oraz źródłem prawdy jest [`ARCHITECTURE.md`](../ARCHITECTURE.md).

## Targety

### BIOS/i386 (domyślny)

`build.ps1` tworzy BIOS-owy obraz i386. Start prowadzi przez `boot/boot.asm`, `boot/stage2.asm` i `kernel/entry.asm` do `kernel/kernel.c::kernel_main`. Kernel inicjalizuje PMM/VMM, framebuffer, stertę, filesystemy, wejście, sieć, rejestr aplikacji, procesy i pulpit. `kernel_main` pozostaje koordynatorem startu i pętli systemu.

Kod jest modułowy w sensie plików, ale działa w jednej przestrzeni Ring 0. Procesy ELF32 uruchamiane przez `process.c` działają w Ring 3 i otrzymują własne katalogi stron. Współdzielenie Ring 0 przez GUI nie jest izolacją aplikacji.

| Odpowiedzialność | Obecny moduł | Rzeczywisty stan |
| --- | --- | --- |
| Przerwania i wejście ABI | `kernel/interrupts.asm`, `kernel/syscall.c` | IDT, `int 0x80`, dispatcher C |
| Procesy i scheduler | `kernel/process.c` | Wspólny moduł: PCB, spawn/exit, wybór procesu, sleep, eventy i IPC |
| Pamięć | `kernel/pmm.c`, `kernel/vmm.c`, `kernel/mem.c` | E820/ramki, paging 32-bit, heap, osobne katalogi ELF32 |
| VFS i filesystem | `kernel/vfs.c`, `kernel/pollikfs.c` | Deskryptory i jeden backend PollikFS v2 |
| Sprzęt | `kernel/hw.c`, `kernel/storage.c`, `kernel/net/*`, `kernel/input_dispatch.c` | Sterowniki i polling; brak wspólnego Device Managera |
| Desktop | `kernel/desktop.c`, `kernel/gui/*`, `kernel/compositor.c` | Shell, aplikacje i renderery wykonują się w Ring 0 |
| HAL, fundament fazy 1 | `kernel/hal.c`, `kernel/hal.h` | Port-I/O, IRQ/flags, idle/halt, CR, TLB, MSR, CPUID, TSC, RDRAND, IDT/TR oraz i386 x87 context; API jest używane przez oba targety |

PIT obsługuje preempcję procesów ELF32; aktualny model nie jest SMP. Bootstrap (w tym wczesne CPUID, port-I/O i zapis EFER), wejścia IRQ/syscall, układy GDT/IDT oraz x86_64 FXSAVE/FXRSTOR pozostają target-specific. Wywołujący C używają wspólnego HAL dla port-I/O, flag CPU, CR/TLB, MSR, CPUID/TSC/RDRAND, IDT/TR i i386 x87 context.

## x86_64

`build-x86_64.ps1` buduje osobny obraz z `kernel/arch/x86_64`. W tym drzewie `scheduler.c` posiada kolejkę TCB, czas, aktywny kontekst i pętlę przełączeń; `process.c` posiada tablicę PID, cykl życia, zasoby procesu i obsługę syscalli. Granica jest wewnętrzna (`process_internal.h`, `scheduler_internal.h`). `Process64` osadza obecnie jeden `Thread64` z unikalnym TID i wskaźnikiem właściciela; tworzenie wielu wątków w jednym procesie nie jest jeszcze obsługiwane. Inne moduły zapewniają niezależne implementacje PMM/VMM, ELF64, user-copy i VFS launch. Target nie jest równoważny i386 funkcjonalnie, nie dzieli jeszcze jednego syscall ABI i nie jest domyślnym obrazem PollikOS. Szczegółowe ograniczenia pozostają w plikach `kernel/arch/x86_64/*.md`.

## Kierunek granic

Docelowa droga obsługi powinna przechodzić przez stabilny syscall/API, Executive (proces, pamięć, I/O, obiekty), interfejs sterownika i HAL. ABI 1.1 ma wspólny format zapytania o wersję i możliwości, lecz numery i konwencje i386/x86_64 pozostają osobne. Dispatchery korzystają z centralnych helperów kopiowania z/do user-space i sprawdzają wynik transferu. Dziś wiele aplikacji GUI i sterowników jest kodem jądra, więc przeniesienie plików do nowych katalogów nie poprawiłoby izolacji. Migracja wymaga po kolei ABI, własności uchwytów, usług Ring 3 i testów regresji.
