# Plan Rozwoju i Wdrożenia PollikOS (ROADMAP)

Element jest oznaczony `[x]` tylko, gdy kod istnieje, build przechodzi, runtime został uruchomiony w QEMU i test przeszedł (dla GUI — z weryfikacją zrzutów ekranu). Pozycje `[ ]` to praca niewykonana albo niezweryfikowana.

## STAGE A: Stabilność obecnego systemu (x86-32) — w toku
- [x] **A.1. Bootloader: jądro ładowane nad 1 MiB (18.09.2026).** Stage 2 wczytuje obraz porcjami po 32 KiB do `0x10000` i kopiuje w trybie unreal pod `0x100000 + n·32 KiB`; liczbę sektorów wpisuje `build.ps1` w ostatnie słowo stage 2. Linker: obraz od `0x100000`, BSS bezpośrednio za nim, asercja `__bss_end < 0x600000`. Usunięty limit 512 KiB (przed zmianą zapas wynosił 144 B). Test: boot 64/256/2048 MiB (`tests/boot_memory.py`), jądro 525312 B > 512 KiB uruchamia się.
- [x] **A.2. Izolacja przestrzeni adresowych (18.09.2026).** Root cause: katalog procesu klonował wszystkie PDE jądra, w tym identity map RAM ≥ 128 MiB pokrywający się z `USER_SPACE_START=0x08000000`; `map_page()` zapisywało PTE użytkownika do współdzielonej tablicy jądra (nadpisanie mapowania jądra, widoczność między procesami, wyciek ramek przy `vmm_destroy_address_space`). Dowód przed poprawką: `build/isolation-before-fix-256.log`. Poprawka: `KERNEL_DIRECT_MAP_TOP = USER_SPACE_START = 1 GiB`; PMM zarządza tylko ramkami < 1 GiB (RAM powyżej raportowany jako nieużywany); katalog procesu nie dziedziczy PDE zakresu użytkownika; `map_page`/`unmap_page` odmawiają zapisu do tablic współdzielonych z jądrem i mapowań USER poza zakresem; `user_range_valid` wymaga zakresu użytkownika. Test: `vmm_isolation_self_test()` przy każdym starcie (PASS 64/256/2048 MiB), `tests/process_stress.py`.
- [x] **A.3. Własność zasobów procesów (18.09.2026).** `reap_dead_processes()` sprząta każdy proces DEAD (nie tylko poprzednika), `kill` ustawia DEAD, stos statyczny workerów starszego modelu nie jest oddawany do PMM (wcześniej `faulttest` zwalniał do PMM 2 strony BSS jądra). Guard pages rejestrowane bez duplikatów.
- [x] **A.4. Prawdziwy test cyklu życia procesu (18.09.2026).** 100 × spawn `hello` → Ring 3 → exit → reap z bilansem PMM (patrz 2.5); `tests/process_stress.py` PASS 256/2048 MiB.
- [x] **A.5. Bezpieczeństwo alokacji pamięci (30.09.2026).** `process_sbrk` sprawdza wynik `allocate_page()` i w razie braku ramek PMM wycofuje (rollback via `free_page`) przydzielone wcześniej w danym wywołaniu strony, zwracając 0 (ENOMEM). `elf_load` sprawdza wynik `map_page()`, zwalnia zaalokowaną ramkę w razie błędu tworzenia tablicy stron i przekazuje błąd do `process_spawn_elf`, który natychmiast zwalnia częściowo zmapowaną przestrzeń adresową (`vmm_destroy_address_space`).
- [x] **A.6. Własność gniazd sieciowych (30.09.2026).** Tabela `g_user_socket_owners` w `kernel/syscall.c` śledzi PID procesu otwierającego gniazdo. Funkcja `syscall_close_process_sockets(pid)` jest wywoływana automatycznie w `reap_dead_processes()` schedulera, zwalniając gniazda TCP i sloty natychmiast po zakończeniu procesu.
- [x] **A.7. Testy wycieków dla cykli open/close okien i minimize/restore (18.09.2026).** Bilans ramek PMM przetestowany automatycznie w QEMU (`tests/test_resolutions_and_perf.py`): 5 pełnych cykli otwarcia/minimalizacji do Docka/przywrócenia/zamknięcia okna wykazało zerowy ubytek pamięci (stabilny stan ramek PMM).
- [x] **A.8. Accounting czasu i zużycie CPU% (30.09.2026).** Dodano licznik `ticks_consumed` w PCB każdego procesu oraz globalny accounting ticków IRQ 0 (120 Hz) w dyspozytorze przerwań. Polecenia terminala `tasks` oraz `ps` wyświetlają kolumnę `CPU%` obliczaną w czasie rzeczywistym dla każdego aktywnego i uśpionego procesu oraz jądra.
- [x] **A.9. RAM powyżej 1 GiB (30.09.2026).** Wdrożono architekturę 64-bitową (`kernel/arch/x86_64/`) z 4-poziomowym stronicowaniem PML4 w wyższej połówce pamięci (`0xFFFF800000000000`), znoszącą 32-bitowy limit 1 GiB i mapującą całą dostępną pamięć RAM maszyny.
- [x] **A.10. Prawdziwy Desktop PollikOS (18.09.2026).**
  - [x] Rzeczywisty katalog `/home/Desktop` w PollikFS v2, integracja operacji VFS (`readdir`, `stat`, `open`, `read`, `write`, `mkdir`, `unlink`, `rename`).
  - [x] Model elementów pulpitu `DesktopItem` (`ITEM_APP`, `ITEM_DIR`, `ITEM_TXT`, `ITEM_FILE`, `ITEM_TRASH`) z siatką ikon 104×96 px, zawijaniem etykiet i zapamiętywaniem pozycji w `/home/Desktop/.layout`.
  - [x] Uruchamianie aplikacji z pulpitu (double-click) ze wspólnym dyspozytorem `app_focus_or_launch()`, animacją odbicia (bounce) ikony w Docku oraz kropką aktywności.
  - [x] Silnik animacji opartych na czasie (`ui_animation.c`) z krzywymi cubic easing (`ease_out_cubic`, `ease_in_cubic`), płynnym otwieraniem okien, minimalizacją do środka ikony Docka i przywracaniem bez repaintu aplikacji (skalowanie bufora `WindowSurface` przez kompozytor).
  - [x] Menu kontekstowe pulpitu (Nowy folder, Nowy plik tekstowy, Ustawienia) oraz menu elementu (Otwórz, Zmień nazwę / F2 z walidacją, Przenieś do Kosza / Del).
  - [x] Prawdziwy Kosz w `/home/Trash` z plikiem metadanych `/home/Trash/.trashinfo`, stanami pusty/pełny, widokiem w Files i akcjami „Przywróć” oraz „Opróżnij Kosz”.
- [x] **A.11. Poprawki i Doszlifowanie Real Desktop (18.09.2026).**
  - [x] **Naprawa tworzenia Nowego Folderu i Pliku Tekstowego:** Root cause w `build/PollikData.img` — niezgodność geometrii superbloku (`[30, 35]` zamiast `[31, 36]`) uniemożliwiała montowanie PollikFS v2 w testach ręcznych; naprawiono geometrię i zintegrowano autowalidację z autoformatowaniem w `build.ps1`.
  - [x] **Czysty start pulpitu:** Wyeliminowano automatycznie generowane atrapy plików (`Projects`, `Documents`, `todo.txt`, `test.txt`); pulpit startuje w czystym stanie z ikonami aplikacji i Koszem.
  - [x] **Niewidzialna siatka i snapping:** Wszystkie ikony (aplikacje, foldery, pliki, Kosz) przyciągają się do siatki (104×96 px), z trwałym zapisem w `/home/Desktop/.layout` i zapobieganiem nakładaniu się ikon.
  - [x] **Zaznaczanie prostokątem (Marquee Selection) i Multi-select:** Rysowanie półprzezroczystego prostokąta przy przeciąganiu po pustym pulpicie, wielokrotny wybór elementów, przełączanie zaznaczenia klawiszem Ctrl (Ctrl+Click / Ctrl+Drag).
  - [x] **Przeciąganie grupowe:** Równoczesne przesuwanie wielu zaznaczonych elementów z zachowaniem ich względnego układu na siatce i wolnym od kolizji rozmieszczaniem.
  - [x] **Bezpośrednie przeciąganie do Kosza (Drag to Trash):** Wykrywanie upuszczenia na Kosz z koralowym/czerwonym podświetleniem celu (`s_trash_hovered`), natychmiastowe przeniesienie via `trash_move_item()`, aktualizacja stanu pulpitu i czerwona plakietka na ikonie Kosza. Ochrona aplikacji systemowych przed skasowaniem.
  - [x] **Dynamiczny Dock i przypinanie (Pin/Unpin):** Konfiguracja przypiętych aplikacji w `/home/.config/dock.conf`. Menedżer `Files` jest zawsze przypięty (brak opcji unpin). Uruchomione aplikacje nieprzypięte pojawiają się dynamicznie w Docku i znikają po zamknięciu. Menu kontekstowe Docka pozwala na Pin / Unpin / Quit.
  - [x] **Ścisła hierarchia wejścia (Input Priority):** Modal dialog -> context menu -> window controls/client area -> Dock -> top bar -> desktop items -> desktop background.
  - [x] **Integracja Notatnika (Notes):** Otwieranie plików tekstowych dwuklikiem z pulpitu, edycja oraz bezpośredni zapis do VFS za pomocą skrótu Ctrl+S lub przycisku Save.
  - [x] **Weryfikacja E2E:** Automatyczny test z symulacją zdarzeń PS/2 QMP (`tests/test_real_desktop_fixes.py`) ze 100% zaliczeniem (20 zrzutów ekranu weryfikujących każdy krok oraz pełną trwałość po twardym restarcie maszyny wirtualnej).
- [x] **A.12. Bezpieczeństwo Danych Builda i Precyzyjny Cykl Życia Docka (18.09.2026).**
  - [x] **Bezwzględna ochrona danych użytkownika:** Usunięto automatyczne kasowanie i formatowanie `build/PollikData.img` z `build.ps1`.
  - [x] **Detekcja geometrii i bezpieczna migracja:** `tests/migrate_pollikfs.py` sprawdza wersję i geometrię; aktualny format `[31, 36]` pozostaje nienaruszony; starsza geometria `[30, 35]` jest bezpiecznie migrowana z automatycznym tworzeniem backupu (`PollikData.img.bak_<timestamp>`); nieobsługiwany format natychmiast zatrzymuje build z czytelnym komunikatem ostrzegawczym.
  - [x] **Jawny przełącznik formatowania:** Formatowanie dozwolone tylko przy tworzeniu nowego pustego pliku lub po jawnym podaniu przełącznika `.\build.ps1 -FormatData`.
  - [x] **Jedno źródło prawdy dla widoczności w Docku:** `visible_in_dock = pinned || running`.
  - [x] **Cykl Unpin podczas działania aplikacji:** Odpięcie działającej aplikacji przestawia `pinned = false`, lecz ikona pozostaje w Docku ze wskaźnikiem aktywności; znika dopiero po zamknięciu okna.
  - [x] **Dynamiczne przypinanie i trwałość:** Uruchomienie nieprzypiętej aplikacji dodaje ją tymczasowo do Docka; przypięcie jej z menu kontekstowego trwale zapisuje stan w `/home/.config/dock.conf`. Po zamknięciu ikona pozostaje w Docku.
  - [x] **Menedżer Files trwale przypięty:** Brak możliwości odpięcia aplikacji `Files` (`APP_FILES`).
  - [x] **Priorytet Docka w obsłudze wejścia:** Kliknięcia lewym i prawym przyciskiem myszy w obszarze Docka mają pierwszeństwo przed oknami znajdującymi się w tle.
  - [x] **Weryfikacja testowa:** Pełny test automatyczny `tests/test_dock_pin_unpin.py` oraz zestaw regresyjny `tests/test_real_desktop_fixes.py` (PASS 100%).

## PHASE 1: PMM, Paging i Pamięć Wirtualna (Fundamenty)
- [x] **1.1. Audyt pamięci i przygotowanie:**
  - [x] Sprawdzenie layoutu pamięci, linker scriptu, bootloadera i sterowników (VBE, RTL8139).
  - [x] Utworzenie dokumentacji `ARCHITECTURE.md` i `ROADMAP.md`.
- [x] **1.2. BIOS E820 Memory Map & Walidacja:**
  - [x] Wdrożenie odpytywania BIOS E820 w `boot/stage2.asm` (INT 15h, AX=E820h).
  - [x] Walidacja sygnatury `SMAP`, obsługa wpisów 20 i 24-bajtowych (ACPI 3.0 ext attribute bit 0).
  - [x] Obsługa regionów >4 GiB bez overflow, ignorowanie nieprawidłowych wpisów i przekazywanie liczby poprawnych wpisów do kernela.
- [x] **1.3. Physical Memory Manager (PMM) & Kompletne Rezerwacje:**
  - [x] Implementacja bitmapy ramek fizycznych 4 KiB.
  - [x] Oznaczanie jako zajęte WSZYSTKICH struktur: 1 MiB (IVT, BDA, Stage 1/2, E820, VBE), kernel `.text`, `.rodata`, `.data`, `.bss`, stos jądra, heap, framebuffer/compositor buffers, RTL8139 DMA, MMIO, bitmapa PMM, page directory, page tables, stosy procesów.
  - [x] Diagnostyka PMM: total physical RAM, usable RAM, reserved RAM, free pages, used pages.
  - [x] Test PMM: zapis liczby wolnych stron, alokacja 128 stron, sprawdzenie braku duplikatów, zapis i weryfikacja wzorca, zwolnienie wszystkich stron, sprawdzenie powrotu licznika wolnych stron.
- [x] **1.4. Paging, VMM & Ochrona Rejestrów:**
  - [x] Inicjalizacja bazowego katalogu stron (Page Directory) i tablic stron (Page Tables).
  - [x] Tożsamościowe mapowanie strefy jądra (supervisor) z zachowaniem kompatybilności DMA RTL8139 i buforów grafiki.
  - [x] Mapowanie sprzętowego bufora ramki VBE LFB.
  - [x] Włączenie stronicowania x86 (załadunek CR3, ustawienie bitu CR0.PG).
  - [x] Ustawienie bitu **CR0.WP** (Write Protect w Ring 0) w celu wymuszenia ochrony stron read-only przed przypadkowym zapisem z kernela.
  - [x] Dokumentacja ograniczeń uprawnień stron w klasycznym 32-bit paging bez PAE/NX (RO, RW, US).
  - [x] Test VMM: alokacja strony fizycznej, mapowanie pod testowy adres wirtualny, zapis `0x12345678`, weryfikacja odczytu, `unmap_page()` i sprawdzenie `get_mapping()`.
- [x] **1.5. Guard Pages & Walidacja Wskaźników Userspace:**
  - [x] Dodanie unmapped guard page pod stosami procesów (kernel stack i user stack) w celu wychwytywania stack overflow.
  - [x] Centralne API walidacji wskaźników z userspace: `copy_from_user()`, `copy_to_user()`, `user_range_valid()` (sprawdzanie granic userspace, overflow `ptr + size`, statusu zmapowania, bitów USER i WRITE).
- [x] **1.6. System Debugowania i Logowania (KLOG & Panic):**
  - [x] Minimalny moduł `klog`: kategorie `BOOT`, `MEM`, `PMM`, `VMM`, `PF`, `PROC`.
  - [x] Poziomy i makra logowania: `KLOG_DEBUG`, `KLOG_INFO`, `KLOG_WARN`, `KLOG_ERROR`.
  - [x] Funkcje diagnostyczne: `dump_registers()` oraz `panic()`.
  - [x] Pełna diagnostyka Page Fault w ISR 14: dekodowanie Present, Write, User, Reserved, Instruction-Fetch; wypisywanie CR2, EIP, ESP, EBP, CS, SS, EFLAGS, CR0, CR3, CR4, PID, nazwy procesu i powodu.
  - [x] Kontrolowany test Page Fault (dostęp do niezamapowanej strony w Ring 0 / test izolacji w Ring 3).
- [x] **1.7. Weryfikacja Całościowa Phase 1:**
  - [x] Czysta kompilacja bez ostrzeżeń (`-Wall -Wextra -Werror`).
  - [x] Uruchomienie w QEMU, weryfikacja logu z seriala, stabilność VBE, RTL8139, DHCP, GUI i testów.

---

## PHASE 2: Przestrzeń Procesów, ELF Loader i Syscall ABI
- [x] **2.1. Niezależna Wirtualna Przestrzeń Procesu:**
  - [x] Usunięcie limitów segmentacji 64 KiB i sztywnego mapowania pod `0x800000`.
  - [x] Tworzenie odrębnego Page Directory dla każdego procesu (sklonowane wpisy PDE jądra **poza zakresem użytkownika** ze zdjętym bitem `PAGE_USER` - kernel supervisor-only, fizyczne ramki współdzielone bez duplikacji; PDE zakresu użytkownika są prywatne — patrz STAGE A poniżej).
  - [x] Niezmapowana strona zerowa (`0x00000000`) - NULL pointer dereference natychmiast wywołuje Page Fault.
  - [x] Zdefiniowane stałe architektury pamięci w `kernel/vmm.h`: `KERNEL_DIRECT_MAP_TOP` = `USER_SPACE_START` (`0x40000000`), `USER_SPACE_END` (`0xC0000000`), `USER_STACK_TOP` (`0xC0000000`), `USER_STACK_SIZE` (`16 * 1024`), `USER_STACK_BOTTOM` (`0xBFFFC000`), `USER_GUARD_PAGE` (`0xBFFFB000`). Programy `apps/user.ld` linkowane od `0x40001000`.
  - [x] Przełączanie rejestru CR3 przy context switchu w schedulerze (tylko w razie zmiany address space).
  - [x] Aktualizacja `TSS.ESP0` na dedykowany stos kernela danego procesu przy każdym przełączeniu (`TSS.ESP0 = current_process->kernel_stack_top`).
- [x] **2.2. ELF32 Executable Loader:**
  - [x] Struktury nagłówków ELF32 (`Elf32_Ehdr`, `Elf32_Phdr`).
  - [x] Walidacja nagłówków: ELF magic, `ELFCLASS32`, little-endian, `EV_CURRENT`, `ET_EXEC`, `EM_386`, walidacja `e_ehsize`, `e_phentsize`, `e_phnum`, `e_phoff`.
  - [x] Ładowanie segmentów `PT_LOAD`: walidacja `p_filesz <= p_memsz`, `p_offset + p_filesz <= file_size`, brak integer overflow, segmenty wyłącznie w granicach userspace.
  - [x] Uprawnienia stron: segmenty bez `PF_W` mapowane jako user read-only, z `PF_W` jako user writable. Udokumentowano: standardowe 32-bit x86 paging bez PAE/NX nie udostępnia bitu NX.
  - [x] Alokacja fizycznych ramek przez PMM, kopiowanie `p_filesz`, zerowanie BSS (`p_memsz - p_filesz`), czyszczenie w przypadku błędu bez wycieków pamięci.
- [x] **2.3. User Stack & Entry do Ring 3:**
  - [x] Prawdziwy userspace stack: alokacja stron pod `USER_STACK_TOP` (`0xC0000000`), niezmapowana strona ochronna (guard page) pod `0xBFFFB000`.
  - [x] Konstrukcja ramki dla `iret`: selektory User Data (SS, DS, ES, FS, GS), User Code (CS), User ESP, User EIP z nagłówka ELF, EFLAGS z bitem IF (włączone przerwania).
  - [x] Logowanie diagnostyczne: `[PROC] starting pid`, `[PROC] entry`, `[PROC] user esp`, `[PROC] cr3`.
- [x] **2.4. Czyste Syscall ABI (INT 0x80) & Kody Błędów:**
  - [x] Jednolita konwencja ABI: EAX = nr syscalla, EBX = arg1, ECX = arg2, EDX = arg3, ESI = arg4, EDI = arg5, EAX = wynik.
  - [x] Standardowe kody błędów errno (w `kernel/syscall.h`): `EINVAL`, `EFAULT`, `ENOMEM`, `ENOENT`, `EBADF`, `EPERM`, `ENOSYS`, `EIO`.
  - [x] Zaimplementowane i zweryfikowane wywołania systemowe: `SYS_EXIT`, `SYS_WRITE`, `SYS_READ`, `SYS_GETPID`, `SYS_YIELD`, `SYS_ALLOC` (sbrk), `SYS_TIME`, `SYS_SLEEP`, `SYS_OPEN`, `SYS_CLOSE`, `SYS_SEEK`, `SYS_SPAWN`.
  - [x] Bezpieczna walidacja wszystkich wskaźników userspace za pomocą `copy_from_user()`, `copy_to_user()`, `user_range_valid()` oraz `copy_string_from_user()`.
- [x] **2.5. Userspace Runtime (SDK) & Aplikacje Testowe:**
  - [x] Userspace SDK: `include/pollikos.h`, `crt0.asm` (`_start` -> `main` -> `SYS_EXIT`), linker script `apps/user.ld`.
  - [x] Aplikacja `hello` (`apps/hello/hello.c`): `SYS_GETPID`, `SYS_WRITE("Hello from Ring 3 ELF!\n")`, `SYS_YIELD`, `SYS_EXIT(42)`.
  - [x] Aplikacja `fault_test` (`apps/fault_test/fault_test.c`): próba zapisu pod NULL (`0x00000000`), kontrolowany Page Fault `USER WRITE NOT_PRESENT`, natychmiastowe zabicie procesu bez awarii kernela.
  - [x] Aplikacja `fault_kernel` (`apps/fault_kernel/fault_kernel.c`): próba zapisu pod adres jądra (`0x00100000`), kontrolowany Page Fault `USER WRITE PROTECTION_VIOLATION`, natychmiastowe zabicie procesu, jądro nienaruszone.
  - [x] Aplikacja `fault_stack` (`apps/fault_stack/fault_stack.c`): przekroczenie stosu / dotknięcie guard page (`0xBFFFB800`), wykrycie naruszenia guard page `[STACK_OVERFLOW_GUARD_PAGE]`, zabicie procesu.
  - [x] Test zwalniania zasobów (PMM leak test): 100 rzeczywistych cykli `spawn(hello ELF)` → Ring 3 → `exit(42)` → reap; bilans wolnych stron PMM identyczny przed i po (`[TEST] PMM no leak`). Do 18.09.2026 ten punkt opisywał jedynie pętlę tworzenia/usuwania katalogów stron; zastąpiono ją prawdziwym cyklem procesów (`tests/process_stress.py`, PASS 256 MiB i 2048 MiB).
  - [x] Stabilność całego systemu: po serii testów i awarii procesów desktop GUI, obsługa myszy PS/2, terminal, sieć RTL8139 (ARP/ICMP ping) i scheduler działają (`[TEST] PHASE 2 PASS`, `tests/smoke.py`).

---

## PHASE 3: Virtual File System (VFS) i PollikFS v2
- [x] **3.1. Abstrakcja VFS:**
  - [x] Ujednolicony interfejs: `vfs_open`, `vfs_close`, `vfs_read`, `vfs_write`, `vfs_seek`, `vfs_stat`, `vfs_mkdir`, `vfs_unlink`.
  - [x] Tabela deskryptorów plików per proces (0: stdin, 1: stdout, 2: stderr, 3+: otwarte pliki).
  - [x] Integracja z syscallami: `SYS_OPEN`, `SYS_CLOSE`, `SYS_READ`, `SYS_WRITE`, `SYS_SEEK`, `SYS_SPAWN`.
- [x] **3.2. Projekt i Implementacja PollikFS v2:**
  - [x] Wsparcie dla katalogów i podkatalogów (`/`, `/bin`, `/home`, `/dev`, `/etc`).
  - [x] Dynamiczna alokacja bloków 1024-bajtowych z bitmapą wolnych bloków.
  - [x] Obsługa inodów z blokami bezpośrednimi i blokiem pośrednim.
  - [x] Długie nazwy plików (do 56 znaków na wpis katalogu).
  - [x] Znaczniki czasu (created, modified) oraz typy plików (VFS_FILE, VFS_DIR).
  - [x] Bezpieczny zapis z synchronizacją bufora dysku (`ata_flush`).
  - [x] Automatyczna instalacja `/bin/hello` i `/home/notes.txt` p  - [x] Weryfikacja operacji na plikach: tworzenie, zapis, modyfikacja, odczyt, usuwanie i trwałość po restarcie (`tests/smoke.py`).

---

## PHASE 4: Desktop & GUI Performance, Window Manager i Compositor
- [x] **4.1. Architektura Window Managera (`kernel/wm.h`, `kernel/wm.c`):**
  - [x] Wydzielenie logiki okien z `kernel.c` do dedykowanego modułu Window Managera (`kernel/wm.h`, `kernel/wm.c`).
  - [x] Struktura okna: `id`, `owner_pid`, `x`, `y`, `width`, `height`, `state` (`NORMAL`, `MINIMIZED`, `MAXIMIZED`), `focused`, `visible`, `dirty`, `min_w`, `min_h`, `max_w`, `max_h`, `restore_x`, `restore_y`, `restore_w`, `restore_h`.
  - [x] Stany okien: `focused_window`, `hovered_window`, `dragged_window`, `resized_window`, `z_order`.
  - [x] Obsługa podwójnego kliknięcia w titlebar (maximize / restore) oraz przeciągania zmaksymalizowanego okna (przywrócenie geometrii i płynny drag).
- [x] **4.2. Active / Inactive Windows:**
  - [x] Aktywne okno: wyraziste kontrolki (czerwony close `X`, wyraźny minimize `-`, maximize `[]`), kontrastowy tytuł i jasny titlebar (`0xf1eff5`).
  - [x] Nieaktywne okno: neutralne/szare przyciski (`0xcdc9d6` / `0xd8d4e0`) zachowujące swój kształt, przygaszony titlebar (`0xeae7f0`), zmniejszony kontrast tytułu (`0x888294`).
  - [x] Przełączanie focusu: kliknięcie nieaktywnego okna wynosi je na wierzch Z-order i przerysowuje wyłącznie obszar titlebara / dirty region bez kosztownej animacji całego okna (`wm_focus()`).
  - [x] Kliknięcie w pulpit odznacza aktywne okno (`wm_unfocus()`).
- [x] **4.3. Zaawansowane Dirty Rectangles i Zero Pełnego Redraw:**
  - [x] Mechanizm `wm_invalidate_rect(x, y, w, h)` łączący nakładające się i przyległe prostokąty w buforze 16 prostokątów.
  - [x] Kopiowanie do framebuffera (`framebuffer_present`) wyłącznie zmienionych regionów.
  - [x] Heurystyka: pełny redraw (`full-frame present`) wyłącznie w przypadku inwalidacji pokrywającej większość ekranu (>60%).
  - [x] Optymalizacja kursora myszy jako osobnej warstwy compositora (inwalidacja tylko 32x36 starej i nowej pozycji, brak odrysowywania sceny przy ruchu kursora, zweryfikowano testem: `0 < changed < 2304` pikseli).
- [x] **4.4. Czasowo Sterowane Animacje z Easingiem:**
  - [x] Centralny silnik animacji oparty na czasie (`wm_time_ms()`, czas trwania, postęp 0.0 - 1.0).
  - [x] Funkcje easing: `ease_out_cubic`, `ease_in_out_cubic` z precyzyjną arytmetyką stałoprzecinkową.
  - [x] Płynne, lekkie animacje otwierania, minimalizacji, przywracania oraz powiększania ikon docka.
- [x] **4.5. Płynny Dragging & Resizing:**
  - [x] Rozdzielenie geometrii okna, layoutu klienta, malowania i prezentacji.
  - [x] Inwalidacja tylko poprzedniego i nowego obrysu okna (`paint(2)`) podczas przesuwania i zmiany rozmiaru z 8 krawędzi.
  - [x] Brak I/O dyskowego, parsowania czy layoutu całego pulpitu w trakcie ruchu myszy.
- [x] **4.6. Target 60/120 FPS & Event-Driven Rendering:**
  - [x] Pętla renderowania sterowana zdarzeniami: gdy brak zmian i animacji — usypianie procesora instrukcją `sti; hlt` (redukcja 100% CPU busy loop do 0% w idle).
  - [x] Gdy aktywna animacja lub interakcja — renderowanie z docelową płynnością 60–120 FPS.
- [x] **4.7. Profiler Wydajności GUI:**
  - [x] Monotoniczny licznik mikrosekund/milisekund (`wm_time_ms()`, `wm_time_us()`, `rdtsc_low()`).
  - [x] Mierzenie czasów: frame, layout, paint, present, dirty area, liczba wywołań present.
  - [x] Polecenie w powłoce terminala: `perf` oraz `perf gui` z podsumowaniem FPS, worst frame time i zużycia CPU.

- [x] **4.8. Płynny Snap Maximize Preview i Czysty Desktop:**
  - [x] Eliminacja zacinania przy podglądzie powiększenia (snap preview): kompozytor nie wymusza `full = 1` podczas przeciągania okna z aktywnym podglądem, stosując dirty-rect (`full = 2`) i zapewniając stabilne 120 FPS.
  - [x] Prekomputacja składowych koloru w `rounded()` (`blend_precomputed`) optymalizuje wewnętrzną pętlę blendowania półprzezroczystych zaokrąglonych prostokątów.
  - [x] Uproszczenie górnego paska (`desktop_draw_bar`): wycentrowana nazwa Pollik OS bez zbędnych kontrolek telemetrycznych.
  - [x] Ograniczenie menu kontekstowego do okien (brak niepożądanych menu przy kliknięciu prawym przyciskiem myszy na dock i pulpit).
  - [x] Wyłączenie wyskakujących toastów powiadomień w prawym górnym rogu ekranu.
  - [x] Przeniesienie informacji o systemie (`OS_LABEL`, architektura) do aplikacji Ustawienia (Settings) i uproszczenie okna powitalnego Welcome.

---

## PHASE 5: Scheduler v2, Blokowanie i Kolejki Zdarzeń (IPC)
- [x] **5.1. Scheduler v2 i Stany Procesów:**
  - [x] Pełne stany procesu w `kernel/process.h` i `kernel/process.c`: `PROC_STATE_RUNNING`, `PROC_STATE_READY`, `PROC_STATE_BLOCKED`, `PROC_STATE_SLEEPING`, `PROC_STATE_DEAD`.
  - [x] Prawdziwe `idle_task` (PID 0) wykonujące `sti; hlt` w stanie bezczynności systemu.
  - [x] Funkcje kontroli stanu: `process_sleep_current()`, `process_block_current()`, `process_unblock()`, `process_yield()`.
  - [x] Pomiary czasu CPU i statystyki przełączeń kontekstu na proces (`reports`, `switches`).
- [x] **5.2. Kolejki Zdarzeń per Proces (Event Queues):**
  - [x] Typy zdarzeń: `EVENT_KEY_DOWN`, `EVENT_KEY_UP`, `EVENT_MOUSE_MOVE`, `EVENT_MOUSE_DOWN`, `EVENT_MOUSE_UP`, `EVENT_MOUSE_WHEEL`, `EVENT_WINDOW_FOCUS`, `EVENT_WINDOW_BLUR`, `EVENT_WINDOW_MOVE`, `EVENT_WINDOW_RESIZE`, `EVENT_WINDOW_CLOSE`, `EVENT_TIMER`, `EVENT_IPC_MESSAGE`, `EVENT_NETWORK`.
  - [x] API zdarzeń w Ring 0 i Ring 3: `poll_event()`, `wait_event()`, `SYS_POLL_EVENT` (164), `SYS_WAIT_EVENT` (163) z bezpiecznym wybudzaniem z `PROC_STATE_BLOCKED`.
- [x] **5.3. Komunikacja Międzyprocesowa (IPC):**
  - [x] Wiadomości `IpcMessage` typu `PID -> PID` z buforem kołowym na proces (`process_send_ipc`, `process_recv_ipc`).
  - [x] Wywołania systemowe `SYS_SEND_IPC` (165) i `SYS_RECV_IPC` (166) z walidacją wskaźników pamięci (`copy_from_user`, `copy_to_user`, `user_range_valid`).
  - [x] Zweryfikowano z poziomu Ring 3 ELF (`apps/hello/hello.c`): `[TEST] IPC PASS`, `[TEST] EVENT_QUEUE PASS`.

---

## PHASE 6: Window Server, SDK i Migracja Aplikacji do Ring 3
- [x] **6.1. Architektura Window Server & SDK:**
  - [x] Userspace SDK w `include/pollikos.h`:
    - `pollikos_wait_event(SystemEvent *ev)`
    - `pollikos_poll_event(SystemEvent *ev)`
    - `pollikos_send_ipc(int dest_pid, const void *data, u32 len)`
    - `pollikos_recv_ipc(int *out_src_pid, void *out_buf, u32 max_len)`
    - `pollikos_socket(int domain, int type, int proto)`
    - `pollikos_connect(int sock, const u8 *ip, u16 port)`
    - `pollikos_send(int sock, const void *buf, u32 len)`
    - `pollikos_recv(int sock, void *buf, u32 max_len)`
  - [x] Primitives rysowania w oknie (`sys_draw_rect_clipped`, `sys_draw_rounded_clipped`, `sys_draw_letter_clipped`).
- [x] **6.2. Rozbudowa Powłoki Terminala i Narzędzi:**
  - [x] **Terminal Shell (GUI i386):** `help`, `about`, `version`, `pwd`, `cd`, `ls`/`dir`, `cat`/`type`, `stat`, `mkdir`, `touch`, `rm`/`del`, `rmdir`, `mv`/`rename`, `cp`, `history`, `echo`, `which`, `clear`, `ps`/`tasks`, `pause`, `resume`, `kill`, `spawn`, `uptime`, `free`/`mem`, `lspci`, `beep`, `ping`, `net`, `publicip`, `perf`, `anim`, `theme`, `time`, `date`, `reboot`, `shutdown`.
  - [x] **TinyCC:** GUI i386 reports native compiler help; compiling and running `/bin/tcc` remains available in the separate x86_64 console only.
  - [x] **Files & Notes:** odczyt i zapis plików na PollikFS v2, autosave, nawigacja.
  - [x] **Settings & Info:** palety motywów, status sieci i sprzętu.
- [x] **6.3. Schowek Systemowy (Clipboard):**
  - [x] Systemowy schowek tekstowy z obsługą globalnych skrótów `Ctrl+C` i `Ctrl+V` współdzielony między Terminalem a Notes.

---

## PHASE 7: Gniazda Sieciowe (Socket API) i Userspace Networking
- [x] **7.1. Userspace Sockets API:**
  - [x] Wywołania systemowe w `kernel/syscall.h` i `kernel/syscall.c`:
    - `SYS_SOCKET` (167): alokacja deskryptora gniazda (100..107).
    - `SYS_CONNECT` (168): łączenie z adresem IP i portem zdalnym via `tcp_connect()`.
    - `SYS_SEND` (169): wysyłanie strumienia danych TCP via `tcp_send()` z bezpiecznym buforowaniem.
    - `SYS_RECV` (170): odczyt strumienia danych TCP via `tcp_read()`.
    - `SYS_CLOSE` (6): czyste zamykanie gniazda i zwolnienie slotu.
  - [x] Integracja z `include/pollikos.h` dla aplikacji Ring 3.

---

## PHASE 8: Obsługa Urządzeń (RTC, ACPI/Power, PC Speaker, PCI)
- [x] **8.1. Zegar CMOS RTC:**
  - [x] Sterownik `kernel/hw.c` i `kernel/hw.h`: odczyt portów 0x70/0x71, dekodowanie BCD, obsługa formatu 12/24h.
  - [x] Narzędzia `time` i `date` w powłoce terminala.
- [x] **8.2. Zarządzanie Zasilaniem (ACPI & Power):**
  - [x] Programowe wyłączanie systemu (`power_shutdown()`): porty ACPI/QEMU (0x604, 0xB004, 0x4004, 0x600).
  - [x] Programowy restart (`power_reboot()`): impuls kontrolera klawiatury 8042 (port 0x64, 0xFE) oraz fallback triple-fault.
  - [x] Polecenia powłoki: `shutdown` oraz `reboot`.
- [x] **8.3. Podsystem Dźwięku (Audio AC'97 & PC Speaker):**
  - [x] Sterownik głośnika systemowego: programowanie PIT kanał 2 (porty 0x43, 0x42) i bramki portu 0x61 (`speaker_beep`).
  - [x] Sterownik Intel ICH AC'97 Audio (`kernel/audio.c`, `kernel/audio.h`): wykrywanie kontrolera PCI (0x8086:0x2415 lub klasa 0x04/0x01), alokacja fizycznych buforów DMA (Buffer Descriptor List BDL), obsługa 48 kHz PCM stereo/mono, regulacja głośności miksera (Master i PCM Out).
  - [x] Zestaw efektów dźwiękowych (`SOUND_STARTUP`, `SOUND_CLICK`, `SOUND_ALERT`, `SOUND_TRASH`) z automatycznym odtwarzaniem akordu startowego po starcie pulpitu i fallbackiem na PC Speaker.
  - [x] Polecenia powłoki: `beep` oraz `sound [status|test|startup|alert|click|trash]`.
- [x] **8.4. Skaner Magistrali PCI:**
  - [x] Odczyt rejestrów PCI Configuration Space (porty 0xCF8 / 0xCFC).
  - [x] Wykrywanie kontrolerów: VGA (0x03), IDE/SATA (0x01), Ethernet RTL8139 (0x02), Audio AC'97 (0x04), USB (0x0C).
  - [x] Polecenie powłoki: `lspci`.

---

## PHASE 9: Niezawodność Systemu Plików, Raportowanie Błędów i Testy Końcowe
- [x] **9.1. Odporność PollikFS v2:**
  - [x] Trwałość danych zweryfikowana po restartach i zapisie w `tests/smoke.py`.
- [x] **9.2. Zaawansowane Raportowanie Awarii:**
  - [x] Kernel Panic: zrzut rejestrów EIP, ESP, EBP, CR0..CR4, CS, DS, SS, PID, ślad stosu.
  - [x] Izolacja procesów Ring 3: kontrolowane Page Faulty (`fault_test.elf`, `fault_kernel.elf`, `fault_stack.elf`), jądro i desktop działają nieprzerwanie.
- [x] **9.3. Benchmark i Końcowe Testy Stabilności:**
  - [x] Test szczelności pamięci PMM: patrz 2.5 (100 rzeczywistych cykli spawn/exit procesu ELF, `tests/process_stress.py`).
  - [x] `tests/smoke.py` w QEMU: PASS (ostatnio 18.09.2026 na jądrze 525312 B).
  - [x] Rozmiar kernela: **525312 bajtów**; dawny limit 512 KiB bootloadera został usunięty (stage 2 ładuje obraz pod 1 MiB, limit 4 MiB w `build.ps1`). Zapis „476,188 bajtów / margines 48 KiB” był nieaktualny.
  - [x] Brak narastających wycieków w cyklach open/close i minimize/restore: `tests/test_resolutions_and_perf.py` wykonuje 5 pełnych cykli otwarcie → minimalizacja → przywrócenie → zamknięcie okna Terminala i mierzy bilans PMM przez QMP `pmemsave`; wynik: 252502 → 252512 wolnych stron (różnica ujemna = brak wycieku), PASS.
  - [x] Kategoryzowane logi systemowe (`BOOT`, `MEM`, `PMM`, `VMM`, `PF`, `PROC`, `FS`, `NET`, `GUI`, `SYS`).
- [x] **9.4. DPI Scaling & Graphic Subsystem:**
  - [x] VBE 1024x768x24 bpp / 32 bpp buforowanie z zachowaniem skalowania czcionek (1-5x scale table), zaokrągleń antyaliasing i elastycznego silnika layoutu.
