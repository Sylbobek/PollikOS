# Pollik OS v0.1 Alpha

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

Zielony przycisk i F11 przełączają maksymalizację/przywrócenie. Przeglądarka przelicza układ strony do nowej szerokości. Pozostałe aplikacje zachowują dotychczasowy układ kontrolek w większym oknie. Prawy przycisk otwiera menu pulpitu lub okna; w Files zaznacza plik i pozwala go otworzyć. Nad linkami, polami tekstowymi i menu kursor zmienia kształt. Nie ma jeszcze zaznaczania wielu obiektów prostokątem ani pełnego zaznaczania tekstu.

YouTube: transport HTTPS i pobranie HTML działają, ale aplikacja YouTube wymaga nieobsługiwanych funkcji JavaScript i Web API. Filmy nie są odtwarzane. Błędy interpretera pokazują teraz informację o niepełnej obsłudze JS na pasku stanu, zamiast sugerować, że strona jest w pełni gotowa.

Wymagane w PATH: `nasm`, `clang`, `ld.lld`, `llvm-objcopy`, `qemu-system-x86_64`. Są już dostępne na tym komputerze. Ctrl+Alt zwalnia mysz przechwyconą przez QEMU.

- `build/PollikOS-Alpha.img` — rzadki obraz rozruchowy BIOS, 10 GiB pojemności logicznej. Kompilacja go odtwarza.
- `build/PollikData.img` — osobny dysk dokumentów, rozszerzany do 10 GiB. Kompilacja **zachowuje istniejące dane**. Nie usuwaj go, jeśli chcesz zachować dokumenty; można kopiować go przy wyłączonym emulatorze jako kopię zapasową. PollikFS nadal mieści osiem plików po 1023 bajty; reszta pojemności czeka na rozbudowę systemu plików.

Skrypt uruchamia dyski jako primary IDE master/slave, VGA standard i RTL8139 w sieci użytkownika QEMU oraz przydziela 2 GiB RAM. Jądro nadal korzysta ze stałej mapy pamięci i nie udostępnia całej tej pamięci dynamicznemu alokatorowi. BIOS musi obsługiwać VBE 1024×768. Obrazy sparse zajmują na NTFS tylko zapisane zakresy.

Przeglądarka pokazuje stan ładowania przed żądaniem i automatycznie odmalowuje wynik po jego zakończeniu. Pierwsze żądanie oczekuje na DHCP. Strona startowa oraz frazy wpisane przez Ctrl+L korzystają z wyników DuckDuckGo Lite; PollikOS nie ma własnego indeksu całego Internetu. Pobieranie pozostaje synchroniczne, więc podczas żądania obsługa wejścia czeka na jego zakończenie.

## Pulpit i dokumenty

Okna mają rogi o promieniu 28 px z wygładzaniem krawędzi. **Cienie są całkowicie usunięte**, również spod docka. Zaokrąglone są też ikony i przyciski. F1–F6 wybiera aplikacje; F6 otwiera przeglądarkę PollikOS Web, a Esc pokazuje pulpit. Działa mysz, dock i przeciąganie okna. Czerwony i żółty przycisk ukrywają okno; zielony przywraca jego pozycję na środku. Jedno okno jest widoczne naraz.

Dock ma nową półprzezroczystą powierzchnię, pięć autorskich ikon inspirowanych współczesnymi interfejsami macOS/Windows, animowane powiększanie po najechaniu i etykiety aplikacji. Okna pojawiają się z krótką animacją przesunięcia (około 180 ms). Kursor ma wygładzony ciemny grot z jasną obwódką; nad tekstem zmienia się w kursor tekstowy, a nad dockiem we wskaźnik. Grafiki nie są kopiami zasobów Apple/Microsoft.

Sterownik myszy negocjuje rozszerzenie PS/2 z rolką, sprawdza ACK i ponawia polecenia po RESEND; dostępny jest powrót do protokołu 3-bajtowego. Rolka przewija notatnik. Składanie obrazu używa pamięci podręcznej tapety, osobnego bufora sceny i częściowych zapisów framebufferu. Zwykły ruch kursora odtwarza jedynie jego poprzedni prostokąt i nowy kursor. Animacja docka zapisuje dolny pas ekranu. Zmiana zawartości okna wymaga pełnego odświeżenia. Częstotliwość animacji ogranicza zegar; nie ma gwarancji 60 FPS.

Sterownik VBE sprawdza adres, pitch, tryb direct-color i układ kanałów RGB; obsługuje 24/32 bity oraz obcina prostokąty do granic ekranu. To nadal renderowanie programowe, bez akceleracji GPU i synchronizacji VSync. Nowe polecenie `gfx` pokazuje liczbę prezentacji i kopiowanych pikseli.

Notatnik zapisuje dokument przez **Ctrl+S** lub przycisk **Save**. PgUp/PgDn przewijają treść. Zapis odbywa się również przed otwarciem innego dokumentu i przed poleceniem `reboot`; nieudany zapis blokuje te działania. Zamknięcie okna QEMU nie zapisuje niezapisanej edycji. Files pokazuje prawdziwą listę dokumentów odczytaną z dysku; kliknięcie wiersza otwiera dokument.

PollikFS mieści osiem plików, każdy do 1023 bajtów, z nazwą do 23 znaków (`a-z`, `0-9`, `.`, `_`, `-`). To własny mały system plików ze stałym katalogiem, bez folderów. Sterownik ATA PIO zapisuje naprzemiennie dwie kompletne migawki: najpierw dane i flush, następnie nagłówek z generacją i sumami kontrolnymi. Przy uszkodzeniu ostatniej migawki można odczytać poprzednią. Jeśli obie są nieprawidłowe, zapis zostaje wyłączony; system nie formatuje automatycznie rozpoznanego niepustego, uszkodzonego dysku. Nie zastępuje to kopii zapasowych.

Własna czcionka **Pollik Sans** zastępuje font 5×7. Ma 95 znaków ASCII, małe i wielkie litery, proporcjonalne odstępy i wygładzanie 4-bitowe. Krzywe są rasteryzowane osobno dla rozmiarów 14, 18, 26, 40 i 52 px, zamiast powiększania małych bitmap. Notatnik używa naturalnych odstępów, a terminal stałych komórek dla wyrównania kolumn.

Klawiatura: podstawowy układ US; polskie znaki nie są jeszcze obsługiwane. Polecenia wpisuj małymi literami. Edycja odbywa się na końcu dokumentu (Backspace usuwa ostatni znak).

## Terminal

| Polecenie | Działanie |
| --- | --- |
| `help`, `about`, `mem`, `clear` | Pomoc, wersja, stała mapa pamięci, czyszczenie terminala |
| `gfx` | Stan framebufferu i liczniki częściowego odświeżania |
| `ls` | Lista plików |
| `new projekt.txt` | Utworzenie pustego pliku na dysku i otwarcie w notatniku |
| `open projekt.txt` | Otwarcie pliku |
| `save` | Zapis aktualnego dokumentu |
| `cat` | Początek aktualnego dokumentu |
| `rm projekt.txt` | Usunięcie pliku; najpierw otwórz inny dokument |
| `ps` | Stany, liczniki pracy i przydziałów czasu procesów |
| `pause 1`, `resume 1` | Wstrzymanie/wznowienie procesu |
| `kill 1`, `spawn 1` | Zatrzymanie/uruchomienie od początku procesu |
| `faulttest` | Proces 1 wykonuje zabronioną instrukcję; kernel zatrzymuje tylko jego |
| `net` | Stan karty, adresy, liczniki ramek i wynik ping |
| `ping` | Asynchroniczny ping bramy QEMU 10.0.2.2 |
| `publicip` | Pobiera publiczny adres IPv4 przez zweryfikowany HTTPS |
| `theme` | Zmiana tapety |
| `reboot` | Zapis edycji i restart przez kontroler klawiatury |

PID 1 i 2 to dwa wbudowane programy demonstracyjne. Terminal przyjmuje 48 znaków na polecenie i pokazuje ostatni wynik, bez historii. `cat` jest ograniczony do 239 znaków.

## Procesy i sieć — co rzeczywiście działa

Kernel konfiguruje IDT, PIC, PIT 100 Hz, TSS i GDT. Zegar wywłaszcza pulpit i dwa procesy ring 3. Każdy proces ma prywatny segment 64 KiB, własny stos użytkownika i własny stos kernela. Programy wykonują kod w CPL3, a `int 0x80` raportuje ich pracę. Wyjątek procesu zatrzymuje go i przekazuje czas innemu procesowi. Wyjątek kernela wypisuje numer na port szeregowy i zatrzymuje system.

To rzeczywiste procesy użytkownika, ale **aplikacje pulpitu nadal działają w kernelu**. Istnieje stronicowanie (PMM/VMM) i loader statycznych ELF32 uruchamianych w Ring 3 z własnym katalogiem stron (`/bin/hello` po sformatowaniu PollikFS v2 oraz wbudowane programy testowe). Układ pamięci: jądro (obraz + BSS) od 1 MiB, sterta jądra do `0x7F0000`, workery od 8 MiB; jądro mapuje tożsamościowo RAM do 1 GiB, przestrzeń użytkownika ELF zajmuje `0x40000000`–`0xC0000000`. RAM powyżej 1 GiB nie jest używany przez to jądro 32-bitowe. Stage 2 ładuje obraz jądra pod 1 MiB, bez dawnego limitu 512 KiB. Szczegóły i ograniczenia: [ARCHITECTURE.md](ARCHITECTURE.md).

Sieć: własny sterownik PCI RTL8139 z DMA, Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS i TCP. Adres, maska, brama oraz DNS pochodzą z dzierżawy DHCP; po utracie łącza są usuwane, zamiast używać wpisanego na stałe adresu. Przycisk **Test connection** w Settings wysyła rzeczywisty ping; wynik pojawia się w tym oknie i pod `net`.

PollikOS Web korzysta z tego samego stosu do HTTP/1.1 i HTTPS przez BearSSL: waliduje nazwę hosta, datę i łańcuch podpisów, obsługuje przekierowania, odpowiedzi `Content-Length` i chunked oraz pobiera HTML, arkusze CSS, PNG/JPEG i klasyczne skrypty `<script src>`. Renderuje własny DOM, podstawowe style blokowe/inline/flex/tabel, formularze GET oraz ograniczony runtime JavaScript Elk z `document`, selektorami, zmianą DOM i zdarzeniami kliknięcia. Wysyła `Accept-Encoding: identity`, więc nie dekoduje gzip/brotli; nie obsługuje HTTP/2, HTTP/3, IPv6, modułów ES, canvas/WebGL ani pełnego CSS/DOM. To jest prawdziwe pobieranie i wykonywanie obsługiwanego podzbioru stron, a nie symulacja, lecz nie jest jeszcze silnikiem zgodnym z każdą współczesną stroną.

Zaufane CA są kompilowane z `kernel/certs/`. Zawarty jest lokalny `Norton Web/Mail Shield Root`, ponieważ na tym komputerze Norton przechwytuje HTTPS; weryfikacja nazwy, dat i podpisów pozostaje włączona. Usunięcie tego pliku z katalogu certyfikatów i przebudowanie obrazu wyłącza zaufanie do lokalnego pośrednika.

Brak również USB, dźwięku, UEFI i instalatora. Fizyczny sprzęt nie był testowany.

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
- `assets/build_ui.py` — własne ikony i kursory, generator tablic `kernel/ui_data.h` (regeneracja wymaga Pillow).
- `fonts/build_font.py` — własne wektorowe definicje Pollik Sans i generator; nie korzysta z zewnętrznych fontów.
- `fonts/PollikSans.png` — próbka czcionki.
- `kernel/font_data.h` — gotowe maski pokrycia; zwykła kompilacja nie wymaga Pillow. Regeneracja fontu: `python fonts/build_font.py` (wymaga Pillow).

Dokumentacja użyta do weryfikacji mechanizmów sprzętowych i protokołów: [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), [rejestry RTL8139 w implementacji QEMU](https://qemu.googlesource.com/qemu/+/refs/tags/v8.1.2/hw/net/rtl8139.c), [ICMP — RFC 792](https://www.rfc-editor.org/info/rfc792/). Kod projektu jest własną, uproszczoną implementacją.

Protokół rozszerzenia myszy sprawdzono względem [implementacji PS/2 w QEMU](https://github.com/qemu/qemu/blob/master/hw/input/ps2.c).

Konfiguracja rozdzielczości korzysta z [QEMU fw_cfg](https://www.qemu.org/docs/master/specs/fw_cfg.html) oraz [QEMU Standard VGA](https://www.qemu.org/docs/master/specs/standard-vga.html).
