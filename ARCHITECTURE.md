# Architektura Systemu Operacyjnego PollikOS

## 0. Bieżący stan i migracja granic architektury — przegląd 30.09.2026

**Ten plik jest źródłem prawdy o architekturze PollikOS.** Opisuje stan potwierdzony w kodzie, a funkcje zaplanowane wyraźnie oznacza jako przyszłe. Dokumenty szczegółowe znajdują się w [`docs/`](docs/) oraz [`kernel/arch/x86_64/`](kernel/arch/x86_64/); w przypadku różnicy pierwszeństwo ma ta sekcja i aktualna implementacja.

### Stan rzeczywisty

- Domyślny graficzny pulpit użytkownika to BIOS/i386 z monolitycznym kernelem podzielonym na moduły. Zawiera pełne środowisko pulpitu z dokiem, siatką ikon, obsługą przeciągania do Kosza, animacjami cubic easing, odtwarzaczem mediów (obrazy/GIF/PKV), biblioteką PollikGL, wbudowaną przeglądarką HTML/CSS/JS oraz podsystemem kont użytkownika i instalatora USB.
- Równolegle rozwinięto pełny, natywny target **`x86_64`** (`kernel/arch/x86_64/`, budowany przez `build-x86_64.ps1`), który osiągnął stan **gotowości self-hosting**: 64-bitowy PMM/VMM w wyższej połówce pamięci, loader ELF64, zaawansowany model procesów z hierarchią rodzic-dziecko i `waitpid`, uniksowe potoki (`pipe.c`), pełną mutację plików w PollikFS v2 (32 MiB pojemności brutto, w tym metadane), buforowane `FILE*` w SDK, powłokę `/bin/pollish`, natywny kompilator **TinyCC 0.9.27 jako `/bin/tcc`** oraz serwer okien z procesowo posiadanymi powierzchniami i samodzielną przeglądarką Ring 3 (`sdk/apps/browser.c`).
- Na i386 istnieją procesy ELF32 Ring 3, osobne katalogi stron, walidacja zakresów user-space, bezpieczne kopie i ABI przez `int 0x80`. Wbudowane aplikacje GUI i przeglądarka na i386 nadal wykonują się w Ring 0.
- PMM/VMM i ELF32 działają; pamięć fizyczna na i386 jest ograniczona do 1 GiB direct map (brak PAE/NX). Target x86_64 usuwa to ograniczenie, oferując 4-poziomowe stronicowanie PML4 i sprzętowy bit NX.
- Warstwa pamięci masowej obsługuje zarówno legacy ATA PIO, jak i sterownik **SATA AHCI** (`kernel/ahci.c`). PollikFS v2 jest głównym systemem plików z automatyczną walidacją geometrii bloków `[31, 36]`.
- System uwierzytelniania (`kernel/auth.c`) zapewnia trwałą bazę `/etc/account.db` z solonym haszowaniem i KDF opartym na BearSSL, obsługując kreator konfiguracji, ekran logowania oraz instalator USB Live (`kernel/installer.c`).

### Plan migracji i warunki przejścia

| Faza | Zakres i bramka akceptacji | Stan |
| --- | --- | --- |
| 0 — analiza | Inwentaryzacja źródeł, granic, buildów, testów i znanych rozbieżności; ten dokument jest indeksem stanu. | Ukończona |
| 1 — granice kernela | HAL dla operacji architektury; port-I/O, IRQ/flagi, bezczynność CPU, CR/TLB, MSR, CPUID/TSC/RDRAND, IDT/TR oraz stan x87 i386. | Zamknięta: wspólna baza C/HAL w `kernel/hal.c` zweryfikowana na obu targetach |
| 2 — syscall layer | Wersjonowane ABI 1.0 dla i386 (`int 0x80`) i x86_64 (`syscall`/`sysret`), wspólny opis możliwości, centralne user-copy i testy błędnych wskaźników. | Zamknięta: `include/pollikos_abi.h`, osobne rejestry i namespace targetów |
| 3 — procesy i wątki | Oddzielić cykl życia procesu od schedulera i TCB; hierarchia procesów, stany i sygnały. | Zaawansowana: w x86_64 kompletny model `spawn`/`waitpid` z TCB `Thread64`, monotonicznymi PID, kodami zakończenia i sygnałami C12; scheduler w `kernel/arch/x86_64/scheduler.c`. W i386 scheduler w `process.c` |
| 4 — pamięć | PMM/VMM z odrębną przestrzenią adresową per proces, guard pages, rollback nieudanych mapowań. | Zamknięta: i386 z izolacją katalogów i ochroną supervisor; x86_64 z 4-poziomowym stronicowaniem PML4 w wyższej połówce pamięci |
| 5 — obiekty i deskryptory | Wprowadzić typowane, ref-counted obiekty; procesowe tablice deskryptorów i sprzątanie przy exit. | Zamknięta na x86_64: dedykowane tablice deskryptorów (do 128 fd per proces), zamykanie gniazd i plików przy zakończeniu; w i386 deskryptory w `vfs.c` |
| 6 — I/O i VFS | Pełny kontrakt operacji plikowych, obsługa mutacji, podwójnie pośrednie bloki i stabilny filesystem. | Zamknięta: x86_64 z pełną mutacją (`O_CREAT`/`O_TRUNC`/`O_APPEND`, `mkdir`, `unlink`, `rmdir`, `rename`), PollikFS v2 z 32 MiB brutto i 64-bajtowym `stat`/`fstat`; w i386 VFS v2 z `/home/Desktop` i `/home/Trash` |
| 7 — sterowniki | Rejestr urządzeń i kontrakty sterowników nad HAL/I/O; ATA PIO, AHCI SATA, RTL8139, VBE/DISPI, TTY. | Zaawansowana: wdrożony sterownik SATA AHCI (`kernel/ahci.c`), RTL8139, VBE framebuffer oraz x86_64 console TTY i mouse |
| 8 — IPC | Kolejki zdarzeń, uniksowe potoki (`pipe.c`), `dup`/`dup2` i potoki powłoki. | Zamknięta na x86_64: potoki jednokierunkowe z buforowaniem, przekierowania I/O w powłoce; w i386 kolejki IPC i zdarzeń |
| 9 — usługi userspace i SDK | Kompletne C SDK dla programistów, biblioteka C, powłoka interaktywna i kompilator w gościu. | Zamknięta na x86_64: `sdk/` z `libpollikc.a` (buforowane `FILE*`, `math.h` z SSE2/x87), powłoka `/bin/pollish`, natywny kompilator **TinyCC 0.9.27 jako `/bin/tcc`** (self-host rebuild `libc.a`) |
| 10 — desktop i aplikacje | Przeniesienie kompozytora i aplikacji do Ring 3 z dedykowanym protokołem okien. | Częściowo zrealizowana: x86_64 serwer okien (`kernel/arch/x86_64/window.c`) z procesowo posiadanymi buforami oraz samodzielna przeglądarka Ring 3 (`sdk/apps/browser.c`); i386 zachowuje zintegrowany pulpit Ring 0 |
| 11 — stabilność i regresje | Zestawy testów automatycznych boot, procesów, pamięci, VFS, awarii i regresji. | Ciągła: testy automatyczne w `tests/` dla obu architektur (m.in. `gui_registry`, `browser_html`, `process_stress`, testy jednostkowe C1–C7 na x86_64) |

Szczegóły kolejnych kontraktów: [kernel](docs/kernel-architecture.md), [syscalle](docs/syscall-abi.md), [sterowniki](docs/driver-api.md), [model obiektów](docs/object-model.md), [pamięć](docs/memory-architecture.md). To plan etapów, nie deklaracja, że komponenty oznaczone „planowana” już działają.

## 1. Aktualna architektura — stan źródeł po przeglądzie 28 września 2026

Dokument opisuje implementację na podstawie źródeł. Po STAGE A (rozruch 1 MiB, PMM/VMM/procesy; 1.3–1.5) doszła warstwa mediów i grafiki aplikacyjnej: wspólny dekoder obrazów/GIF/„wideo" (`kernel/media.c`), własne software'owe API graficzne **PollikGL** (`kernel/pollikgl.c`), **Canvas 2D** w przeglądarce z mostkiem DOM/JS, próbki mediów seedowane na pulpit (`kernel/sample_media.h`) oraz szereg zmian wizualnych powłoki (bezramkowe okna, pełne obrysy, cień okna, szybka ścieżka logowania, częściowe odświeżanie animacji, podsumowanie benchmarku PollikMark). Szczegóły: 1.3, 1.6, 1.6.1, 1.8 oraz nowa sekcja 1.12. Wcześniejsze raporty `build/final-ui-522560.json` i `build/final-522560-tests.json` dotyczą starszego jądra; po ostatnich zmianach nie odtwarzano całej macierzy testów. Obecność mechanizmu w kodzie ani ograniczony PASS nie oznaczają jego bezbłędności. README i ROADMAP nie są samodzielnym dowodem stanu implementacji.

### 1.1. Charakter systemu

**Pollik OS v0.1 Alpha to samodzielny, eksperymentalny system x86-32 z monolitycznym jądrem podzielonym na moduły źródłowe.** Bootloader i kod wejścia napisano w NASM, a większość systemu w C kompilowanym przez Clang dla `i386-none-elf`, w trybie freestanding. Nie korzysta z jądra Linux/BSD, GRUB-a ani systemowej libc. Zawiera własne funkcje pomocnicze i adaptacje bibliotek zewnętrznych: BearSSL, Elk oraz stb_image.

To więcej niż graficzna makieta: kod implementuje rozruch BIOS, sterowniki urządzeń, stronicowanie, procesy Ring 3, loader ELF, trwały zapis i stos sieciowy. Jednocześnie aplikacje pulpitu, parsowanie stron i JavaScript nadal wykonują się w Ring 0. Podział na pliki C nie stanowi granicy ochrony pamięci.

### 1.2. Mapa modułów

| Obszar | Główne pliki | Odpowiedzialność |
| --- | --- | --- |
| Rozruch | `boot/boot.asm`, `boot/stage2.asm`, `kernel/entry.asm`, `kernel/linker.ld` | BIOS, wczytanie obrazu, E820, VBE, tryb chroniony, wejście do C |
| Pamięć | `kernel/pmm.c`, `kernel/vmm.c`, `kernel/mem.c` | Ramki fizyczne, tablice stron, mapowania, sterta jądra |
| Procesy i ABI | `kernel/process.c`, `kernel/interrupts.asm`, `kernel/elf.c`, `kernel/syscall.c` | Przerwania, scheduler, ELF32, wywołania systemowe, zdarzenia i IPC |
| SDK użytkownika | `include/pollikos.h`, `apps/user.ld`, `apps/*` | Wrappery ABI i demonstracyjne programy ELF |
| Runtime i start jądra | `kernel/kernel.c` | 79 linii: funkcje freestanding, inicjalizacja i główna pętla delegująca obsługę pulpitu |
| Powłoka pulpitu | `kernel/desktop.c`, `kernel/desktop.h` | Pulpit, dock, menu, cykl życia okien i koordynacja iteracji UI |
| Elementy pulpitu | `kernel/desktop_items.c`, `kernel/desktop_items.h` | Siatka 104×96, snapping, multi-select (marquee), przeciąganie grupowe, layout `/home/Desktop/.layout` |
| Kosz systemowy | `kernel/trash.c`, `kernel/trash.h` | Katalog `/home/Trash`, metadane `.trashinfo`, przywracanie, opróżnianie |
| Animacje UI | `kernel/ui_animation.c`, `kernel/ui_animation.h` | Cubic easing (otwieranie, minimalizacja/przywracanie z Docka, bounce) |
| Kompozytor | `kernel/compositor.c` | Kompozycja, unieważnianie cache, ramki okien, prezentacja klatek, kursor oraz przydziały PMM sceny i tapety |
| Rasteryzacja | `kernel/graphics.c`, `kernel/graphics.h` | Prywatny cel rysowania, clipping, prymitywy, cache pokrycia AA narożników, pełny obrys zaokrąglony (`roundrect_stroke`), skalowanie blitów RGBA/koloru, tekst do dowolnego bufora |
| PollikGL (własne API grafiki) | `kernel/pollikgl.c`, `kernel/pollikgl.h` | Software'owy odpowiednik warstwy DirectX/Vulkan: immediatowy kontekst po buforze pikseli (rect, linia, trójkąt, koło, blit) ze scissorem; bez GPU |
| Media | `kernel/media.c`, `kernel/media.h`, `kernel/sample_media.h` | Wspólny dekoder obrazów (PNG/JPEG/BMP/GIF/TGA/PSD/PNM przez stb_image), własny odtwarzacz animowanego GIF oraz „wideo" MJPEG `.pkv`; generowane próbki na pulpit |
| Programowe gfx/3D | `kernel/gfx_device.c`, `kernel/soft3d.c`, odpowiednie `.h` | Cienkie API targetów programowych, clear/kształty, macierze, clipping i rasteryzacja trójkątów CPU; przyrostowe funkcje krawędzi i całkowity bilinear tekstury; nie sterownik GPU |
| Wejście powłoki | `kernel/input_dispatch.c`, `kernel/input_dispatch.h` | PS/2, stan wskaźnika i modyfikatorów, routing wejścia do powłoki/WM/aplikacji |
| Prywatny stan powłoki | `kernel/shell_internal.h` | Wspólny kontekst interakcji desktop/kompozytor/wejście; nie API aplikacji |
| Aplikacje GUI | `kernel/gui/*` | Rejestr metadanych i callbacków w `gui/apps.c`; renderery, prywatny stan i logika aplikacji oraz adapter przeglądarki, przez host API `gui/app_host.h` |
| Okna i prezentacja | `kernel/wm.c`, `kernel/framebuffer.c` | Geometria, Z-order, animacje, powierzchnie okien, zapis do LFB |
| Dysk i pliki | `kernel/storage.c`, `kernel/pollikfs.c`, `kernel/vfs.c` | ATA PIO, starsze migawki dokumentów, PollikFS v2, deskryptory |
| Kontroler AHCI | `kernel/ahci.c`, `kernel/ahci.h` | Sterownik SATA AHCI z FIS, PRDT, nagłówkami poleceń i obsługą portów HBA |
| Uwierzytelnianie | `kernel/auth.c`, `kernel/auth.h` | Trwała baza `/etc/account.db`, haszowanie z solą i KDF (BearSSL), kreator konta i ekran logowania |
| Instalator systemowy | `kernel/installer.c`, `kernel/installer.h` | Zapis instalatora USB Live na docelowy dysk twardy ATA |
| Sieć | `kernel/network.c`, `kernel/net/*` | Integracja, RTL8139, protokoły IPv4, TCP, TLS i HTTP |
| Przeglądarka | `kernel/browser/*` | Nawigacja, DOM, CSS, obrazy, Canvas 2D, layout, renderowanie, Elk JS i shim `var`/`const` |
| Sprzęt i diagnostyka | `kernel/hw.c`, `kernel/klog.c`, `kernel/system.h` | RTC, PCI, zasilanie, PC speaker, logi i wspólne definicje |
| Podsystem Audio | `kernel/audio.c`, `kernel/audio.h` | Sterownik Intel ICH AC'97 (Bus Master DMA, PCM 48 kHz, mikser), synteza dźwięków systemowych, fallback na PC Speaker |
| HAL (faza 1) | `kernel/hal.c`, `kernel/hal.h`, adaptery `kernel/system.h` | Port-I/O 8/16/32-bit, IRQ/flags, idle/halt, CR0–CR4, TLB, MSR, CPUID/TSC/RDRAND, IDT/TR oraz i386 x87 context; oba targety używają wspólnego API, a boot/entry zachowują kod arch-specific |
| Podsystem 64-bitowy | `kernel/arch/x86_64/*`, `build-x86_64.ps1` | Jądro x86_64 wyższej połówki, PML4, scheduler TCB, ELF64, potoki, TTY, serwer okien `window.c` |
| PollikOS C SDK | `sdk/*`, `tools/*` | Biblioteka standardowa `libpollikc.a`, `crt0`, driver `pollikcc`, `/bin/pollish`, natywny **TinyCC 0.9.27 (`/bin/tcc`)** |
| Zasoby | `assets/system-icons/`, `/usr/share/icons`, `fonts/`, `kernel/font_data.h` | Ikony PNG ładowane z PollikFS; kursory i Pollik Sans generowane osobno |
| Budowanie i testy | `build.ps1`, `build-x86_64.ps1`, `run.ps1`, `Start-PollikOS.cmd`, `tests/` | Obrazy dysków, konfiguracja QEMU, scenariusze integracyjne obu architektur |

### 1.3. Rozruch i inicjalizacja

1. BIOS uruchamia sektor rozruchowy pod `0x7C00`. Stage 1 wczytuje 8 sektorów Stage 2 pod `0x8000` przez rozszerzenia INT 13h.
2. Stage 2 włącza A20 (port `0x92`), odczytuje liczbę sektorów jądra z ostatniego słowa własnego obrazu (`kernel_sectors`, offset 4094; wpisuje ją `build.ps1`) i wczytuje obraz od LBA 9 porcjami po 64 sektory do bufora `0x10000`. Po każdej porcji ustanawia „unreal mode” (chwilowe `CR0.PE`, flat DS/ES o limicie 4 GiB, powrót do trybu rzeczywistego) i kopiuje 32 KiB pod **`0x100000 + n·32 KiB`**. Jądro nie jest już ograniczone do 512 KiB poniżej EBDA; obowiązuje limit 4 MiB w `build.ps1` i asercja linkera `__bss_end < 0x600000`. Jest to płaski obraz binarny, nie loader ELF jądra.
3. Zapisuje liczbę wpisów E820 pod `0x6000`, a wpisy od `0x6004` (maksymalnie 64). Pobiera informacje trybu VBE `0x118` do `0x7000` i ustawia liniowy framebuffer.
4. Ładuje tymczasową GDT, ustawia `CR0.PE`, przechodzi do kodu 32-bitowego, ustawia stos na `0x9FC00` i skacze pod `0x100000`.
5. `kernel/entry.asm` zeruje BSS (bezpośrednio za `.data`, wyrównane do 4 KiB) i wywołuje `kernel_main()`.
6. Jądro inicjalizuje kolejno: port szeregowy → PMM → VMM → autotesty pamięci → framebuffer → wczesne `desktop_init()` (wymiary i `compositor_init()`, przydział sceny/tapety) → stertę → starszy FS i VFS → PS/2 i RTC → sieć → `gui_apps_init()` (callbacki inicjalizacji Notes, przeglądarki i PollikMark) → procesy/przerwania → `desktop_start()` (grafika → UI → WM i powierzchnie okien → pierwszy obraz pulpitu i kursor). **Między tymi etapami `kernel_main()` rysuje ekran ładowania** przez `compositor_splash(etap, procent)`: gradientowe tło, logo „P", tytuł i pasek postępu, prezentowany natychmiast do LFB. Dzięki temu ciężka inicjalizacja nie pokazuje się jako zamrożony pulpit, a przejście do ekranu logowania jest płynne. Splash korzysta z `graphics_init`/`set_draw_target`/`framebuffer_present` przed `desktop_start()`.
7. Główna pętla w `kernel.c` wywołuje `net_poll()`, `phase2_poll()` i `desktop_poll()`. Ten ostatni odpytuje rejestr aplikacji i wejście, aktualizuje okna oraz animacje i zleca kompozytorowi prezentację zmian. Runtime może przejść w `sti; hlt`, gdy pulpit jest bezczynny i nie czekają dane PS/2. Scheduler jest obsługiwany przez przerwanie PIT; nie jest oddzielnym procesem użytkownika.

### 1.4. Pamięć: istnieją PMM i paging

**Stronicowanie jest zaimplementowane i włączane przez `vmm_init()` (`CR0.PG=1`, `CR0.WP=1`).** Poprzedni opis izolacji wyłącznie segmentacją był nieaktualny.

- PMM korzysta z mapy BIOS E820 i bitmapy ramek 4 KiB. **Zarządza wyłącznie ramkami poniżej `KERNEL_DIRECT_MAP_TOP` = 1 GiB**; RAM powyżej jest wykrywany i raportowany („Unmanaged RAM above 1 GiB direct map”), ale nie jest przydzielany — jądro 32-bitowe nie ma highmem ani kmap. Przy 2 GiB w QEMU dostępne jest więc ok. 1015 MiB.
- VMM używa klasycznych 32-bitowych katalogów/tablic stron; bez PAE/NX nie ma sprzętowego zakazu wykonywania kodu na stronie.
- Jądro mapuje tożsamościowo RAM od 0 do min(RAM, 1 GiB) oraz framebuffer MMIO. Strona zerowa jest odmapowana. **Direct map jądra i przestrzeń użytkownika są rozłączne**: użytkownik zajmuje `0x40000000`–`0xC0000000`.
- Nowy proces ELF otrzymuje osobny katalog stron. `vmm_create_address_space()` kopiuje wpisy PDE jądra **tylko poza zakresem użytkownika** (indeksy < 256 i ≥ 768), jako supervisor. `map_page()`/`unmap_page()` **odmawiają** modyfikacji tablicy stron współdzielonej z katalogiem jądra przez katalog procesu oraz mapowań z bitem USER poza zakresem użytkownika (log `map_page refused`). Wcześniejszy kod klonował wszystkie PDE, więc przy RAM > 128 MiB mapowanie ELF pod `0x08048000` nadpisywało tożsamościowe mapowanie jądra w **współdzielonej** tablicy, było widoczne dla każdego innego procesu i nie było zwalniane przy `vmm_destroy_address_space()` (dowód: `build/isolation-before-fix-256.log`, „user mapping modified the kernel directory”, „free pages before 63367 / after 63366”).
- `vmm_isolation_self_test()` uruchamia się przy każdym starcie po `vmm_self_test()`: tworzy dwa katalogi, mapuje stronę użytkownika pod `USER_SPACE_START` w pierwszym, sprawdza niezmienność katalogu jądra, brak widoczności w drugim, odmowę mapowania użytkownika pod `0x00400000` i pełny zwrot ramek do PMM.
- `user_range_valid()` wymaga, aby cały zakres leżał w `[USER_SPACE_START, USER_SPACE_END)`; samo sprawdzanie bitu USER w PTE nie wystarczało (PTE obszaru starszych workerów mają USER, a PDE w katalogu procesu nie). `copy_from_user()`/`copy_to_user()` korzystają z tej walidacji. Nie zastępuje to pełnego audytu ABI i obsługi błędów.
- `kmalloc`/`kfree` zarządzają osobną, ograniczoną stertą jądra od końca BSS do `0x7F0000` (po przeniesieniu obrazu pod 1 MiB jest to ok. 5,6 MiB). Przydzielenie QEMU 2 GiB RAM nie oznacza sterty jądra o rozmiarze 2 GiB.

Najważniejsze zakresy fizyczne/rezerwacje, z wyłączną górną granicą:

| Zakres / adres | Zastosowanie |
| --- | --- |
| Poniżej `0x100000` | BIOS, bootloader, dane E820/VBE, bufor porcji stage 2 (`0x10000`) i wstępny stos (`0x9FC00`); obszar zarezerwowany dla PMM |
| Od `0x100000` | `.text`, `.rodata`, `.data` (obraz wczytany przez stage 2), następnie BSS do `__bss_end`; linker wymaga `__bss_end < 0x600000` |
| Od końca BSS do `0x7F0000` | Obszar sterty jądra, z wyrównaniem allocatorów |
| `0x7FF000` | Odmapowana strona ochronna |
| `0x800000` – `0x820000` | Zachowany obszar starszych workerów demonstracyjnych |
| Dynamiczne ramki PMM (`compositor_init()`) | Scena i tapeta: dwa osobne ciągłe przydziały, każdy `screen_w * screen_h * 4` bajtów zaokrąglony do stron 4 KiB; bez stałych adresów i rezerwacji po 24 MiB |
| Dynamiczne ramki PMM (`wm_init()`) | Siedem powierzchni; każda ma pojemność `screen_w * screen_h` pikseli `u32` i dwa zewnętrzne słowa canary. Przydział `(capacity + 2) * 4` B zaokrąglony do stron 4 KiB; geometria/stride okna mogą być mniejsze od pojemności |
| Runtime LFB (`framebuffer.c`) | Pamięć urządzenia, nie zwykła sterta; pitch/bpp po DISPI mogą różnić się od pozostawionych metadanych BIOS VBE |

Przestrzeń **wirtualna** ELF ma osobne stałe w `kernel/vmm.h`: `KERNEL_DIRECT_MAP_TOP=USER_SPACE_START=0x40000000`, `USER_SPACE_END=0xC0000000`, wierzchołek stosu `0xC0000000`, stos 16 KiB (`USER_STACK_BOTTOM=0xBFFFC000`) i guard page `USER_GUARD_PAGE=0xBFFFB000`. `apps/user.ld` umieszcza program od `0x40001000`. Nie należy utożsamiać tych adresów z fizycznym przydziałem ramek. **Zmiana ABI:** ELF-y zlinkowane pod dawnym `0x08048000` (np. `/bin/hello` na dysku sformatowanym starszym jądrem) są odrzucane z logiem „PT_LOAD outside user range”; wymagają ponownego zlinkowania. Ramka framebuffera QEMU (`0xFC000000`/`0xFD000000`) leży w zakresie jądra; jeśli na innym sprzęcie LFB wypadnie w zakresie użytkownika, VMM loguje ostrzeżenie, a LFB pozostaje dostępny tylko przy katalogu jądra (PID 0).

### 1.5. Procesy, ELF i wywołania systemowe

Współistnieją dwa modele: starsze workery demonstracyjne oraz nowsze procesy ELF z płaskimi segmentami i katalogiem stron. Tabela ma `MAX_PROCESSES=16`; pulpit jest zadaniem jądra. Scheduler round-robin obsługuje stany ready/running/blocked/sleeping/dead, przełącza ramkę rejestrów, CR3 i stos Ring 0 w TSS. Kod zawiera zapis/odtworzenie stanu x87 (`fnsave`/`frstor`); kompilacja wyłącza SSE/MMX.

Zwalnianie zasobów wykonuje `reap_dead_processes()` wywoływane przez scheduler po wyborze następnego procesu: zamyka deskryptory VFS każdego procesu w stanie DEAD (nie tylko poprzednika), a dla procesów ELF niszczy katalog stron i zwalnia stos jądra do PMM. Workery starszego modelu używają statycznej pamięci BSS, która nie jest oddawana do PMM (wcześniej wyjątek workera powodował `pmm_free_pages()` na jego statycznym stosie). `kill` z terminala ustawia stan DEAD, więc zabity proces także jest sprzątany.

Loader `kernel/elf.c` obsługuje statyczne ELF32 little-endian, `ET_EXEC`, `EM_386` i segmenty `PT_LOAD`. Sprawdza nagłówki i zakresy, przydziela strony, kopiuje dane i zeruje BSS. Nie jest to dynamiczny linker ani środowisko zgodne z programami Linuksa.

ABI korzysta z `int 0x80`: numer w EAX, argumenty używanych wrapperów w EBX/ECX/EDX, wynik w EAX. Obejmuje kończenie i uruchamianie procesów, PID, yield/sleep/time, przyrost sterty, pliki, kolejki zdarzeń/IPC oraz podstawowe operacje TCP. Są to własne interfejsy PollikOS, nie pełne POSIX ani BSD sockets.

`apps/` zawiera `hello` oraz trzy programy do kontrolowanego sprawdzania obsługi błędów pamięci. Build tworzy ich ELF-y i osadza je w jądrze; inicjalizacja nowego PollikFS v2 instaluje je w `/bin`. Obsługa wyjątków odróżnia błąd procesu od błędu Ring 0: pierwszy prowadzi do zakończenia procesu, drugi do diagnostyki/panic.

### 1.6. Pulpit i grafika

Siedem wbudowanych aplikacji to Welcome, Files, Terminal, Notes, Settings, Browser i PollikMark3D. **Nie są to siedem niezależnych procesów ELF**: wszystkie działają w Ring 0, a renderery, stan i logika pozostają w modułach aplikacji. Podział źródłowy powłoki i rejestr opisuje podrozdział 1.6.1.

`wm.c` przechowuje geometrię i stan okien, focus, Z-order, animacje, regiony wymagające odświeżenia oraz powierzchnie okien. `kernel.c` ma obecnie 79 linii i ogranicza się do runtime freestanding, rozruchu oraz głównej pętli. `desktop.c` odpowiada za powłokę, dock, menu i cykl życia okien; `compositor.c` za kompozycję, unieważnianie cache, ramki okien, klatki, kursor i przydział buforów sceny/tapety przez PMM. `graphics.c` utrzymuje prywatne cele rasteryzacji i prymitywy rysowania, a `input_dispatch.c` obsługuje PS/2 i routing wejścia. Wspólny prywatny kontekst interakcji w `shell_internal.h` należy do desktopu i jest używany tylko przez desktop, kompozytor i wejście; cele rasteryzacji, ważność powierzchni i stan dekodera PS/2 pozostają prywatne dla swoich modułów. Zawartość okien rysują moduły z `kernel/gui/` przez wąskie host API. Istnieje obsługa wielu nakładających się okien, przesuwania, zmiany rozmiaru, minimalizacji i maksymalizacji.

Przepływ obrazu: **zawartość aplikacji → cache powierzchni okna → kompozycja z tapetą → bufor sceny → `framebuffer_present()` → LFB**.

- Powierzchnia przechowuje **straight RGB**, także w zewnętrznych narożnikach prostokąta. Pokrycie geometrii okna (promień 12 px, ograniczony połową wymiarów) jest stosowane **raz**, podczas kompozycji nad rzeczywistą sceną; nie jest wcześniej mieszane z czernią. Środkowe wiersze są kopiowane, a częściowe narożniki blendowane. `graphics.c` współdzieli cache AA promieni 1–29 (8555 B pokrycia, 16 próbek granicznych, skala 0–64); większe promienie mają obliczeniowy fallback. Cień rysowany jest w pasach poza nieprzezroczystym wnętrzem, także pod przezroczystymi narożnikami.
- Każdy slot PMM ma pojemność całego ekranu, niezależną od aktualnego `width/height/stride`. `pixels = base + 1`, a canary są w `base[0]` i `base[capacity + 1]`, **poza** całym obszarem rysowania. `wm_check_canaries()` odczytuje te słowa pamięci, nie tylko pola struktury. To diagnostyczne znaczniki, nie odmapowane guard pages ani gwarancja wykrycia każdego błędu.
- Faktyczna ścieżka damage jest w `compositor_paint()`: pełna scena, cache docka albo (tryb `full == 2`) **prostokąt obejmujący sumę starych i nowych granic wizualnych okna wraz z cieniem**, przycięty do ekranu. Odtwarza tapetę i kompozytuje przecinające go okna w Z-order, zachowując nietkniętą część cache docka. Aktywny podgląd przyciągania okien (`snap_preview`) nie wymusza już pełnego odrysowywania ekranu podczas przeciągania — kompozytor utrzymuje tryb dirty-rect (`full == 2`), dzięki czemu ruch okna i podgląd działają płynnie (120 FPS) bez zacinania nawet przy wysokich rozdzielczościach 3440×1440. Wewnętrzne pętle `rounded()` w `graphics.c` korzystają z prekomputacji kanałów koloru i przezroczystości (`blend_precomputed`).
- Menu kontekstowe (`ui_menu.c`, `desktop.c`, `input_dispatch.c`) obsługuje kompletną hierarchię powłoki: okna aplikacji (zaznaczanie i operacje na plikach w Files), elementy pulpitu (Open, Rename/F2 z walidacją, Move to Trash/Del), wolną przestrzeń pulpitu (New Folder, New Text File, przełącznik motywu, Ustawienia pulpitu) oraz ikony Docka (Open, Pin to Dock, Unpin from Dock z gwarancją stałego przypięcia Files, Quit).
- Okno Welcome (`welcome.c`) zrezygnowało z centralnego dużego logo "P" i napisów wersji, natomiast sekcja identyfikacji i informacji o systemie (`OS_LABEL`, architektura) została zintegrowana w aplikacji Ustawienia (`settings.c`).
- Wyskakujące toasty powiadomień w prawym górnym rogu ekranu (`ui_draw_notifications()`) zostały wyłączone, zapewniając czysty pulpit.
- Cache klienta jest odświeżany przy zmianie treści, aktywności lub rozmiaru, nie przy samej translacji. Ghost minimalizacji używa ukończonego cache bez zmiany autorytatywnej geometrii zminimalizowanego okna. Zapisany benchmark potwierdza zero paintów klientów podczas drag w obu rozdzielczościach, nie zerowy koszt kompozycji.
- `present_with_cursor()` tymczasowo rysuje kursor w scenie, rozszerza transfer o potrzebne stare/nowe granice kursora, wykonuje **jedno zgłoszenie `framebuffer_present()`**, po czym odtwarza czystą scenę w RAM. Osobna ścieżka cursor-only nie maluje klientów. Jeden transfer nie oznacza atomowego flipu, synchronizacji z VSync ani braku tearingu podczas scanout.

- **Wygląd okien i obrysów:** okna są bezramkowe; krawędź definiuje subtelny, wielowarstwowy **cień okna** (`draw_window_shadow`, pasy poza nieprzezroczystym wnętrzem) zamiast 1 px ramki. Cache klienta nie rysuje już własnych pasków ramki. Obrysy zaokrąglone rysuje `roundrect_stroke(x,y,w,h,r,t,stroke,fill)`: maluje kolor obrysu, potem wnętrze o `t` mniejsze, dzięki czemu piksele narożników są pełne (koniec z wyblakłym łukiem). Usunięto dekoracyjny „gloss" na kartach/docku/przyciskach. Wspólna skala promieni: `UI_RADIUS_SMALL/MEDIUM/LARGE/PILL` w `kernel/system.h`.
- **Szybka ścieżka logowania:** `compositor_paint()` ma osobny wczesny powrót, gdy `auth_is_active()` — nie kompozytuje tapety, ikon, okien ani docka pod ekranem logowania, tylko rysuje sam ekran auth. Usunięto wymuszony repaint przy każdym ruchu myszką (`auth_pointer` zwraca 1 tylko przy zmianie stanu). Dzięki temu wpisywanie hasła jest płynne jak na odblokowanym pulpicie.
- **Częściowe odświeżanie animacji:** `compositor_invalidate_animated(id)` zbiera prostokąt okna, a `compositor_paint(full==2)` odtwarza tapetę i kompozytuje tylko przecinające go okna w Z-order, zamiast całego ekranu. Odtwarzanie GIF/`.pkv` w podglądzie Files nie powoduje już zacinania kursora co klatkę. `desktop_poll()` dla animacji aplikacji ustawia `shell.partial` zamiast pełnego `request_scene_redraw()`.
- **Dock i login:** dock ma delikatny sheen na górnej krawędzi i pill wskaźnika uruchomionej aplikacji (aktywna szersza, akcent). Ekran logowania przeszedł na układ „nazwa → pole → przycisk po prawej", bez karty i logo, w stylu systemowym; pole ma ciągły 2 px akcentowy obrys i migający kursor widoczny tylko przy wpisanym tekście (nie nachodzi na placeholder).

`wm_time_us()` zwraca rzeczywisty **64-bitowy** czas: pełny TSC EDX:EAX, kalibrację względem PIT, dzielenie z zachowaniem wysokiej części ilorazu i monotoniczne ograniczenie. Bez poprawnej kalibracji używa rozszerzanego licznika PIT (raportowana rozdzielczość 8334 µs); nie zakłada fikcyjnej częstotliwości CPU. Telemetria rozdziela paint, compose, present, TOTAL oraz odstępy klatek, liczniki i dwie historie do 128 próbek. Deklarowane 1 µs TSC nie jest dowodem dokładności 1 µs. Definicje i końcowe pomiary: [PERFORMANCE.md](PERFORMANCE.md).

`gfx_device` jest cienką warstwą programowych targetów (kolor/głębia, stride/capacity, clear i kształty), nie zamiennikiem całego kompozytora. `soft3d` wykonuje macierze, clipping jednorodny, rasteryzację trójkątów, depth, kolor oraz **teksturowanie** (nearest/bilinear, opcjonalnie perspektywicznie) na CPU. W tej sesji zoptymalizowano rasteryzer: zamiast liczyć trzy funkcje krawędzi i trzy dzielenia na piksel używa przyrostowych funkcji krawędzi (jedno mnożenie przez `1/area`), a `sample_bilinear` przeszedł na całkowite wagi 8.8 bez float w pętli. Renderer poza tym cienki; PollikMark pracuje porcjami w callbacku `poll`, render wyświetla ukończony bufor, a po pełnym przebiegu pokazuje **RESULTS SUMMARY** (ocena literowa, 0–100 pkt, średni throughput z jednostką i liczba ukończonych poziomów na test). Zakres i granice opisuje [POLLIKMARK.md](POLLIKMARK.md).

VBE/Bochs DISPI i QEMU `fw_cfg` umożliwiają wybór rozdzielczości przy starcie. To renderowanie programowe, bez akceleracji GPU. Parametr 120 Hz i napis „120 FPS” w launcherze nie są pomiarem FPS ani gwarancją VSync. Końcowe pomiary nie dowodzą 60/120 FPS.

### 1.6.1. Podział powłoki i rejestr aplikacji GUI — końcowy Surface

Powłokę i aplikacje pulpitu wydzielono z `kernel.c` do osobnych jednostek translacji, kompilowanych niezależnie (bez dołączania tekstowego plików `.c`). Moduły powłoki opisano powyżej; warstwa aplikacji obejmuje:

| Plik | Zawartość |
| --- | --- |
| `kernel/gui/apps.c`, `apps.h` | Statyczny rejestr `GuiApp`: nazwa, ikona, kolory tła, dolny inset i minima rozmiaru; callbacki `init/render/key/click/open/close/resize/scroll/cursor/poll`, zapamiętany pełny rozmiar, dyspozycja i mapa znaków PS/2 |
| `kernel/gui/app_host.h` | Wąskie host API: rysowanie (`ui_bridge_*`, `app_draw_*`, blit), otwarcie okna, unieważnianie powierzchni/sceny, motyw, animacje, zasilanie, czas/metryki, alokacja/zwalnianie i ograniczona obsługa ładowania |
| `kernel/gui/app_internal.h`, `app_edit.c` | Aliasy rysownika, pomocnicze operacje na napisach i wspólny edytor tekstu (schowek do 255 B, Ctrl+C/V, Backspace, Enter) |
| `kernel/gui/welcome.c`, `files.c`, `notes.c`, `terminal.c`, `settings.c` | Pełne renderery oraz prywatny stan i logika odpowiednich aplikacji; `files.c` ma też podglądarkę zdjęć/GIF/`.pkv` (media przez `kernel/media.c`) |
| `kernel/gui/browser_client.c` | Adapter przeglądarki: inicjalizacja, renderowanie, klawiatura, zmiana rozmiaru, przewijanie i polling |
| `kernel/gui/pollikmark.c`, `pollikmark.h` | Slot 6: programowy benchmark, viewport, praca kooperacyjna, metryki i zachowane wyniki |

Granice podziału:

- Moduły GUI nie sięgają do globali WM (`g_windows`) ani prywatnego stanu kompozytora (`dirty_client`); komunikują się przez `gui/app_host.h` i deklaracje z `gui/apps.h`. Kliknięcia i zapytania o kursor używają współrzędnych lokalnych względem okna; kółko przekazuje deltę przewijania. Stan aplikacji pozostaje w ich modułach, nie w rejestrze ani powłoce.
- `gui_apps_init()` wywołuje dostępne callbacki inicjalizacji Notes, przeglądarki i PollikMark. Brak callbacku oznacza no-op (dla kursora wynik domyślny 0); zamknięcie Notes/Terminala nie kasuje ich trwałego w sesji stanu.
- `gui_app_resized(id, width, height)` zapisuje **pełny rozmiar okna**, z chrome 34 px, przed wywołaniem opcjonalnego callbacku. Kompozytor powiadamia o zmianie przed renderem; lokalny początek współrzędnych również obejmuje chrome. `gui_app_size()` przed pierwszym powiadomieniem zwraca 680×410. Wszystkie siedem wpisów rejestru ma minimum **480×280** pełnego okna, pobierane przez WM przy inicjalizacji. To kontrakt resize, nie skalowanie bitmapy klienta.
- `gui_apps_poll()` zwraca maskę bitową identyfikatorów aplikacji zgłaszających zmianę. `desktop_poll()` buduje najpierw `active_mask` z okien `open && visible && !minimized` i woła `gui_apps_poll_mask(active_mask)`, więc callback `poll` zamkniętej/ukrytej aplikacji nie jest w ogóle uruchamiany (koniec pracy `browser_poll`/`files_poll` w tle). `gui_apps_poll()` pozostaje jako wariant z pełną maską dla testów i wywołań bez bramki.
- **Jasny motyw nie ma już osobnej, kosztownej ścieżki tapety.** Aktywny motyw jest renderowany raz do pełnorozdzielczego bufora `u32` (`desktop_paint_wallpaper`), a każde odmalowanie (pełne i częściowe) to zwykły `rep movsl`/`memcpy`. Wcześniej jasny motyw trzymał tylko półrozdzielczą kopię RGB565 i rozwijał ją per-piksel bilinearnie przy każdej klatce z uszkodzeniem regionu — to było źródło lagu tylko na białym motywie.
- Nazwy, ikony, minima oraz parametry tła pochodzą z rejestru. **Nie oznacza to pełnej niezależności aplikacji od WM:** `wm.c` nadal ma siedem stałych slotów, domyślne nazwy i geometrie, a zgodność `APP_COUNT == NUM_APPS` sprawdza asercja w `desktop.c`. IDs 0–5 zachowano, PollikMark ma ID 6 (F7). Rozmiary docka/cache zależą od liczby aplikacji. Nie jest to dynamiczny system wtyczek.
- Notes i Files korzystają bezpośrednio z VFS i PollikFS v2 (`/home/Desktop`, `/home/Trash`), z pełną obsługą plików tekstowych i nawigacji po katalogach. Menu kontekstowe Files wspiera widok Kosza z akcjami Opróżnij Kosz i Przywróć; stan motywu i animacji należy do powłoki.
- To podział na pliki źródłowe, nie migracja do Ring 3: nie zmienia ABI, modelu procesów ani granicy ochrony pamięci.

### 1.7. Prawdziwy Desktop, System Plików PollikFS v2, Kosz i Animacje UI

W ramach etapu „Prawdziwy Desktop PollikOS” zaimplementowano pełnoprawne środowisko pulpitu zintegrowane z PollikFS v2 i VFS:

1. **Struktura katalogów i integracja VFS (`vfs.c`, `pollikfs.c`):**
   - Dodano i podłączono operacje katalogowe `vfs_readdir()` oraz zmianę nazwy/przenoszenie `vfs_rename()`.
   - Pulpit mapuje katalog `/home/Desktop` w PollikFS v2. Wszystkie operacje (tworzenie folderów, plików, zmiana nazwy, usuwanie) to rzeczywiste operacje na i-węzłach i blokach dyskowych.
   - Po restarcie systemu cała zawartość `/home/Desktop` oraz pozycje ikon w `/home/Desktop/.layout` są zachowywane na dysku.

2. **Model elementów pulpitu (`desktop_items.c`, `desktop_items.h`):**
   - Wspólny model `DesktopItem` obsługujący typy: `ITEM_APP` (aplikacje systemowe), `ITEM_DIR` (katalogi), `ITEM_TXT` (pliki tekstowe), `ITEM_FILE` (inne pliki) oraz `ITEM_TRASH` (Kosz).
   - Dynamiczna siatka (grid) o wymiarach komórek 104×96 px z 2-wierszowym inteligentnym zawijaniem etykiet, cieniami tekstu dla czytelności na każdym tle i subtelnym podświetleniem zaznaczenia (selection pill).
   - Obsługa przeciągania ikon (drag & drop) z przyciąganiem do wolnych komórek siatki, upuszczaniem plików do folderów oraz przeciąganiem do Kosza.

3. **System Kosza (`trash.c`, `trash.h`):**
   - Prawdziwy katalog `/home/Trash` na dysku PollikFS v2.
   - Trwałe metadane usuwania w pliku `/home/Trash/.trashinfo` (oryginalna ścieżka, ścieżka w koszu, timestamp usunięcia).
   - Dynamiczny stan ikony Kosza na pulpicie: `empty` oraz `full` (czerwona plakietka wskaźnika zawartości).
   - Integracja z menedżerem plików (Files): otwarcie Kosza wyświetla jego zawartość, z akcjami „Przywróć” (do oryginalnej lokalizacji z ochroną przed nadpisaniem) oraz „Opróżnij Kosz” (trwałe usunięcie i-węzłów).

4. **Silnik animacji zależnych od czasu (`ui_animation.c`, `ui_animation.h`):**
   - Wszystkie animacje bazują na rzeczywistym upływie czasu (`wm_time_us()` / monotoniczny zegar jądra) ze wspólnymi krzywymi cubic easing: `ease_out_cubic`, `ease_in_cubic`, `ease_in_out_cubic`.
   - **Otwieranie okna:** płynne powiększenie (scale 0.94 → 1.0) z rozjaśnieniem (fade 0 → 255) w czasie ~200 ms.
   - **Minimalizacja do Docka:** translacja i skalowanie okna w kierunku środka ikony danej aplikacji w Docku (ease-in, ~220 ms).
   - **Przywracanie z Docka:** translacja i skalowanie z pozycji ikony w Docku do poprzedniej geometrii okna (ease-out, ~220 ms).
   - **Launch feedback:** subtelna animacja bounce ikony w Docku i kropka statusu uruchomionej aplikacji.
   - **Brak repaintu klienta:** kompozytor transformuje i skaluje gotowy bufor `WindowSurface` klienta w RAM. Aplikacja nie renderuje swojej zawartości w każdej klatce animacji (zero client repaints).
   - Zoptymalizowane śledzenie uszkodzeń (damage tracking) minimalizuje redraw do starego i nowego prostokąta powierzchni.

5. **Zaznaczanie prostokątem (Marquee Selection) i Multi-select (`desktop_items.c`, `input_dispatch.c`):**
   - Przeciąganie kursora myszy po pustym tle pulpitu aktywuje dynamiczny prostokąt selekcji.
   - Kompozytor rysuje półprzezroczyste wypełnienie z obwódką o barwie akcentu motywu (`0x6c5ce7`).
   - Funkcja `desktop_items_update_marquee()` oblicza przecięcia geometryczne (AABB) z ramkami ikon w czasie rzeczywistym.
   - Obsługa klawisza Ctrl (śledzona w `input_dispatch.c`) pozwala na dołączanie i odłączanie obiektów z grupy zaznaczenia (`desktop_items_toggle_selected()`).

6. **Niewidzialna siatka, snapping i grupowe przeciąganie (`desktop_items.c`):**
   - Niewidzialna siatka o wymiarach komórek 104×96 px.
   - Zaznaczenie wielu elementów pozwala na ich łączne przesuwanie (`desktop_items_group_move()`) z zachowaniem względnych odległości w wierszach i kolumnach siatki.
   - 3-fazowy algorytm rozmieszczania: faza 1 (obliczenie docelowych komórek), faza 2 (rozwiązywanie kolizji z elementami niezaznaczonymi poprzez poszukiwanie najbliższych wolnych komórek), faza 3 (zatwierdzenie nowych współrzędnych i natychmiastowy zapis do `/home/Desktop/.layout`).

7. **Bezpośrednie przeciąganie do Kosza (Drag to Trash):**
   - Ciągłe sprawdzanie kolizji kursora z ikoną Kosza podczas drag & drop (`desktop_items_hit_trash()`).
   - Podświetlenie koralowym/czerwonym kolorem (`s_trash_hovered = 1`) wskazuje Kosz jako aktywny cel upuszczenia.
   - Upuszczenie elementu wywołuje `trash_move_item()`, usuwając obiekt z pulpitu i zapamiętując metadane w `.trashinfo`. Aplikacje systemowe (`ITEM_APP`) są chronione przed usunięciem.

8. **Dynamiczny Dock i trwałe przypinanie (Dock Pin/Unpin, `/home/.config/dock.conf`):**
   - Układ Docka jest w pełni dynamiczny: szerokość i pozycje ikon są wyliczane na podstawie liczby widocznych elementów (`dock_get_visible_apps()`).
   - **Jedno źródło prawdy dla widoczności:** `visible_in_dock = pinned || running` (gdzie `running` to `windows[id].open`).
   - **Cykl życia Unpin w trakcie działania:** Gdy aplikacja działa (`running = true`, `pinned = true`), a użytkownik wybierze *Unpin from Dock*, `pinned` zmienia się na `false`, lecz ikona **pozostaje w Docku** jako dynamiczna uruchomiona aplikacja ze wskaźnikiem aktywności. Dopiero po zamknięciu okna (`running = false`) ikona znika z Docka.
   - **Przypinanie dynamicznej aplikacji:** Jeśli nieprzypięta aplikacja zostanie uruchomiona i użytkownik wybierze *Pin to Dock*, `pinned` zmienia się na `true` i ikona pozostaje w Docku także po zamknięciu okna.
   - **Wyjątek Files:** Aplikacja `Files` (`APP_FILES`) ma zawsze `pinned = true` — funkcja `dock_set_pinned` odrzuca próbę odpięcia, a menu kontekstowe Docka ukrywa opcję Unpin.
   - **Trwałość konfiguracji:** Przypięcia są zapisywane w `/home/.config/dock.conf` i zachowywane po restarcie systemu. Stan uruchomienia (`running`) jest ulotny i nie jest zapamiętywany w pliku konfiguracyjnym — po restarcie widoczne są tylko przypięte aplikacje.

9. **Ścisła hierarchia przetwarzania wejścia (Input Priority):**
   - W `input_dispatch.c` zaimplementowano bezwzględny porządek obsługi kliknięć i zdarzeń myszy:
     1. Okno modalne / okno dialogowe (blokada pozostałych elementów interfejsu).
     2. Aktywne menu kontekstowe (kliknięcie poza menu natychmiast je zamyka).
     3. Pasek Dock (kliknięcia lewym przyciskiem i menu kontekstowe prawym przyciskiem myszy; Dock jest zawsze rysowany na pierwszym planie nad oknami, więc `dock_hit()` ma priorytet przed oknami tła).
     4. Kontrolki okien (chrome: close, minimize, maximize) oraz obszar klienta aktywnego/najwyższego okna.
     5. Górny pasek pulpitu (Top Bar).
     6. Elementy i ikony pulpitu (zaznaczanie, drag & drop, menu kontekstowe obiektu).
     7. Wolne tło pulpitu (Marquee selection, menu kontekstowe tworzenia folderów/plików).

**Tożsamość końcowego zestawu** według raportów, nie samych dat plików:

> Superseded checkpoint identity: the recorded 522,560-byte image and 512-KiB cap below are historical; the current build has a 4-MiB raw-image cap documented in section 1.9.

| Pole | Wartość |
| --- | --- |
| Obraz | `build/PollikOS-Surface.img` |
| Jądro | **522560 / 524288 B; zapas 1728 B** |
| SHA-256 jądra | `64756ee53d2455ceae00d3e37717d65710377a83a3a7f4560300fb125e7c574b` |
| SHA-256 ELF | `fd5fcba0ebf98accf8094b650ceace136197bb2a9e24eeb5441769bb1249ea96` |
| SHA-256 obrazu | `3b6fd8ed67e1c88b6615b9cf07b4b624bb343fc1d28529c2ab4cedf60cf45cba` |
| Powiązanie ELF/obrazu | Raport UI: `elf_matches_image_offset4608 = true`, tożsamość niezmieniona podczas przebiegów |

Weryfikacja — **wyłącznie zapisane wykonania końcowego zestawu**, nie nowe testy przy edycji dokumentacji i nie przeniesione wyniki 501004/501136/521336 B:

| Zestaw | Wynik i źródło |
| --- | --- |
| Sześć native: `gui_registry`, `held_drag`, `app_layout`, `browser_cooperative`, `soft3d_native`, `pollikmark_native` | **PASS**, `final-ui-522560.json`; rzeczywiste źródła ze stubami, bez bootowania obrazu. Browser: 1009 kooperacyjnych usług |
| `browser_responsive`, standardowy `surface_bounds`, `corners_guards`, `resize_layout`, `pollikmark` — **1024×768 i 1920×1080** | **PASS**, `final-ui-522560.json`; także ponowne standardowe surface bez `--wm`, niezmienione kryteria, exit 0/0. PollikMark: ograniczony scenariusz, nie Run all |
| `benchmark_gui`, `test_perf` — **1024×768 i 1920×1080** | **PASS**, `final-522560-tests.json`; definicje i pomiary w [PERFORMANCE.md](PERFORMANCE.md), nie gwarancja FPS |
| `smoke` — **1024×768** | **PASS**, `final-ui-522560.json`; 1920×1080 **NOT RUN**, skrypt bez opcji rozdzielczości |
| `browser_js`, `browser_e2e`; boot przy **64 i 256 MiB** | **PASS**, `final-522560-tests.json`; nie rozszerza się na pełny benchmark pamięci ani wszystkie konfiguracje sieci |
| Dodatkowy `surface_bounds --wm` — obie rozdzielczości | **Pierwotnie FAIL, następnie PASS po poprawieniu asercji**; zastrzeżenie poniżej |
| `display.py` | **NOT RUN** dla końcowego zestawu; nieaktualne oczekiwania, brak podstaw do PASS |

**Zastrzeżenie WM:** raport ma status `PASS_REQUESTED_SUITE_WITH_WM_ASSERTION_CHANGE_CAVEAT`. Pierwotny dodatkowy test błędnie oczekiwał MAXIMIZED (`state=2`) po minimalizacji; źródło `wm_minimize()` ustawia MINIMIZED (`state=1`), zachowując geometrię i stan sprzed minimalizacji. Poprawiony test sprawdza także zachowany prostokąt i poprzedni stan. Późniejszy PASS **nie jest zaliczeniem pierwotnego kryterium**; nie zmieniano jądra. Pierwotne porażki zachowano w `build/final-ui-522560/initial-attempt.json` i `initial-surface_bounds-wm-*`.

Wcześniejszy `final-522560-tests.json` zawiera browser responsive 1920 jako NOT RUN w swoim przebiegu; późniejszy `final-ui-522560.json` dokumentuje faktyczny PASS obu rozdzielczości po dodaniu argumentu rozdzielczości do harnessu. Raport UI odnotowuje też snapshot obrazu w smoke oraz korektę asercji WM. Testy UI używały snapshot boot disk i oddzielnych dysków tymczasowych, bez podłączania użytkowego `PollikData.img`. Nie jest to globalny dowód bezpieczeństwa danych ani braku wycieków. Szczegóły granic przeglądarki: [BROWSER_RESPONSIVENESS.md](BROWSER_RESPONSIVENESS.md).

### 1.7.1. Dwa równoległe systemy plików

Obie warstwy korzystają z ATA PIO i dysku **primary IDE slave**. `kernel_main()` inicjalizuje zarówno `fs_init()`, jak i `vfs_init()`; obecność v2 nie oznacza automatycznej migracji dokumentów pulpitu.

| Cecha | Starszy PollikFS (`storage.c`) | PollikFS v2 (`pollikfs.c` + `vfs.c`) |
| --- | --- | --- |
| Użytkownicy | Dokumenty pulpitu, starsze `fs_*` i tablica `files[]` | VFS, deskryptory procesów, ścieżki `/bin` i `/home` |
| Położenie | Dwie migawki po 18 sektorów, LBA 0–35 | Od LBA 64, oddzielny obszar tego samego dysku |
| Struktura | Stała lista bez katalogów | Katalogi, inody, bitmapa bloków |
| Limity | Legacy backend: 8 plików po 1023 B (`FS_FILES`, `FS_CAPACITY`), nazwy do 23 znaków | PollikFS v2: 32768 bloków po 1024 B (32 MiB brutto), tabela 512 inodów |
| Zapis | Pełny payload → flush → nagłówek z generacją i sumą → flush | Aktualizacje bloków i metadanych, operacje flush |
| Odporność | Wybór poprawnej migawki A/B; odmowa zapisu przy rozpoznanym uszkodzeniu obu | Brak odpowiednika transakcji A/B lub journalingu dla całości operacji |

i386 ma 16 slotów deskryptorów procesu (do 13 zwykłych oprócz standardowych strumieni) i pulę 64 obiektów VFS. x86_64 ma 128 slotów per proces (fd 0–127, zwykłe otwarcia fd 3–127), a pula obiektów VFS skaluje się z aktywną pojemnością procesu. Deskryptory 0–2 są zarezerwowane na standardowe strumienie; wyjście jest kierowane do portu szeregowego, a nie automatycznie do okna terminala. To prosta warstwa nad jednym backendem, nie kompletny system montowania wielu systemów plików.

**Ważne dla danych i bezpieczeństwo buildu:** `build.ps1` **NIGDY nie formatuje ani nie usuwa automatycznie istniejącego `build/PollikData.img`**.
- Przy starcie buildu narzędzie `tests/migrate_pollikfs.py inspect` weryfikuje sygnaturę i geometrię superbloku.
- Jeśli wykryty zostanie aktualny format PollikFS v2 (`[31, 36]`), build kontynuuje normalnie bez dotykania danych.
- Jeśli wykryty zostanie starszy format o geometrii `[30, 35]`, build automatycznie wykonuje kopię bezpieczeństwa obrazu (`PollikData.img.bak_<timestamp>`) i przeprowadza bezpieczną migrację relokując ewentualne dane użytkownika i powiększając tabelę inodów do 31 bloków bez utraty danych.
- Jeśli format jest nieobsługiwany lub uszkodzony, build jest natychmiast zatrzymywany z czytelnym komunikatem błędu.
- Formatowanie może nastąpić **wyłącznie na jawne żądanie użytkownika** za pomocą przełącznika `.\build.ps1 -FormatData` lub przy tworzeniu całkowicie nowego pustego pliku obrazu.
- `pollikfs_init()` w kernelu **nie formatuje automatycznie** — błąd odczytu lub uszkodzenie powoduje odmowę montowania z `g_fs_mounted=0`.

Inody mają 60 B i nie przekraczają granic bloku 1024 B: 17 slotów/blok, więc 512 slotów wymaga **31 bloków tabeli**, nie 30. Superblok jest w bloku 0, bitmapa 1–4, tabela 5–35, **dane od bloku 36** (numery względne względem obszaru v2 od LBA 64).

To walidacja superbloku, nie pełny fsck, kontrola wszystkich inodów/bitmap ani transakcje. Część operacji nadal nie propaguje wszystkich błędów I/O. `ata_flush()` nie zapewnia atomowości wieloblokowej aktualizacji. Pojemność logiczna obrazu 10 GiB nie jest pojemnością użytkową obecnego v2. Kopie dysku pozostają konieczne. Powyższe stwierdzenia wynikają ze źródła; raporty końcowego zestawu nie zawierają osobnego PASS testu montowania/recovery v2.

### 1.8. Sieć, HTTPS i przeglądarka

Przepływ sieciowy: **RTL8139/PCI/DMA → Ethernet/ARP → IPv4 → ICMP lub UDP/TCP → DHCP/DNS lub TLS/HTTP**. `net_manager.c` koordynuje interfejs i konfigurację, `network.c` łączy stos z pulpitem. Stos jest w dużej części obsługiwany przez polling. `wifi_if.c` jest warstwą przygotowawczą, a nie dowodem obsługi realnej karty Wi-Fi.

TCP ma małą, stałą pulę gniazd (4). DNS korzysta z UDP i cache. HTTPS używa BearSSL z TLS 1.2 oraz weryfikacją certyfikatów, nazwy hosta i czasu. Implementacja wymaga poprawnego czasu RTC i źródła entropii RDRAND; w razie ich braku odmawia połączenia. Zaufanie wynika ze skompilowanych kotwic w `kernel/certs/`; lokalny certyfikat pośrednika nie powinien być traktowany jako uniwersalne zaufanie wydania publicznego.

HTTP/1.1 obsługuje m.in. przekierowania, Content-Length i chunked. Wysyła `Accept-Encoding: identity`; brak dekodowania gzip/brotli, IPv6, HTTP/2 i HTTP/3. Nie jest to kompletny stos sieciowy klasy desktop.

Przeglądarka składa się z:
- `browser_app.c`: nawigacja, historia, stan strony i integracja;
- `html_parser.c`: DOM (w tym `<canvas width height>`);
- `css_engine.c`: ograniczony CSS (m.in. `text-align`, `border`, `background-color`, `border-radius`, `padding/margin`, flex, kolor nazwany/hex);
- `layout.c` i `render.c`: układ i rysowanie (teraz także centrowanie i wyrównanie do prawej pojedynczej linii tekstu oraz `margin: 0 auto`);
- `images.c`: dekodowanie obrazów przez wspólny `kernel/media.c` (PNG/JPEG/BMP/GIF/TGA/PSD/PNM), z budżetem pamięci;
- `js_engine.c` i `js_compat.c`: Elk, wybrany podbiór DOM/zdarzeń, budżet wykonania JS oraz **Canvas 2D** (`canvas.getContext('2d')` z `fillRect/clearRect/strokeRect/beginPath/moveTo/lineTo/closePath/stroke/arc/fill/fillText`, `fillStyle`/`strokeStyle`) rysowany przez PollikGL do bufora węzła i wyświetlany w layoucie.

**Uwaga o JS:** Elk w tej konfiguracji nie obsługuje `var`/`const` ani `this`, a dodawanie nowych właściwości obiektów jest zabronione. Dlatego: (a) `js_decl_shim()` przepisuje `var`/`const` na `let`, żeby zwykłe skrypty stron działały; (b) obiekt kontekstu canvas jest w całości tworzony w C (`js_set`), a metody są dopisywane przez generowany kod do istniejących właściwości; (c) stan ścieżki (pen/arc) trzymany jest w C, nie w `this`.

Nawigacja pobiera HTML, zasoby CSS/obrazy/skrypty, przetwarza DOM i style, oblicza układ oraz uruchamia obsługiwane skrypty. Pozostaje **synchroniczną transakcją głównego wątku jądra**, ale optuje w ograniczoną obsługę kooperacyjną: wybrane granice oczekiwania sieciowego i pracy parserów wywołują throttlowaną usługę wejścia okien i prezentacji. Nie jest to asynchroniczne HTTP/TLS ani worker. Podczas ładowania można obsługiwać kursor i okna, lecz nie klientowe kliknięcia/typing/scroll, dock launches ani polling innych aplikacji. Osobny `load_active` chroni zmieniany DOM i odrzuca rekurencyjne ładowanie; zamknięcie okna nie anuluje stosu pobierania. Kopiowanie/alokacje, kryptografia, dekodowanie obrazów, interpreter i końcowy zwykły layout/raster nadal mogą blokować. Dokładne checkpointy i ograniczenia: [BROWSER_RESPONSIVENESS.md](BROWSER_RESPONSIVENESS.md). Terminalowe HTTP nie optuje w tę usługę. Nie ma zgodności z pełnym współczesnym Web API, odtwarzania YouTube ani izolowanego procesu renderera.

**Granica zaufania:** TLS chroni transport, lecz nie czyni treści strony bezpieczną dla parsera. Przeglądarka i biblioteki przetwarzające zewnętrzne dane działają w jądrze; przeniesienie ich do Ring 3 jest priorytetem architektonicznym, nie gotową funkcją.

### 1.9. Sprzęt, budowanie i zasoby

Domyślne środowisko launchera to QEMU `pc`: jeden procesor, `-cpu max`, RTC UTC, 2 GiB RAM, Standard VGA z 32 MiB VRAM, dwa dyski IDE i RTL8139 z siecią użytkownika QEMU. Nazwa `qemu-system-x86_64` nie zmienia jądra w system 64-bitowy. Launcher dopuszcza zakres 1024×720–3440×1440, z szerokością podzielną przez 8. `run.ps1` domyślnie wybiera **`PollikOS-Alpha.img`**, a `Start-PollikOS.cmd` nie nadpisuje tego wyboru. Nie uruchamiają automatycznie zweryfikowanego Surface: potrzebny jest jawny parametr `-ImageName PollikOS-Surface.img`. Nie zmieniano launcherów przy tej aktualizacji. Końcowe pomiary GUI używały odrębnego środowiska testowego (TCG, 256 MiB, 1024×768/1920×1080, bez NIC), nie domyślnych 2 GiB launchera.

`hw.c` zawiera RTC, skaner PCI, PC speaker oraz ścieżki wyłączania/restartu dla obsługiwanego środowiska. Audio ma ograniczony sterownik Intel ICH AC'97 i fallback PC Speaker (`kernel/audio.c`). Skrypt buduje obraz instalacyjny USB Live, ale nie ma natywnego stosu USB host/HID/storage ani UEFI. W kodzie jest ścieżka AHCI/SATA; deklaracje sprzętowe pozostają ograniczone do testowanych urządzeń, a fizyczny sprzęt nie był walidowany.

`build.ps1` wymaga NASM, Clang, LLD, llvm-objcopy, llvm-ar oraz Windowsowego `fsutil` do obrazów sparse. Kompiluje własne moduły z `-Wall -Wextra -Werror`, buduje bibliotekę BearSSL i ELF-y demonstracyjne, linkuje `kernel.elf`, a następnie tworzy `kernel.bin`. Obraz rozruchowy ma układ: sektor startowy → 8 sektorów Stage 2 (z liczbą sektorów jądra wpisaną w ostatnie słowo) → jądro od bajtu 4608. Stage 2 ładuje od 1 MiB porcjami 32 KiB; skrypt odrzuca surowy obraz `kernel.bin` większy niż 4 MiB, a linker utrzymuje koniec BSS poniżej `0x600000`. Nie obowiązuje dawny limit 512 KiB poniżej EBDA. Zweryfikowany build z 2026-10-04 wygenerował `kernel.bin` 3,232,332 B (6314 sektorów); osobny kernel USB-installer miał 4,029,004 B. Oba obrazy są poniżej skryptowego limitu 4 MiB.

Oba obrazy mają po 10 GiB pojemności logicznej. Build odtwarza obraz systemowy, zachowując istniejący dysk danych; stosuje plik `.pending` i kopię `.previous` przy podmianie obrazu. To nie jest kopia zapasowa dokumentów. Zawartość `/bin` instalowana podczas formatowania v2 nie jest automatycznie aktualizowana samym przebudowaniem kernela. Cache BearSSL opiera się na pliku stamp, nie na pełnym śledzeniu zmian źródeł.

`assets/build_ui.py` i `fonts/build_font.py` generują tablice grafik i czcionek; do regeneracji potrzebny jest Python i Pillow. Zwykły build używa gotowych nagłówków. `third_party/` zawiera biblioteki z własnymi licencjami, a `build/` — artefakty, obrazy, logi i zrzuty ekranu; nie należy liczyć ich jako ręcznie napisanego kodu systemu.

### 1.10. Testy i granice weryfikacji

Poniższa tabela opisuje zakres scenariuszy w kodzie testów, **nie dodatkowe wyniki PASS**. Rzeczywista macierz końcowego Surface jest w 1.6.1; nie rozszerza się automatycznie na pozostałe scenariusze.

| Plik | Zakres |
| --- | --- |
| `tests/smoke.py` | Rozruch, pulpit, wejście, dokumenty starszego FS, restart, workery i podstawowa sieć |
| `tests/process_stress.py` | Czeka na wynik testów jądra po starcie: `vmm_isolation_self_test`, trzy ELF-y błędów Ring 3 oraz **100 rzeczywistych cykli spawn → Ring 3 → exit → reap** wbudowanego `hello` z bilansem wolnych stron PMM; asercja braku PANIC |
| `tests/compare_shots.py` | Pomocnicze porównanie pikselowe zrzutów PPM z referencyjnymi (regresja wizualna) |
| `tests/gui_registry.py` | Natywny test routingu callbacków rzeczywistego rejestru `gui/apps.c`, bez rozruchu QEMU |
| `tests/recovery.py` | Uszkodzenia migawek starszego FS i brak urządzeń; nie pełna odporność v2 |
| `tests/network_ring.py` | 300 ramek ARP i zawijanie pierścienia RTL8139 |
| `tests/browser_e2e.py` | Scenariusz DHCP/DNS/TCP/TLS/HTTP zależny od warunków sieciowych |
| `tests/browser_js.py` | Lokalna strona HTTP z CSS, obrazem, JS, DOM i kliknięciem |
| `tests/display.py` | Wybrane rozdzielczości i operacje na oknach |
| `tests/benchmark_gui.py`, `tests/test_perf.py` | Scenariusze i odczyt telemetrii GUI, nie gwarancja określonego FPS |
| `tests/generate_trust.py` | Narzędzie przygotowania kotwic zaufania, nie test wykonania systemu |

Jądro uruchamia autotesty PMM/VMM (w tym izolację przestrzeni adresowych) i zawiera `phase2_poll()`, który po starcie przeprowadza testy wyjątków procesów, a następnie **100 rzeczywistych cykli** `process_spawn_elf(hello)` → wykonanie w Ring 3 (`SYS_GETPID`, `SYS_WRITE`, IPC, poll zdarzeń, `SYS_YIELD`, `SYS_EXIT(42)`) → reap przez scheduler; kolejny cykl startuje dopiero, gdy poprzedni slot ma stan UNUSED. Bilans wolnych stron PMM przed i po musi być identyczny (`[TEST] PMM no leak`). Dawny komunikat o „100 spawn/exit cycles” dotyczył jedynie pętli tworzenia/usuwania katalogów stron; został zastąpiony. `PHASE 2 PASS` potwierdza izolację błędów, brak wycieku ramek w cyklu życia procesu ELF i działanie desktopu w trakcie, nie wszystkie właściwości procesów (np. limity, sygnały, wait).

> Superseded checkpoint: the test results and 525,312-byte kernel size below describe the September Stage A image; current build limits and size are recorded in section 1.9.

Wyniki po zmianach STAGE A (jądro 525312 B, 18 września): `tests/process_stress.py` PASS przy 256 MiB i 2048 MiB (100/100 uruchomień Ring 3, 0 błędów spawn, wolne strony PMM identyczne: 56448/56448 i 252896/252896); `tests/boot_memory.py` PASS 64/256/2048 MiB; `tests/smoke.py` PASS; `tests/recovery.py` PASS; `tests/network_ring.py` PASS; `tests/pollikfs_mount.py` PASS (7 scenariuszy); `tests/surface_bounds.py` PASS 1024×768 (z `--wm` i bez), 1920×1080, 3440×1440 `--wm`; `tests/corners_guards.py` PASS 1024×768; `tests/test_stage1_wm.py` PASS. Siedem zrzutów ekranu 1024×768 (pulpit, terminal, settings, titlebar, overlap, Alt-Tab, surface) jest **pikselowo identycznych** z zarchiwizowanymi referencjami `build/final-ui-522560/` (`tests/compare_shots.py`, 0 różniących się pikseli). Nie wykonano ponownie: `browser_e2e`, `browser_js`, `benchmark_gui`, `test_perf`, `pollikmark`, `display.py`.

[PERFORMANCE.md](PERFORMANCE.md) zawiera końcowe pomiary AFTER dla 522560 B; brak porównywalnego, zweryfikowanego BEFORE, więc nie wylicza przyspieszenia. [POLLIKMARK.md](POLLIKMARK.md) opisuje ograniczony scenariusz i rozróżnia ukończone, anulowane oraz niewykonane obciążenia. Deklaracji z ROADMAP o „100% PASSED”, braku wycieków czy rozmiarze binarium nie traktujemy jako świeżych pomiarów. Testy spoza końcowych raportów, pełny PollikMark Run all, inne rozdzielczości, sprzęt fizyczny i próby długotrwałe nie mają tu statusu PASS.

### 1.11. Ocena i zalecana kolejność rozwoju

**Mocna strona:** projekt łączy własny rozruch, kod sprzętowy, graficzny pulpit, trwałe dane i rzeczywiste protokoły sieciowe. Ma też fundamenty userspace i automatyzację testów. Główna trudność to nie brak modułów, lecz ich niepełne rozdzielenie i współistnienie starszych oraz nowszych ścieżek.

Priorytety, będące propozycją dalszej pracy, nie zmianami wykonanymi w tym przeglądzie:

1. **Poprawność pamięci i własność zasobów:** kolizja identity mapping z zakresem ELF i zapis do współdzielonych tablic stron zostały usunięte (1.4); pozostaje audyt nieudanych alokacji w `elf_load`/`process_sbrk` (brak sprawdzenia wyniku `allocate_page` w sbrk), presji/fragmentacji pamięci i ścieżek awarii oraz zwalnianie gniazd TCP po zakończeniu procesu (`g_user_sockets` w `syscall.c` nie ma właściciela). Scena, tapeta i powierzchnie okien są przydzielane przez PMM według rozdzielczości; `compositor_init()` i WM zatrzymują start przy braku pamięci. PASS dla 64/256/2048 MiB i trzech rozdzielczości nie jest dowodem dla wszystkich konfiguracji. Znane ograniczenie: RAM powyżej 1 GiB pozostaje nieużywany do czasu migracji na x86_64 (STAGE B), gdzie jądro w wyższej połowie i direct map 64-bit usuną ten podział.
2. **Niezawodność danych:** utrzymać istniejącą odmowę montowania niepoprawnego superbloku v2 i oddzielenie jawnego formatowania; rozszerzyć kontrolę spójności, pełną obsługę błędów I/O, transakcje lub dziennik i osobne testy recovery v2. Następnie bezpieczna migracja Notes/Files do VFS.
3. **Granica aplikacja–jądro:** stabilne ABI i własność uchwytów, protokół okien oraz migracja jednej małej aplikacji do ELF. Same funkcje `sys_draw_*` w C nie są graficznym ABI Ring 3.
4. **Izolacja przeglądarki i asynchroniczne I/O:** przenieść parsery/renderowanie/JS do procesu z ograniczeniami zasobów; uniezależnić responsywność pulpitu od pobierania sieciowego. Na dziś działa już HTTPS, podstawowy DOM/JS, Canvas 2D, centrowanie i „boxy" oraz media (obrazy/GIF/`.pkv`); nadal brak pełnego Web API, `fetch`/XHR, pełnego Canvas i prawdziwego wideo (H.264/VP9) — YouTube jako SPA tym silnikiem się nie wyrenderuje.
5. **Porządek implementacji i pomiarów:** aplikacje GUI i powłoka są już wydzielone źródłowo z `kernel.c`, metadane i callbacki skupia rejestr, a GUI ma 64-bitowy zegar i rozdzieloną telemetrię. Pozostało oddzielić autotesty od zwykłego startu, zachować powtarzalność pomiarów i aktualizować README/ROADMAP według zapisanych wyników. Ta aktualizacja dotyczy dokumentacji bieżącego stanu kodu (w tym z 22 września 2026), nie jest spekulacyjną optymalizacją.

Zmiany kodu z sesji mediów/UI/Canvas opisuje 1.12; powyższe priorytety to propozycja dalszej pracy, nie zrealizowane zadania.

---

### 1.12. Warstwa mediów, PollikGL i Canvas 2D (22 września 2026)

Ta warstwa jest wspólna dla aplikacji i przeglądarki i nie zmienia modelu procesów ani granicy Ring 0/3 — nadal działa w jądrze.

**Wspólny dekoder mediów (`kernel/media.c`, `media.h`).**
- `media_decode(data, len, *w, *h)` — statyczne obrazy przez `stb_image` (PNG/JPEG/BMP/GIF pierwsza klatka/TGA/PSD/PNM), limit 4096×4096 i 4 MP na obraz, budżet 8 MP na stronę. Zwraca RGBA8 alokowane `kmalloc`; zwalniane `media_free`.
- **Animowany GIF** — własny dekoder LZW + kompozytor klatek (`media_gif_open`, `media_gif_canvas`, `media_gif_due`, `media_gif_close`): palety globalne/lokalne, przezroczystość, disposal 0/1/2/3, interlace, pętla, sterowanie czasem przez `ticks`.
- **„Wideo" `.pkv` (MJPEG)** — minimalny kontener: nagłówek `PKV1` + `w/h/liczba klatek/opóźnienie` + tabela `(offset,len)` klatek JPEG (`media_clip_*`). Każda klatka jest niezależnie dekodowana przez `media_decode`. To realny, choć prosty odtwarzacz; **nie** jest to H.264/VP9 ani MP4 — pełne kodeki pozostają poza zakresem.

**Podglądarka zdjęć w Files (`kernel/gui/files.c`).** Dwuklik pliku obsługiwanego dołącza `files_is_media`; `files_open_image` (na starcie woła `files_close`, żeby poprzedni obraz/GIF/klip nie mieszał się z nowym) dekoduje i pokazuje w trybie podglądu: obraz dopasowany do okna (skalowanie nearest przez `sys_draw_canvas_clipped`/`graphics_blit_*`), nagłówek, `< Back`, `Esc`. GIF/`.pkv` animują się (`files_poll` → `compositor_invalidate_animated`), co przy częściowym odświeżaniu nie zacieka kursora.

**PollikGL (`kernel/pollikgl.c`, `pollikgl.h`).** Własna, software'owa warstwa rysowania w stylu DirectX/Vulkan (bez GPU): `pgl_begin/clip/clear/fill_rect/stroke_rect/line/fill_triangle/fill_circle/stroke_circle/blit`. Używa jej Canvas 2D; może być bazą dla przyszłych aplikacji graficznych. To **nie** jest ani DirectX, ani Vulkan — prawdziwe API sterują sprzętowym GPU, którego tu nie ma.

**Canvas 2D w przeglądarce.** `<canvas width height>` jest parsowany i ma bufor `0x00RRGGBB` w `DomNode`. `canvas.getContext('2d')` zwraca obiekt rysujący do tego bufora; layout nadaje rozmiar, render wyświetla bufor. Obsługiwane metody: `fillRect`, `clearRect`, `strokeRect`, `beginPath`, `moveTo`, `lineTo`, `closePath`, `stroke`, `arc`, `fill`, `fillText`, oraz właściwości `fillStyle`/`strokeStyle`. Tekst do bufora rysuje `sys_text_to_buffer` w `graphics.c`. To podzbiór standardu Canvas 2D, wystarczający do rysowania i prostych animacji.

**Próbki mediów na pulpicie (`kernel/sample_media.h`, `tools/gen_sample_media.py`).** `desktop_items_init()` seeduje jednorazowo na `/home/Desktop` trzy wygenerowane pliki: `photo.png`, `animation.gif`, `film.pkv`. Pliki można otwierać dwuklikiem z pulpitu lub z Files. Generator odtwarza nagłówek i tablice C.

**Dodatki rasteryzera/UI.** `graphics.c` zyskał: `roundrect_stroke` (pełny obrys narożników), `graphics_blit_rgba` i `sys_draw_rgba_clipped`, `sys_draw_canvas_clipped` (blit bufora `0x00RRGGBB`), `sys_text_to_buffer`. Powłoka dostała nowe mosty w `app_host.h`/`ui.h` (`ui_bridge_roundrect_stroke`, `ui_bridge_rounded`, `ui_bridge_blit_rgba`, `ui_bridge_roundrect_border`).

> Superseded checkpoint: the build-size note below records the September 22 image; current size and load limit are documented in section 1.9.

**Build.** Do listy modułów jądra i linkowania dodano `kernel/media.c` i `kernel/pollikgl.c` (`build.ps1`). Jądro przekracza 4 MiB? Nie — mieści się w limicie (ok. 690 KB). `tools/gen_sample_media.py` odtwarza `kernel/sample_media.h`.

**Granice.** Cały dekoder/stb, PollikGL, Canvas i JS nadal wykonują się w Ring 0 na danych z sieci/dysku; izolacja przeglądarki pozostaje priorytetem architektonicznym (1.11). Brak pełnego standardu Canvas, pełnego DOM, `fetch`/XHR, HTTP/2/3, mediów strumieniowych i odtwarzania wideo z prawdziwych kodeków.

---

### 1.13. Architektura x86_64, PollikOS C SDK i Self-Hosting (TinyCC)

Równolegle do bazowego jądra i386, w gałęzi `kernel/arch/x86_64` oraz katalogu `sdk/` zaimplementowano kompletny, natywny 64-bitowy podsystem, budowany przez `build-x86_64.ps1`. Nie jest to emulacja ani warstwa translacji, lecz samodzielne jądro w wyższej połówce pamięci wraz z pełnym środowiskiem wykonawczym userspace.

1. **Pamięć i model wirtualny (`kernel/arch/x86_64/memory.h`, `vmm.c`, `pmm.c`):**
   - Jądro działa w wyższej połówce pamięci wirtualnej (`MM_KERNEL_START = 0xFFFF800000000000`).
   - 4-poziomowe stronicowanie x86_64 (PML4, PDPT, PD, PT) z obsługą sprzętowego bitu NX (No-Execute) oraz ochroną Write-Protect (`CR0.WP`).
   - Przestrzeń użytkownika obejmuje zakres canonical lower-half, a stos użytkownika jest zabezpieczony dynamicznymi guard pages.
   - PMM zarządza całą dostępną pamięcią fizyczną z mapy E820, znosząc 32-bitowe ograniczenie 1 GiB.

2. **Model procesów i hierarchia (`process.c`, `scheduler.c`, `PROCESS_MODEL.md`):**
   - Do 1024 slotów procesu (`PROCESS_MAX`), z aktywną pojemnością zależną od RAM (32/64/128/256/512/1024) i monotonicznymi 64-bitowymi PID.
   - TCB `Thread64` z dedykowanym stanem rejestrów, stosem jądra i buforem FPU/SSE2 (izolowany kontekst x87/SSE dla każdego procesu).
   - W pełni zaimplementowany model `pollikos_spawn` / `pollikos_waitpid`: ścisła relacja rodzic-dziecko, blocking wait, typowane kody zakończenia (`POLLIKOS_WAIT_EXITED`, `POLLIKOS_WAIT_SIGNALED`), procesy zombie oraz automatyczne adoptowanie i sprzątanie sierot przez proces nadrzędny (reaper).
   - Obsługa środowiska procesów (`envp`, `setenv`, `unsetenv`), ścieżki bieżącej (`cwd`) oraz przeszukiwania `PATH` przez `pollikos_spawnp`.

3. **Pliki, mutacja i PollikFS v2 na x86_64 (`file.c`, `fs_platform.c`, `FILE_MUTATION.md`):**
   - Pełna obsługa mutacji: `open` z flagami `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`.
   - PollikFS v2 używa 8 direct, 256 single-indirect i 65,536 double-indirect wskaźników: inode może reprezentować 67,379,200 B (~64.24 MiB), ale cały wolumen ma 32 MiB brutto, więc dostępna zawartość pliku jest mniejsza po odjęciu metadanych i innych danych (format dyskowy bez zmian).
   - Funkcje manipulacji strukturą katalogów: `mkdir`, `unlink`, `rmdir`, `rename` z pełną walidacją atomowości i odpornością na zapełnienie dysku.
   - Dedykowane 64-bajtowe ABI metadanych (`stat`, `fstat`) z 64-bitowym rozmiarem pliku i timestampami.
   - Enumeracja katalogów (`opendir`, `readdir`) ze stabilnymi deskryptorami katalogów per-proces.

4. **IPC i potoki (`pipe.c`, `pipe.h`):**
   - Jednokierunkowe, zbuforowane potoki jądra z semantyką POSIX `pipe()`.
   - Obsługa duplikacji deskryptorów (`dup`, `dup2`) umożliwiająca przekierowania standardowych strumieni `stdin`/`stdout`/`stderr` oraz budowanie wieloetapowych potoków (`cmd1 | cmd2 | cmd3`) w powłoce.

5. **Warstwa graficzna i okna x86_64 (`window.c`, `console_fb.c`, `mouse.c`):**
   - Serwer okien z kompozytorem bezpośrednio zarządzającym buforami okien posiadanymi przez procesy użytkownika (`Window64`).
   - Proces w Ring 3 otrzymuje dedykowaną pamięć na bufor pikseli, a kompozytor renderuje obramowania, pasek tytułowy, kursor myszy i prezentuje zawartość do bufora ramki.
   - Obsługa wejścia: kolejka zdarzeń myszy i klawiatury per okno (`WINDOW_KEY_QUEUE`).
   - Konsola tekstowo-graficzna (`console_fb.c`) i sterownik TTY (`tty.c`) z emulacją terminala.

6. **PollikOS C SDK i samowystarczalność kompilacji (`sdk/`, `SELF_HOSTING.md`):**
   - Kompletne środowisko cross-developmentu i kompilacji natywnej:
     - `crt0.o` i biblioteka `libpollikc.a`: implementacja libc obejmująca buforowane I/O (`fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets`, `printf`, `snprintf`, `perror`), alokator sterty `malloc`/`free` nad `sbrk`/`mmap`, funkcje łańcuchowe, konwersje, `qsort`, `bsearch`, środowisko i czas.
     - Biblioteka matematyczna `<math.h>` ze skalarną obsługą `float` i `double` na x87/SSE2 (funkcje trygonometryczne, wykładnicze, logarytmy, potęgi).
     - Sterownik kompilacji `pollikcc` pozwalający na budowanie programów C do natywnych binarek ELF64.
   - **Interaktywna powłoka `/bin/pollish` (`sdk/apps/pollish.c`):**
     - Prompt ze ścieżką roboczą, edycja linii, historia poleceń utrwalana w `/home/.pollik_history`.
     - Rozwijanie zmiennych środowiskowych i statusu `$?`.
     - Wbudowane polecenia: `ls`/`dir`, `cat`/`type`, `mkdir`, `rm`/`del`, `rmdir`, `mv`/`rename`, `touch`, `clear`, `echo`, `exit`.
     - Wykonywanie zewnętrznych programów ELF z argumentami i potokami.
   - **Natywny kompilator TinyCC 0.9.27 (`/bin/tcc`):**
     - Sklonowany i zaadaptowany kod TinyCC osadzony w systemie plików (`/usr/include`, `/usr/lib/crt0.o`, `/usr/lib/libc.a`, `/usr/lib/tcc/libtcc1.a`).
     - Sterownik self-hostingu potrafi przekompilować `libc.a` za pomocą `tcc -c` bezpośrednio wewnątrz działającego systemu PollikOS, osiągając kamień milowy samowystarczalności kompilatora (C8–C10).
   - **Samodzielna przeglądarka Ring 3 (`sdk/apps/browser.c`, `sdk/apps/browser_js.c`):**
     - Przeniesienie silnika przeglądarki do aplikacji userspace linkowanej z C SDK i `libpollikc.a`.
     - Komunikacja z serwerem okien przez `<pollikos/window.h>`, pobieranie stron z dysku i sieci przez `<pollikos/fs.h>` i `<pollikos/net.h>`.

---

### 1.14. Podsystem Uwierzytelniania, Baza Kont i Instalator USB Live

W wersji 0.1 wprowadzono zintegrowany mechanizm tożsamości użytkownika oraz nośnik instalacyjny:

1. **Format bazy kont (`kernel/auth.c`, `kernel/auth.h`):**
   - Plik `/etc/account.db` przechowuje rekord `AccountRecord` (128 bajtów, sygnatura `ACR1`, wersja 1).
   - Bezpieczeństwo haseł: 16-bajtowa kryptograficzna sól oraz funkcja KDF (Key Derivation Function) wykonująca 8192 rundy haszowania oparte na algorytmach BearSSL.
   - Stany uwierzytelniania: `AUTH_SETUP_INTRO`, `AUTH_SETUP_NAME`, `AUTH_SETUP_PASSWORD`, `AUTH_SETUP_CONFIRM`, `AUTH_LOGIN`, `AUTH_FORMAT_WARNING`.
   - Przy pierwszym starcie uruchamiany jest asystent tworzenia konta; kolejne uruchomienia witają użytkownika ekranem blokady/logowania.

2. **Instalator USB Live (`kernel/installer.c`, `kernel/installer.h`):**
   - Skrypt budowania tworzy hybrydowy obraz instalacyjny `build/PollikOS-USB-Installer.img`.
   - Jądro uruchomione w trybie instalatora (`POLLIK_INSTALL_MEDIA`) wykrywa docelowy dysk twardy (Primary Master ATA via `storage_install_target_info`).
   - `installer_write_system()` kopiuje obraz runtime z partycji instalacyjnej bezpośrednio na docelowy dysk, weryfikuje sumy kontrolne i przygotowuje strukturę PollikFS v2.

---

### 1.15. Sterownik SATA AHCI i Warstwa Pamięci Masowej

Obok tradycyjnego sterownika ATA PIO (Legacy IDE na portach `0x1F0`/`0x170`), jądro zyskało pełny sterownik **Serial ATA Advanced Host Controller Interface (AHCI)** w `kernel/ahci.c` i `kernel/ahci.h`:

- Wykrywanie kontrolera AHCI przez enumerację magistrali PCI (klasa 0x01, podklasa 0x06, interfejs 0x01).
- Mapowanie obszaru ABAR (AHCI Base Address Register) do przestrzeni wirtualnej jądra przez VMM.
- Inicjalizacja HBA (Host Bus Adapter), włączenie silnika AHCI (`GHC.AE = 1`), alokacja Command List (CLB), Received FIS (FB) oraz tablicy Command Table.
- Przesyłanie danych w trybie DMA z użyciem struktur PRDT (Physical Region Descriptor Table), co eliminuje obciążenie CPU związane z odczytem/zapisem sektorów w pętli PIO.
- Przezroczysta integracja z `kernel/storage.c`: warstwa pamięci masowej automatycznie wybiera AHCI w obecności nowoczesnego kontrolera SATA, zachowując ATA PIO jako bezpieczny fallback.

---

### 1.16. Higiena Nagłówków i Zgodność z IWYU / Clangd

W ramach porządkowania granic kompilacji przeprowadzono audyt czystości nagłówków (Include-What-You-Use):
- **`kernel/browser/browser.h`**: usunięto zbędne dołączenie `../mem.h`; moduły implementujące alokacje pamięci (`browser_app.c`, `html_parser.c`, `css_engine.c`, `js_engine.c`) dołączają `mem.h` bezpośrednio.
- **`kernel/gui/apps.c`**: usunięto nieużywany nagłówek `app_host.h`.
- **`kernel/syscall.h`**: usunięto niepotrzebne nagłówki `system.h` i `pollikos_abi.h`; nagłówek syscalli definiuje wyłącznie kody operacji i błędy.
- **`third_party/bearssl/src/x509/x509_minimal.c`**: usunięto zduplikowane dołączenie `inner.h` i oznaczono wymagane dołączenie pragmą `// IWYU pragma: keep`.

---

### 1.17. Podsystem Dźwięku (Audio AC'97 & PC Speaker)

W wersji 0.1 wprowadzono sprzętowy podsystem audio (`kernel/audio.c`, `kernel/audio.h`):

1. **Sterownik Intel 82801AA (ICH AC'97 Audio):**
   - Wykrywanie kontrolera na magistrali PCI (klasa 0x04 Multimedia, podklasa 0x01 Audio Controller lub ID 0x8086:0x2415).
   - Konfiguracja rejestrów PCI: włączenie I/O Space (bit 0) oraz Bus Master DMA (bit 2).
   - Odczyt I/O baz: NAMBAR (Native Audio Mixer) oraz NABMBAR (Native Audio Bus Master).
   - Alokacja fizycznych stron PMM na tablicę deskryptorów buforów (Buffer Descriptor List - BDL) oraz bufor próbek PCM.
   - Konfiguracja kodeka AC'97: reset miksera, ustawienie głośności Master i PCM Out (tłumienie 0 dB do -46.5 dB), włączenie Variable Rate Audio (VRA) i taktowania 48 000 Hz.
   - Sprzętowe odtwarzanie dźwięku w trybie Bus Master DMA: wypełnienie deskryptora BDL, programowanie rejestrów `BDBAR` i `LVI`, start silnika DMA bitem `CR.RP`.

2. **Dźwięki systemowe i Fallback:**
   - Predefiniowany zestaw motywów dźwiękowych: akord startowy pulpitu (`SOUND_STARTUP`), kliknięcie (`SOUND_CLICK`), alert systemowy (`SOUND_ALERT`) oraz opróżnienie kosza (`SOUND_TRASH`).
   - W przypadku braku kontrolera AC'97 (np. na starszych maszynach wirtualnych lub bez opcji `-device AC97`), podsystem automatycznie i bezgłośnie przełącza się na fallback do klasycznego sterownika PC Speaker (`speaker_beep`).
   - Polecenie `sound` w powłoce terminala pozwala na testowanie tonów, odtwarzanie motywów oraz weryfikację stanu sprzętu.

---



## 2. Docelowa architektura — koncepcja, nie stan wykonania

Poniższy zachowany schemat przedstawia kierunek rozwoju. Procesy ELF, PMM, VMM i podstawowy VFS już istnieją, ale aplikacje GUI, izolowana przeglądarka, pełny Window Server i część sterowników pozostają celem. Schemat nie potwierdza bezpieczeństwa ani kompletności tych warstw.

```
+-------------------------------------------------------------------------+
|                          USERSPACE (Ring 3)                             |
|  +--------------+  +--------------+  +--------------+  +--------------+ |
|  |   Terminal   |  |    Notes     |  |    Files     |  |   Browser    | |
|  |    (ELF)     |  |    (ELF)     |  |    (ELF)     |  | (Sandboxed)  | |
|  +-------+------+  +-------+------+  +-------+------+  +-------+------+ |
|          |                 |                 |                 |        |
|          +-----------------+--------+--------+-----------------+        |
|                                     |                                   |
|                             PollikOS User SDK                           |
|                      (libsys: crt0, syscalls, io)                       |
+-------------------------------------|-----------------------------------+
                                      | INT 0x80 (Czyste ABI)
+-------------------------------------v-----------------------------------+
|                           KERNEL (Ring 0)                               |
|                                                                         |
|  +-------------------+  +-------------------+  +---------------------+  |
|  |    Syscall Core   |  |   VFS Subsystem   |  |    Socket Layer     |  |
|  | (Arg validation,  |  |  (Mounts, Inodes, |  |   (TCP/UDP handles, |  |
|  |  FD table, Dispatch) |  FDs, PollikFS v2)|  |    BSD-like API)    |  |
|  +---------+---------+  +---------+---------+  +----------+----------+  |
|            |                      |                       |             |
|  +---------v----------------------v-----------------------v----------+ |
|  |                   Process & Scheduler Subsystem                    | |
|  |    (ELF32 Loader, PCB, States: READY/RUNNING/BLOCKED/SLEEP, IPC,   | |
|  |     Event Queues, Per-Process Address Space, FPU/Context Switch)   | |
|  +--------------------------------+-----------------------------------+ |
|                                   |                                     |
|  +--------------------------------v-----------------------------------+ |
|  |                     Virtual Memory Manager (VMM)                   | |
|  |    (Page Directory, Page Tables, 4 KiB Pages, User/Supervisor,     | |
|  |     Page Fault Handler, invlpg, Virtual Address Space Layout)      | |
|  +--------------------------------+-----------------------------------+ |
|                                   |                                     |
|  +--------------------------------v-----------------------------------+ |
|  |                    Physical Memory Manager (PMM)                   | |
|  |    (BIOS E820 Map, Page Frame Allocator, 4 KiB Bitmap, Free Pools) | |
|  +--------------------------------------------------------------------+ |
|                                                                         |
|  +--------------------------------------------------------------------+ |
|  |                          Driver Subsystem                          | |
|  |  [VBE/DISPI LFB] [RTL8139 DMA] [IDE/ATA] [PS/2 & USB HID] [AC97]    | |
|  +--------------------------------------------------------------------+ |
+-------------------------------------------------------------------------+
```

---

## 3. Kryteria dalszego rozwoju

Poniższe punkty są kryteriami akceptacji przyszłych zmian, a nie listą ukończonych funkcji. Obowiązujące adresy i limity opisuje sekcja 1 oraz nagłówki źródłowe; dawny projekt z ELF od `0x40000000` nie jest aktualnym ABI.

### 3.1. Pamięć i procesy

- Jednoznacznie oddzielić wirtualne zakresy jądra i użytkownika od fizycznej mapy RAM. Nowe mapowanie użytkownika nie może zmieniać współdzielonej tablicy jądra ani innego procesu.
- Powiązać rezerwacje framebufferów, powierzchni, DMA i stosów z rzeczywiście dostępną pamięcią; wyeliminować nakładanie zakresów i obsłużyć niewystarczający RAM.
- Przetestować rollback każdej nieudanej alokacji ELF, stosu, sterty i deskryptorów oraz zwalnianie zasobów po exit i wyjątku.
- Rozdzielić guard pages stosów użytkownika i jądra, z poprawnym raportowaniem właściciela.
- Utrzymać jawne ograniczenia x86-32 bez NX, zamiast deklarować nieistniejącą ochronę wykonania.

### 3.2. Trwałe dane i VFS

- Zachować oddzielenie montowania od jawnego formatowania i istniejącą odmowę przy niepoprawnym superbloku; rozszerzyć walidację na pełną spójność metadanych bez nadpisywania uszkodzonego obszaru.
- Zapewnić sprawdzanie błędów każdej operacji dyskowej i spójność wieloblokowych aktualizacji, np. przez dziennik lub copy-on-write.
- Dodać testy awarii zapisu i restartu specyficzne dla v2, obejmujące dane, katalogi, inody i bitmapę.
- Zaprojektować migrację dokumentów v1 z kopią zapasową i weryfikacją odczytu, zanim przełączy się Notes/Files.
- Rozszerzać ścieżki, listowanie katalogów i uprawnienia dopiero wraz z implementacją oraz testami ich semantyki.

### 3.3. Aplikacje, IPC i Window Server

- Zdefiniować wersjonowane ABI okien, przekazywania powierzchni i zdarzeń, z kontrolą właściciela i limitami pamięci.
- Udowodnić poprawne blokowanie/wybudzanie bez utraty zdarzeń, zanim wejście zostanie przeniesione z pętli pulpitu do procesów.
- Przenieść prostą aplikację do ELF i dopiero potem kolejne aplikacje oraz przeglądarkę.
- Wywołania graficzne nie mogą udostępniać aplikacji bezpośrednich wskaźników do pamięci jądra.

### 3.4. Przeglądarka i sieć

- Przenieść przetwarzanie HTML/CSS/obrazów/JS do procesu Ring 3; błędy parserów powinny kończyć renderer, nie pulpit. Wymaga to także poprawnego VMM i walidowanego ABI — samo przeniesienie plików nie wystarczy.
- Zapewnić własność uchwytów gniazd, limity zasobów i zamykanie ich po zakończeniu procesu.
- Oddzielić asynchroniczne pobieranie od pętli UI i zachować walidację TLS.
- Dokumentować rzeczywiście obsługiwany podzbiór standardów, bez deklarowania zgodności z całym współczesnym Web.

### 3.5. Wydajność i sprzęt

- Mierzyć czas klatki i opóźnienie wejścia w zdefiniowanym środowisku; zachować rozróżnienie paint, compose i present.
- Potwierdzić brak renderowania klienta podczas samego przesuwania oraz brak aktywnego oczekiwania w stanie bezczynności.
- Rozszerzać i testować obsługę kolejnych urządzeń; podstawowe AC'97/PC Speaker i ścieżka AHCI istnieją, ale enumeracja PCI sama nie gwarantuje USB host/HID, UEFI ani zgodności z dowolnym audio/SATA.
- Uaktualniać deklaracje o FPS, pamięci i stabilności na podstawie zapisanych pomiarów, nie komunikatów startowych.
