# Pollik OS v0.1 Alpha

Eksperymentalny cel x86_64: [stan self-hostingu i instrukcja testów](SELF_HOSTING.md).
Domyślny pulpit nadal działa w trybie i386; nowy cel uruchamia statyczne ELF64 z plik�w PollikFS v2 przez VFS w Ring 3 z wyw�aszczaniem przez timer, planowaniem proces�w, argc/argv/envp oraz odczytem plik�w przez prywatne deskryptory procesu i 64-bitowym PMM/VMM (testowany z 5 GiB RAM).

Własny eksperymentalny system w C i asemblerze: bootloader BIOS, kernel x86, pulpit inspirowany macOS, trwałe pliki, procesy użytkownika i podstawowa sieć. Bez Linuksa, Windows, GRUB-a i standardowej biblioteki C. Narzędzia kompilacji to NASM i LLVM; emulator to QEMU.

Nazwa wydania to **Pollik OS v0.1 Alpha**. Wcześniejsze numery 0.2/0.3 oznaczały lokalne prototypy. Nazwę i numer wersji interfejsu definiuje teraz wspólnie `kernel/system.h`.

## Uruchomienie

Okna mają narożniki o promieniu 20 px z wygładzaniem 8×8 próbek na piksel krawędzi. Przeciąganie prawej/dolnej krawędzi lub prawego dolnego rogu zmienia rozmiar w obu kierunkach, do minimum 680×410. Kursor wskazuje aktywną krawędź. Maksymalizacja i przywracanie zachowują rozmiar ustawiony ręcznie; minimalizowanie nadal działa żółtym przyciskiem. Skrypty PowerShell są zapisane jako UTF-8 z BOM, aby poprawnie działały także w Windows PowerShell 5.1.

**Najprościej: otwórz folder projektu i kliknij dwukrotnie `Start-PollikOS.cmd`.** System otworzy się w osobnym oknie QEMU. Windows nadal działa; nie trzeba restartować komputera ani instalować systemu na jego dysku. Zamknij poprzednie okno QEMU przed ponownym uruchomieniem PollikOS.

Gotowy obraz jest już zbudowany. Dwuklik uruchamia `run.ps1`; ustawienie ExecutionPolicy Bypass dotyczy wyłącznie tego procesu PowerShell i nie zmienia globalnej polityki Windows. Po zmianach w kodzie przebuduj obraz poniższym poleceniem.

W PowerShell w katalogu projektu:

```powershell
.\build.ps1
.\run.ps1
```

Launcher automatycznie dobiera rozdzielczość do monitora, na którym znajduje się kursor podczas uruchomienia. Możesz podać ją ręcznie: `.\run.ps1 -Resolution 1920x1080` albo uruchomić pełny ekran: `.\run.ps1 -Fullscreen`. Zakres wynosi 1024×768–3440×1440 (szerokość podzielna przez 8). Obsługa QEMU Standard VGA z 32 MiB VRAM ustawia rzeczywisty framebuffer; bez konfiguracji pozostaje tryb BIOS 1024×768. To dobór przy starcie, bez automatycznego przełączania podczas przenoszenia okna pomiędzy monitorami. Przekątna 24/27/34 cali nie wyznacza rozdzielczości; monitory 4K korzystają z ograniczonego trybu do 3440×1440.

Zielony przycisk i F11 przełączają maksymalizację/przywrócenie. Przeciągnięcie okna do górnej krawędzi ekranu wywołuje płynny, półprzezroczysty podgląd powiększenia (snap maximize preview) z zaokrąglonymi rogami, działający bez zacinania w 120 FPS. Górny pasek pulpitu prezentuje minimalistyczną, wycentrowaną nazwę systemu **Pollik OS**. Przeglądarka przelicza układ strony do nowej szerokości. Pozostałe aplikacje zachowują dotychczasowy układ kontrolek w większym oknie. Prawy przycisk myszy otwiera zaawansowane menu kontekstowe: w oknach (Files, edytory), na pulpicie (tworzenie nowych folderów i plików tekstowych), na elementach pulpitu (otwieranie, zmiana nazwy, usuwanie) oraz na Docku (otwieranie, przypinanie/odpinanie aplikacji, zamykanie). Informacje o systemie i wersji znajdują się w Ustawieniach (Settings). Nad linkami, polami tekstowymi i menu kursor zmienia kształt. Obsługiwane jest zaznaczanie wielu obiektów półprzezroczystym prostokątem (Marquee Selection) oraz przeciąganie grupowe.

YouTube: transport HTTPS i pobranie HTML działają, ale aplikacja YouTube wymaga nieobsługiwanych funkcji JavaScript i Web API. Filmy nie są odtwarzane. Błędy interpretera pokazują teraz informację o niepełnej obsłudze JS na pasku stanu, zamiast sugerować, że strona jest w pełni gotowa.

Wymagane w PATH: `nasm`, `clang`, `ld.lld`, `llvm-objcopy`, `qemu-system-x86_64`. Są już dostępne na tym komputerze. Ctrl+Alt zwalnia mysz przechwyconą przez QEMU.

- `build/PollikOS-Alpha.img` — rzadki obraz rozruchowy BIOS, 10 GiB pojemności logicznej. Kompilacja go odtwarza.
- `build/PollikData.img` — osobny dysk dokumentów (10 GiB sparse). Kompilacja **bezwzględnie chroni istniejące dane użytkownika**: wykrywa geometrię PollikFS v2, automatycznie migruje starsze geometrie (z kopią zapasową `PollikData.img.bak_<timestamp>`) i odmawia formatowania bez jawnego parametru `.\build.ps1 -FormatData`.

Skrypt uruchamia dyski jako primary IDE master/slave, VGA standard i RTL8139 w sieci użytkownika QEMU oraz przydziela 2 GiB RAM. Jądro nadal korzysta ze stałej mapy pamięci i nie udostępnia całej tej pamięci dynamicznemu alokatorowi. BIOS musi obsługiwać VBE 1024×768. Obrazy sparse zajmują na NTFS tylko zapisane zakresy.

Przeglądarka pokazuje stan ładowania przed żądaniem i automatycznie odmalowuje wynik po jego zakończeniu. Pierwsze żądanie oczekuje na DHCP. Strona startowa oraz frazy wpisane przez Ctrl+L korzystają z wyników DuckDuckGo Lite; PollikOS nie ma własnego indeksu całego Internetu. Pobieranie pozostaje synchroniczne, więc podczas żądania obsługa wejścia czeka na jego zakończenie.

## Pulpit i system plików

Pulpit PollikOS to pełnoprawne, dynamiczne środowisko pracy połączone bezpośrednio z systemem plików **PollikFS v2** i warstwą **VFS**:
- **Prawdziwy katalog pulpitu i czysty start:** Pulpit reprezentuje zawartość katalogu `/home/Desktop`. Każdy folder, plik tekstowy czy aplikacja to rzeczywisty obiekt VFS, a nie sztuczna makieta. Pulpit startuje w czystym stanie bez plików-zaślepek, eksponując jedynie skróty aplikacji i Kosz.
- **Niewidzialna siatka i snapping:** Ikony ułożone są w dynamicznej, niewidzialnej siatce (104×96 px), zapobiegającej nakładaniu się elementów. Pozycje ikon są trwale zapamiętywane w pliku konfiguracji siatki `/home/Desktop/.layout` i zachowywane po restarcie. Użytkownik może przeciągać ikony myszą (drag & drop) między komórkami siatki, upuszczać pliki do folderów oraz bezpośrednio do Kosza.
- **Zaznaczanie prostokątem (Marquee Selection) i Multi-select:** Przeciągnięcie kursora myszy po wolnej przestrzeni pulpitu wyświetla półprzezroczysty fioletowy prostokąt z obwódką, zaznaczający wszystkie przecinane elementy. Obsługiwany jest również klawisz Ctrl (Ctrl+Click / Ctrl+Drag) do przełączania i dodawania do zaznaczenia oraz grupowe przeciąganie zaznaczonych elementów z zachowaniem ich względnego układu.
- **Wspólny model DesktopItem:** Obsługuje aplikacje systemowe (`Files`, `Terminal`, `Notes`, `Settings`, `Web`, `PollikMark`), katalogi, pliki tekstowe `.txt`, pliki ogólne oraz Kosz. Etykiety pod ikonami używają czcionki Pollik Sans z dwuwierszowym zawijaniem tekstu i cieniem dla pełnej czytelności na każdym tle.
- **Menu kontekstowe i akcje:** Prawy przycisk myszy na wolnym pulpicie otwiera menu kontekstowe z możliwością utworzenia nowego folderu (`New Folder`, `New Folder 2`...) oraz nowego pliku tekstowego (`New Text File.txt`...). Prawy przycisk na elemencie umożliwia jego otwarcie, zmianę nazwy (`Rename` / klawisz `F2` z walidacją) oraz przeniesienie do Kosza (`Move to Trash` / klawisz `Del`).
- **Prawdziwy Kosz (Trash) i Drag to Trash:** Obiekty można przeciągnąć bezpośrednio na ikonę Kosza — po najechaniu Kosz podświetla się koralowym/czerwonym kolorem jako cel upuszczenia, a po zwolnieniu przycisku element jest przenoszony do `/home/Trash` wraz z metadanymi w `/home/Trash/.trashinfo`. Ikona Kosza otrzymuje czerwoną plakietkę wskazującą stan pełny. Aplikacje systemowe są chronione przed usunięciem. Dwuklik na Koszu otwiera menedżer plików Files z możliwością przywrócenia pliku do pierwotnej lokalizacji lub trwałego opróżnienia.
- **Płynne animacje oparte na czasie (Time-based Cubic Easing):** Animacje otwierania okien (scale/fade ~200 ms), minimalizacji do środka ikony Docka (translation/scale ~220 ms) oraz przywracania z Docka korzystają ze wspólnych krzywych `ease_out_cubic` i `ease_in_cubic`. Kompozytor transformuje zapamiętany bufor `WindowSurface` — aplikacje nie renderują zawartości w każdej klatce animacji (zero client repaints).
- **Dynamiczny Dock i cykl życia aplikacji (Pin/Unpin):** Widoczność w Docku wynika z formuły `visible_in_dock = pinned || running`. Przypięte aplikacje są zawsze widoczne. Aplikacje nieprzypięte pojawiają się dynamicznie w Docku ze wskaźnikiem aktywności tylko w trakcie działania. Odpięcie działającej aplikacji (*Unpin from Dock*) nie usuwa jej natychmiast — pozostaje ona w Docku jako dynamiczna uruchomiona aplikacja, a znika dopiero po zamknięciu okna. Przypięcie działającej aplikacji (*Pin to Dock*) sprawia, że ikona pozostaje w Docku po zamknięciu. Menedżer plików `Files` jest zawsze przypięty (brak opcji odpięcia). Konfiguracja przypięć jest trwale zapisywana w `/home/.config/dock.conf` i zachowywana po restarcie.
- **Hierarchia zdarzeń (Input Priority):** Ściśle uporządkowany routing wejścia: okna modalne/dialogi → aktywne menu kontekstowe → Dock (zawsze na pierwszym planie, bez blokowania przez okna w tle) → kontrolki i obszar okien → górny pasek → elementy pulpitu → tło pulpitu.

Notatnik (Notes) otwiera pliki tekstowe z pulpitu po dwukliku oraz zapisuje dokumenty bezpośrednio do PollikFS v2 przez **Ctrl+S** lub przycisk **Save**. Menedżer plików (Files) wyświetla zawartość dysku w czasie rzeczywistym i umożliwia bezpośrednią nawigację po folderach oraz Koszu.

Klawiatura: podstawowy układ US; polskie znaki nie są jeszcze obsługiwane. F2 rozpoczyna zmianę nazwy zaznaczonego elementu pulpitu, Del przenosi do Kosza, a Ctrl+S zapisuje plik w Notatniku. Escape zamyka otwarte menu kontekstowe i okna dialogowe.

## Terminal

| Polecenie | Działanie |
| --- | --- |
| `help`, `about`, `version`, `gfx`, `mem`/`free` | Pomoc, informacje o systemie, grafika i pamięć |
| `pwd`, `cd`, `ls`/`dir`, `cat`/`type`, `stat` | Nawigacja i odczyt plików oraz metadanych |
| `mkdir`, `touch`, `rm`/`del`, `rmdir`, `mv`/`rename`, `cp` | Operacje na PollikFS; mv i cp nie nadpisują celu |
| `echo`, `which`, `history`, `clear`/`cls` | Tekst, informacje o komendach i historia sesji |
| `new`, `open`, `save` | Tworzenie i edycja plików w Notatniku |
| `ps`/`tasks`, `pause`, `resume`, `kill`, `spawn`, `faulttest` | Lista i obsługa procesów demonstracyjnych |
| `net`, `ping`, `publicip`, `lspci`, `beep` | Sieć i urządzenia |
| `uptime`, `time`, `date`, `perf`, `theme`, `anim` | Czas, wydajność i ustawienia pulpitu |
| `reboot`, `shutdown` | Zapis edycji i sterowanie zasilaniem |
| `tcc --help`, `tcc --version` | Pomoc do natywnego TinyCC dla konsoli x86_64 |

Terminal ma przewijany zapis sesji, historię poleceń pod Up/Down (64 pozycje), edycję linii do 159 znaków oraz `history -c`. `cat` odczytuje do 32767 bajtów na polecenie. Pulpit działa jako i386 i nie może uruchomić programu ELF64 `/bin/tcc`; przykłady kompilacji są w `kernel/arch/x86_64/TINYCC_PORT.md` i `CONSOLE_TTY.md`.

## Procesy i sieć — co rzeczywiście działa

Kernel konfiguruje IDT, PIC, PIT 100 Hz, TSS i GDT. Zegar wywłaszcza pulpit i dwa procesy ring 3. Każdy proces ma prywatny segment 64 KiB, własny stos użytkownika i własny stos kernela. Programy wykonują kod w CPL3, a `int 0x80` raportuje ich pracę. Wyjątek procesu zatrzymuje go i przekazuje czas innemu procesowi. Wyjątek kernela wypisuje numer na port szeregowy i zatrzymuje system.

To rzeczywiste procesy użytkownika, ale **aplikacje pulpitu nadal działają w kernelu**. Istnieje stronicowanie (PMM/VMM) i loader statycznych ELF32 uruchamianych w Ring 3 z własnym katalogiem stron (`/bin/hello` po sformatowaniu PollikFS v2 oraz wbudowane programy testowe). Układ pamięci: jądro (obraz + BSS) od 1 MiB, sterta jądra do `0x7F0000`, workery od 8 MiB; jądro mapuje tożsamościowo RAM do 1 GiB, przestrzeń użytkownika ELF zajmuje `0x40000000`–`0xC0000000`. RAM powyżej 1 GiB nie jest używany przez to jądro 32-bitowe. Stage 2 ładuje obraz jądra pod 1 MiB, bez dawnego limitu 512 KiB; build ogranicza kernel.bin do 4 MiB, a BSS < 0x600000. Szczegóły i ograniczenia: [ARCHITECTURE.md](ARCHITECTURE.md).

Sieć: własny sterownik PCI RTL8139 z DMA, Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS i TCP. Adres, maska, brama oraz DNS pochodzą z dzierżawy DHCP; po utracie łącza są usuwane, zamiast używać wpisanego na stałe adresu. Przycisk **Test connection** w Settings wysyła rzeczywisty ping; wynik pojawia się w tym oknie i pod `net`.

PollikOS Web korzysta z tego samego stosu do HTTP/1.1 i HTTPS przez BearSSL: waliduje nazwę hosta, datę i łańcuch podpisów, obsługuje przekierowania, odpowiedzi `Content-Length` i chunked oraz pobiera HTML, arkusze CSS, PNG/JPEG i klasyczne skrypty `<script src>`. Renderuje własny DOM, podstawowe style blokowe/inline/flex/tabel, formularze GET oraz ograniczony runtime JavaScript Elk z `document`, selektorami, zmianą DOM i zdarzeniami kliknięcia. Wysyła `Accept-Encoding: identity`, więc nie dekoduje gzip/brotli; nie obsługuje HTTP/2, HTTP/3, IPv6, modułów ES, canvas/WebGL ani pełnego CSS/DOM. To jest prawdziwe pobieranie i wykonywanie obsługiwanego podzbioru stron, a nie symulacja, lecz nie jest jeszcze silnikiem zgodnym z każdą współczesną stroną.

Zaufane CA są kompilowane z `kernel/certs/`. Zawarty jest lokalny `Norton Web/Mail Shield Root`, ponieważ na tym komputerze Norton przechwytuje HTTPS; weryfikacja nazwy, dat i podpisów pozostaje włączona. Usunięcie tego pliku z katalogu certyfikatów i przebudowanie obrazu wyłącza zaufanie do lokalnego pośrednika.

Nie ma natywnego stosu USB host (HID/storage) ani UEFI. Istnieje obraz instalatora Live przeznaczony do uruchomienia z USB i instalowania na dysk ATA oraz podstawowe audio Intel ICH AC'97 z fallbackiem PC Speaker. Fizyczny sprzet nie byl testowany.

## Testy

```powershell
python tests/smoke.py
python tests/process_stress.py --ram 256 2048
python tests/boot_memory.py --ram 64 256 2048
python tests/recovery.py
python tests/network_ring.py
python tests/browser_e2e.py
python tests/browser_js.py
python tests/display.py
```

`process_stress.py` czeka na wynik testów jądra po starcie: autotest izolacji przestrzeni adresowych, trzy programy błędów Ring 3 i 100 rzeczywistych cykli spawn/exit ELF `hello` z bilansem wolnych stron PMM. Obraz testowy wybiera zmienna `POLLIK_TEST_IMAGE` (domyślnie `PollikOS-Alpha.img`).

Test uruchamia prawdziwy obraz systemu w QEMU. Sprawdza rozruch, obraz VBE, klawiaturę i mysz, edycję, dock, tapetę, procesy (postęp obu, pauza jednego, izolowany wyjątek i ponowne uruchomienie), ARP/ICMP oraz tworzenie, zapis, odczyt po restarcie i usuwanie plików. Korzysta z osobnego `build/smoke-data.img`, nie z dokumentów użytkownika. Zrzuty PPM i logi są zapisywane w `build/`.

Drugi test uruchom po pierwszym. Na kopiach dysku testowego sprawdza odczyt poprzedniej migawki po uszkodzeniu najnowszej, odmowę zapisu na dysku z uszkodzonymi nagłówkami i rozruch bez dysku danych oraz karty sieciowej.

Test sieciowy wysyła 300 ramek ARP przez backend socket QEMU i sprawdza odpowiedzi również po dwukrotnym przejściu przez koniec pierścienia odbiorczego DMA. Testy przeglądarki uruchamiają obraz z DHCP: pierwszy sprawdza DNS, TCP, TLS i pobranie HTTPS, a drugi lokalny serwer HTTP z zewnętrznym CSS, PNG i JS, modyfikacją DOM oraz kliknięciem.

## Kod i dokumentacja techniczna

- `boot/` — własny rozruch BIOS, odczyt obrazu, A20 i tryb chroniony.
- `kernel/kernel.c` — pulpit, rysowanie, aplikacje i PS/2.
- `kernel/storage.c` — ATA PIO i PollikFS.
- `kernel/process.c`, `kernel/interrupts.asm` — przerwania, procesy i program CPL3.
- `kernel/network.c` — PCI/RTL8139, ARP i ICMP.
- `kernel/framebuffer.c` — walidacja VBE oraz pełne i częściowe aktualizacje ekranu.
- `assets/build_system_icons.py` - normalized PNG icons in /usr/share/icons, loaded from PollikFS; assets/build_cursor.py generates cursors.
- `fonts/build_font.py` — własne wektorowe definicje Pollik Sans i generator; nie korzysta z zewnętrznych fontów.
- `fonts/PollikSans.png` — próbka czcionki.
- `kernel/font_data.h` — gotowe maski pokrycia; zwykła kompilacja nie wymaga Pillow. Regeneracja fontu: `python fonts/build_font.py` (wymaga Pillow).

Dokumentacja użyta do weryfikacji mechanizmów sprzętowych i protokołów: [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), [rejestry RTL8139 w implementacji QEMU](https://qemu.googlesource.com/qemu/+/refs/tags/v8.1.2/hw/net/rtl8139.c), [ICMP — RFC 792](https://www.rfc-editor.org/info/rfc792/). Kod projektu jest własną, uproszczoną implementacją.

Protokół rozszerzenia myszy sprawdzono względem [implementacji PS/2 w QEMU](https://github.com/qemu/qemu/blob/master/hw/input/ps2.c).

Konfiguracja rozdzielczości korzysta z [QEMU fw_cfg](https://www.qemu.org/docs/master/specs/fw_cfg.html) oraz [QEMU Standard VGA](https://www.qemu.org/docs/master/specs/standard-vga.html).
