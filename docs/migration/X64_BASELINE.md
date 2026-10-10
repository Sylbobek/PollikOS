# PollikOS x86_64 — baseline migracji, 10.10.2026

Zakres: stan istniejącego repozytorium, mapa zależności, build/testy x86_64
oraz przygotowanie dalszej migracji. **Na tym etapie i386 pozostaje.**
Plan wykonania: [X64_MIGRATION_PLAN.md](X64_MIGRATION_PLAN.md).
Zasady dalszego rozwoju: [AGENTS.md](../../AGENTS.md).

## Tożsamość i warunki pomiaru

- Repozytorium: `C:\Users\syltu\Desktop\PollikOS`, gałąź `main`.
- HEAD: `4df1a499226897957746cc14b08052255503c65b`.
- Baseline obejmuje zastany **brudny working tree (62 ścieżki)**, nie sam HEAD.
  Istnieją zmiany buildów, bezpieczeństwa/admin, wejścia/USB, GUI i narzędzi
  dyskowych, w tym nieśledzone `factory_reset.*`, `usb_input.*`,
  `usb_platform.c`, `sdk/apps/settings.c` i `tools/build_usb.ps1`.
  Nie resetowano, nie stashowano ani nie commitowano tych zmian.
- Toolchain: Clang/LLVM 22.1.7, NASM 3.01, QEMU 11.0.0,
  Python 3.11.9; dostępne PowerShell 7.6.5 i Windows PowerShell 5.1.26100.9444.
- QEMU: TCG, jeden CPU; konfiguracje określają poszczególne testy.
  Wyniki dotyczą wirtualnego sprzętu, bez uruchamiania instalacji na dysku fizycznym.

Przed testami skopiowano aktualne źródła i zależności do izolowanego katalogu
`C:\Users\syltu\b64-1010`. **Nie kopiowano istniejących obrazów dysków**;
obrazy powstają w `build/x86_64/` tej kopii. Testy używają jej fixtures,
jednorazowych kopii i snapshotów. Starych binariów kernela nie użyto jako dowodu
działania obecnych źródeł. Wstępna kopia z długą ścieżką znajduje się pod
`build/migration-baseline-20261010/workspace`; nie jest końcowym środowiskiem pomiaru.

Dowody lokalne (katalog `build/` jest ignorowany przez Git):

- `build/migration-baseline-20261010/initial-status.txt`: stan przed zmianami.
- `source-manifest.json`: ścieżki i SHA-256 skopiowanych plików.
- `ignored-source-inputs.json`: lokalne wejścia pominięte przez `git ls-files`,
  w tym wymagane skrypty generatorów; zawiera także pliki cache, które nie są kodem.
- `kernel-dependencies.json`: zależności 85 jednostek C uzyskane przez
  `clang --target=x86_64-none-elf -MM -DPOLLIK_X64=1 -DSELFTEST=1`;
  **0 błędów dependency scan**. To dowód zależności nagłówków, nie test runtime.
- `original-disks.json`: rozmiary/mtime istniejących obrazów i hash
  `build/PollikData.img`; porównanie końcowe zapisuje osobny raport ochrony danych.
- `results.json`, `*-ci-x64-extended.log` i logi w izolowanym `build/ci/`:
  polecenia, statusy, czasy i błędy. Próby przygotowania środowiska zachowano
  osobno od końcowej weryfikacji.

## Przeczytane kontrakty i konfiguracja

Przejrzano `ARCHITECTURE.md`, `ROADMAP.md`, `README.md`, `sdk/README.md`,
`docs/{kernel-architecture,memory-architecture,syscall-abi,driver-api,object-model}.md`,
checkpointy bezpieczeństwa i I/O, dokumenty x86_64 dotyczące pamięci,
wykonania użytkownika, ELF64, procesu/schedulera, VFS, fd/stat/katalogów,
mutacji i runtime. Sprawdzono rzeczywiste źródła, nagłówki, ASM, build/launchery,
generatory oraz testy i konfigurację `.github/workflows/kernel.yml`.

Dokumentacja zawiera warstwy historyczne. `VFS_LAUNCH.md`, `FILE_ABI.md`
i `RUNTIME_C1.md` opisują także dawne limity/read-only. Aktualne wartości
pochodzą z kodu, nie z historycznych oznaczeń ukończenia:

- `memory.h`: `MM_KERNEL_START=0xffffff8000000000`, użytkownik
  `[0x0000008000000000, 0x0000800000000000)`; linker startuje kernel pod
  `0x100000`. Opis `0xFFFF800000000000` w ARCHITECTURE nie jest aktualnym
  adresem mapowań tego kernela; nie cały tekst/kod kernela przeniesiono do high-half.
- `elf64.h`: maksymalnie **4 MiB** obrazu, 16 program headers, 1024 stron
  segmentów; statyczny little-endian ELF64/EM_X86_64/ET_EXEC, bez dynamicznego linkera.
- `user_abi.h`/`user.h`: do **1024** slotów procesu, limit zależny od RAM,
  jeden osadzony TCB, 256 KiB stosu użytkownika z guard page,
  **128** deskryptorów na proces. SYSCALL jest głównym transportem,
  INT 0x81 pozostaje przejściowym transportem fixtures x86_64; to nie ELF32 ABI.
- `include/pollikos_abi.h`: wspólny, 24-bajtowy opis ABI **1.1**;
  numery operacji x86_64 są osobne i generują nagłówki C/NASM.
  `stat` v1 ma 64 B, rekord katalogu v1 96 B. Nie zmieniono żadnej struktury.
- `pollikfs.h`: 32768 bloków po 1024 B (**32 MiB brutto**), 512 inode,
  inode 60 B, dirent 64 B, FS od LBA 64, tabela inode 31 bloków,
  dane od bloku 36. Profil dysku 30 GiB nie zwiększa pojemności filesystemu.

## Rzeczywisty stan x86_64 w źródłach

Istnieje samodzielny kernel C/NASM, bez Linux/BSD. `build-x86_64.ps1`
kompiluje `--target=x86_64-none-elf`, linkuje `elf_x86_64`, tworzy oddzielne
warianty `kernel`, `selftest`, `system` (`-Production`) i rzeczywiste ELF64
aplikacji. Kernel używa `-mgeneral-regs-only`, aplikacje x87/SSE2 bez AVX;
SDK testuje format ELF i powtarzalność przykładu. Limit obrazu bootstrap to
1 MiB, linker wymaga końca BSS nie wyżej niż `0x1fe000`.

Kod implementuje ścieżki PMM64/VMM64 (ramki >4 GiB, DMA32, NX/WP, rollback),
user-copy, ELF64, timer/preempcja Ring 3, spawn/wait/sygnały/potoki,
deskryptory z prawami, okna posiadane przez proces, TTY i konsola graficzna.
Proces posiada jeden TCB; kernel i I/O pozostają BSP/IF=0, bez SMP.
Runtime zawiera libc/crt0, powłokę `pollish` i natywny TinyCC z sysrootem.

Istnieją aplikacje ELF64 Desktop, Files, Terminal, Notes, Settings, Browser,
Calculator i odtwarzacze mediów/wideo. Domyślny Browser linkuje QuickJS,
parsery NetSurf/libcss i wspólny layout/raster; wariant Elk jest opcjonalny.
HTTP jest w Ring 3, IPv4/TCP/TLS/RTL8139 w kernelu. Audio AC'97, PS/2 i USB
mają adaptery x86_64. Obecność tych ścieżek nie zastępuje testu każdej funkcji.

Wspólny PollikFS/VFS obsługuje mutacje, prawa katalogów, generacje inode,
metadata redo journal/CRC/recovery i read-only przy uszkodzeniu. Journal nie
zapewnia atomowości całej zawartości danych pliku. Wspólne konta używają
Argon2id i weryfikacji starszych rekordów, sesji, lock/logout/admin i capabilities.
Nie jest to jeszcze system wielu kont z ACL per inode.

## Zależności i blokery przejścia

Pełna mapa i etapy: [X64_MIGRATION_PLAN.md](X64_MIGRATION_PLAN.md).
Najważniejsze ustalenia z kodu:

1. Nie można usunąć całego `kernel/` poza `arch/x86_64/`: build x86_64
   nadal kompiluje VFS/PollikFS/journal/konta/security/HAL/storage/audio/USB,
   sieć i fonty. Browser userspace kompiluje cztery pliki z `kernel/browser/`.
   Dependency scan wykazał także użycie `kernel/system.h`, `kernel/klog.h`,
   `kernel/font_data.h`, `kernel/audio_chime.h`, `kernel/gfx/framebuffer_mode.h`,
   `kernel/certs/anchors.h` i nagłówków `kernel/include/bearssl/`.
2. `boot/boot.asm` i `boot/stage2.asm` są wspólne. Kod `bits 32` w wejściu
   x86_64 jest niezbędnym mostem do long mode; nie usuwać go wraz z ELF32.
3. `Start-PollikOS.cmd` uruchamia `run.ps1`, a ten nadal obsługuje i386;
   `.clangd` i `tools/gen_compile_commands.py` generują konfigurację i386,
   CI ma macierz `[i386, x86_64]`. Potrzebne jest kontrolowane przełączenie
   wszystkich wejść projektu, zamiast samego skasowania źródeł.
4. `kernel/ahci.c` i `kernel/installer.c` nie są linkowane przez obecny build
   x86_64. `fs64_mount()` rozpoznaje primary IDE slave i ATA PIO;
   `storage.c` wycina backend AHCI pod `POLLIK_X64`. Zachowanie SATA/installera
   wymaga portu lub jawnej decyzji o wycofaniu funkcji. Pełny parytet desktopu
   Ring 0 z aplikacjami SDK nie został udowodniony.
5. `.gitignore` z regułą `build/` ukrywa wymagane wejścia
   `third_party/libparserutils/build/{make-aliases.pl,Aliases}` oraz
   `third_party/libhubbub/build/{make-entities.pl,Entities}`.
   `git ls-files` ich nie zawiera, a `tools/build_web_libraries.py` bezwarunkowo
   czyta je przy generowaniu. Czysty checkout nie jest jeszcze samowystarczalny.
6. Build normalny wywołuje `tools/build_x64_system.py`, który synchronizuje
   istniejący `PollikData-system.img`; Production także profil 30 GiB.
   Przed rozwojem trzeba rozdzielić build od aktualizacji dysku albo zawsze
   używać izolacji. Test formatter z `tests/format_pollikfs2.py` jest wymaganym
   narzędziem x86_64 i nie może zniknąć wraz z testami legacy.
7. Windows PowerShell 5.1 w `tools/ci.ps1::Run-Check` z `*>` i
   `ErrorActionPreference=Stop` przerywa build na stderr z ostrzeżeniami
   OpenLibm (redefinicje `INFINITY`/`NAN` na Clang 22). Zachowano wynik FAIL
   tej próby; PowerShell 7 pozwala ocenić właściwy kod zakończenia builda.
8. Długa ścieżka izolowanej kopii przekroczyła limit długości polecenia
   Windows przy `llvm-ar` dla 303 obiektów libcss (`WinError 206`).
   Końcowe próby wykonano w krótkiej ścieżce. Docelowo użyć response file
   w generatorze archiwum; ten etap nie zmienia generatorów.
9. **Aktualny SELFTEST nie przechodzi bramki VFS.**
   `kernel/arch/x86_64/path_test.c:98–104` oczekuje dokładnie 55 wpisów
   `/bin`, lecz zastany generator `tools/build_x64_data.py` dodaje
   `settings.pol`. Świeży QEMU 64 MiB raportuje 56 (`0x38`) i kończy się
   `[X64] FAIL: disk files visible through VFS`. To rozbieżność kontraktu
   fixture/testu, nie dowód utraty plików. Przed usunięciem i386 trzeba
   zsynchronizować oczekiwany manifest bez zastępowania asercji luźnym limitem.
   W tym etapie nie zmieniano testu ani generatora.

Ograniczenia SMP/UEFI/NVMe/GPU/dynamicznego linkera/Web nie są same w sobie
blokadami usunięcia i386. Nie należy mieszać ich z bramkami tej migracji.

## Wyniki testów

- **PASS — build SELFTEST:** świeży kernel 579875 B, ELF64 i obraz BIOS
  utworzone z aktualnych źródeł. Log: izolowane `build/ci/build-x64-selftest.log`.
- **PASS — testy natywne CI:** `security_account_native.py`,
  `pollikfs_read_native.py`, `host_journal.py`, `utf8_ui_native.py`.
  Bezpieczeństwo obejmuje m.in. 19 punktów przerwania zapisu sektorowego,
  recovery rename, uszkodzony journal/bitmapę w read-only, zakresy/ENOSPC,
  prawa VFS, sesje, RFC Argon2id i starsze rekordy.
- **FAIL — CI Extended / `kernel_checkpoint.py`, QEMU 64 MiB:**
  przyczyna 55 vs 56 plików opisana powyżej. Zebrano częściowe PASS
  TinyCC/self-host, PMM/VMM/user-copy/ELF i schedulera, w tym 100 cykli
  mieszanych exit/fault/kill z identycznym bilansem PMM `0x3d65` przed/po.
  Brak `[X64] SELFTEST PASS`; późniejsze fazy tej sesji są **NOT RUN**.
- **FAIL — `x86_64_boot.py`:** świeży guest 16 MiB kończy się tą samą
  asercją VFS. Pozostałe konfiguracje 64/256/5120/32768 MiB oraz dalsze
  reboot/ENOSPC/CPU przypadki w tym poleceniu są **NOT RUN**.
- **PASS — zachowanie wejść i danych (kontrola po buildzie SELFTEST):**
  6451 oryginalnych wejść bez zmian SHA-256; żaden istniejący obraz nie
  zmienił rozmiaru/mtime, także SHA-256 `build/PollikData.img` pozostał ten sam.
- **PASS — build normalny:** `pwsh -NoProfile -File build-x86_64.ps1`,
  kernel 579836 B, ELF64/EM_X86_64 potwierdzony przez `llvm-readobj`.
  Log/exit 0: `verified-normal-build.log` / `.json`. Ostrzeżenia bibliotek
  upstream pozostają widoczne; PASS nie oznacza builda bez ostrzeżeń.
- **PASS — `x86_64_storage.py`:** wszystkie dziewięć przypadków QEMU
  (sygnatura, starsza geometria, błędna nazwa/inode/bloki/rozmiar,
  mały dysk i brak dysku), bez zmiany testowanych obrazów i bez
  osadzania kompletnych ELF aplikacji w kernelu. Log: `verified-storage.log`.
- **PASS — odrzucanie CPU:** osobne wywołania istniejącego helpera `boot`
  dla pentium3 oraz qemu64 bez LM/NX/PAE/MSR/SYSCALL. Bez dysku danych,
  z boot snapshot; każdy raportuje UNSUPPORTED CPU przed wejściem w long mode.
  Log: `verified-cpu-rejection.log`. Nie zastępuje pełnej macierzy boot.
- **PASS — `terminal_commands_x64.py`:** rzeczywiste polecenia, arytmetyka,
  system/procesy, potoki, atomowa kopia oraz admin: anulowanie, błędne hasło,
  sudo, dziedziczenie i sprzątanie przy exit; `TERMINAL_COMMANDS_PASS`.
- **PASS — `web_engine_x64.py`:** upstream HTML5/CSS selectors, important,
  calc, media i lifecycle w normalnym gościu x86_64.
- **PASS — `x86_64_app_checkpoint.py`:** normalny desktop, Notes UTF-8,
  atomowy save/ENOSPC oraz Files GUI: folder/copy/trash/restore/rename/cut/paste.
- **PASS — `browser_images_x64.py`:** rzeczywiste lokalne HTTP/redirects,
  HTML/CSS/QuickJS, PNG/JPEG framebuffer, DOMContentLoaded, timers, async fetch,
  bookmark/save, homepage i sprzątanie przy zamknięciu. Publiczne HTTPS było
  **NOT RUN**; nie podawano opcji `--https`.
- **PASS — `x86_64_security_session.py`:** Ring 3, prawa i admin,
  anulowanie/błędne hasło, dziedziczenie, zużycie jednorazowego grantu,
  lock/unlock, zmiana hasła, logout cleanup i logowanie po restarcie.
- **PASS — `network_clients_x64.py`:** współbieżne HTTP, chunk decoding,
  odmowa dla obcego PID i odrzucenie zamkniętego tokenu.
- **PASS — `x86_64_console.py --timeout 300`:** `CONSOLE PASS`, powłoka,
  pliki, potoki, przekierowania, Ctrl+C, historia, kompilacja/link/uruchomienie
  TinyCC oraz persistencja konta po restarcie na kopii dysku.
- **PASS — `usb_input_x64_guest.py`:** domyślny xHCI/USB mouse, ruch,
  przyciski, klawiatura i unplug/replug. UHCI/OHCI/EHCI oraz USB tablet są
  **NOT RUN** w tym baseline.
- **NOT RUN — gość `devices_x64.py`:** próba polecenia zakończyła się błędem
  przygotowania (brak `build/x86_64/system/PollikData-system.img`), zanim
  uruchomiono QEMU. Test wymaga osobnego wariantu Production i fixture
  `devices_c.elf`; nie korzystano z wcześniejszych obrazów użytkownika.
- **NOT RUN —** build Production, pełne macierze RAM/rozdzielczości, osobne
  media/movie/window-lifecycle/factory-reset/spawn-stress/crash-write-replay
  QEMU suites, i386 build/regresje, GitHub CI i fizyczny sprzęt. Natywne
  testy recovery są PASS, ale nie zastępują nieuruchomionych crash QEMU suites.

Wszystkie osiem dodatkowych testów normalnego gościa (terminal, Web engine,
Notes/Files, Browser, sesje, sieć, konsola, xHCI) zakończyło się exit 0.
Statusy i czasy: `remaining-results.json`; pełne logi: `guest-*.log`.
**Pełny SELFTEST i CI pozostają FAIL**, mimo PASS powyższych testów.

Logi wstępnych prób z brakującymi wejściami/nieukończonym buildem nie stanowią
wyniku runtime. Próby uruchomienia testów bez gotowego obrazu zakończyły się
błędem przygotowania; ich QEMU było NOT RUN. Nie użyto starszych obrazów,
żeby obejść błąd builda albo uzyskać pozorny PASS.
Bezpośrednie uruchomienie Windows PowerShell 5.1 z odziedziczonym środowiskiem
bundled hosta zatrzymało jeden build na niedostępnym `Get-FileHash`.
Końcowy normalny build uruchomiono w PowerShell 7 z dostępnym
`Microsoft.PowerShell.Utility`; zachowano oba logi, bez modyfikacji źródeł.

## Odtworzenie pomiaru

Przygotować krótką, nową kopię wszystkich bieżących źródeł, nagłówków,
vendored bibliotek, generatorów i zasobów, **bez `.git/`, głównego `build/`
i istniejących obrazów dysków**. Uwzględnić także lokalne wejścia ignorowane
przez Git opisane wyżej. Nie uznawać samego `git archive HEAD` za kopię
tego working tree. Zachować manifest SHA-256 wejść i toolchain.

Uruchamiać z izolowanej kopii w PowerShell 7:

```powershell
.\tools\ci.ps1 -Target x86_64 -Extended
python tests\x86_64_boot.py
python tests\x86_64_storage.py
```

CI wykonuje natywne security/read/journal/UTF-8, buduje SELFTEST,
uruchamia `kernel_checkpoint.py`, buduje normalny kernel oraz wykonuje
terminal, Web engine, Notes/Files, Browser, sesje, sieć i konsolę.
`x86_64_boot.py` dodaje macierz RAM, reboot/ENOSPC i odrzucanie CPU bez
wymaganych cech. Nie używać `--skip-high-memory` do deklarowania pełnego PASS.
Po pierwszym błędzie CI kolejne kroki są NOT RUN do czasu osobnego wykonania.

## Zmienione pliki i granice etapu

Dodano `AGENTS.md`, `docs/migration/X64_BASELINE.md` oraz
`docs/migration/X64_MIGRATION_PLAN.md`. Logi, manifesty, runner i obrazy
testowe są lokalnymi artefaktami, nie zmianami implementacji.
Nie usunięto i386, nie zmieniono kernela/SDK/ABI ani istniejących skryptów,
nie wykonywano instalacji fizycznej ani `git push`.
Końcowa kontrola oryginalnych plików/dysków i linków dokumentacji:
`preservation-check.json`, `document-check.json` — PASS.
