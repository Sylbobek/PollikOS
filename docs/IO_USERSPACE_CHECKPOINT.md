# I/O, userspace i trwałość danych — checkpoint 6–10

## Zaimplementowane

6. x86_64 używa jednej tablicy `Descriptor64`: typ, prawa odczytu/zapisu,
   backing i close-on-spawn. Pliki, potoki i konsola zachowują dotychczasowe
   numery fd oraz semantykę dup/dup2, dziedziczenia i refcount. Usunięto
   równoległe tablice `files`, `streams`, `pipe_index`, `pipe_write`,
   `console_alias` i `cloexec`. Tablica VFS nie nadpisuje już zajętego slotu
   potoku. Kontrola urządzeń wymaga `CAP_DEVICE` dla operacji zmieniających
   stan globalny; odczyt możliwości pozostaje dostępny.

7. `pollikos_spawn_rights` może wyłącznie ograniczyć prawa rodzica. Potomkowie
   nie odzyskują odebranych praw. Kernel nadaje pulpitowi prawo konfiguracji
   urządzeń; pulpit przekazuje je Settings. Browser uruchamiany przez pulpit
   dostaje tylko odczyt plików, sieć i okna. VFS, okna, sieć oraz operacje sesji
   sprawdzają te prawa w kernelu, również dla odziedziczonych fd. Usunięto
   błąd życia tablicy metod QuickJS: lazy initialization nie wskazuje już
   tablicy na zakończonym stosie. Istniejące SDK okien, zdarzeń i potoków jest
   wspólnym API aplikacji. i386 zachowuje GUI Ring 0 do czasu parytetu funkcji;
   nie usunięto działającego pulpitu ani parserów potrzebnych temu targetowi.

8. HTTP/1.1, URL, nagłówki, długość odpowiedzi i chunk decoding działają
   w `sdk/lib/http.c`, w procesie Ring 3. Proces może utrzymywać cztery
   żądania, kernel osiem strumieni z PID właściciela i monotonicznym tokenem.
   TCP/TLS są nieblokujące i mają deadline; exit/logout zamykają zasoby.
   TLS ma osobny dynamiczny kontekst na strumień. Został w kernelu wraz ze
   stosem IPv4 i sterownikami. Nie wyłączamy weryfikacji nazwy dla HTTPS z
   numerycznym IP: taka forma jest obecnie ENOTSUP. TLS wymaga prawdziwego
   źródła entropii i poprawnego UTC, bez słabego fallbacku.

9. PollikFS korzysta z `BlockDevice` nad istniejącymi ATA/AHCI: read, write,
   flush, granica okna sektorów i początek filesystemu. Dziennik redo obejmuje
   metadane: bitmapę, inode, katalogi i tablice pośrednie. Dane trafiają na
   dysk przed commit metadanych. Header i każdy rekord mają CRC32, commit
   jest utrwalany przed apply, a po apply następuje flush i wyczyszczenie
   znacznika. Recovery waliduje cały log przed zapisem i nie wychodzi poza
   ustaloną geometrię. Cache dziennika jest przydzielany raz po starcie.
   Mount sprawdza własność bloków, bitmapę, liczniki, typy i osiągalność inode.
   Uszkodzenia powodują read-only, bez automatycznej naprawy lub formatowania.

10. Workflow `.github/workflows/kernel.yml` buduje oba targety na Windows,
    wykonuje testy kodu wspólnego oraz QEMU i ma wyłącznie `contents: read`.
    Te same komendy są dostępne lokalnie przez `tools/ci.ps1`. Tryb Extended
    dodaje sesje, konsolę, współbieżną sieć i obrazy przeglądarki. Dokumentacja
    rozróżnia stan i386/x86_64, wdrożone API oraz granice walidacji.

## Zgodność danych i ABI

Superblock, inode, katalogi i drzewo bloków PollikFS v2 zachowują format.
Log zajmuje 64 sektory bezpośrednio przed filesystemem: dla dysku danych
LBA 0–63, dla układu instalowanego obszar przed LBA 16384. Nowy log powstaje
tylko w pustym prefiksie; obcy MBR lub nieznane dane nie są nadpisywane.
Znany dziennik jest resetowany wyłącznie przy jawnym formatowaniu jego celu.
Nie zmieniono pojemności 32 MiB ani nie formatowano obrazu użytkownika.

Publiczne C API `pollikos_http_*` pozostaje takie samo, ale wymaga ponownego
zbudowania starych ELF64 używających kernelowych syscalli HTTP. Ich dawne
numery są zarezerwowane i zwracają ENOTSUP. ABI info ma minor 1 i nowe bity
STREAMS/RIGHTS. NASM oraz C czytają wersję i możliwości ze wspólnego nagłówka.
Zwykłe operacje fd i pozostałe numery syscalli nie zmieniły się.

## Granice

- Dziennik chroni metadane, nie gwarantuje atomowej zamiany całej zawartości
  istniejącego pliku. Przerwany zapis danych może być częściowy.
- Log mieści 31 bloków metadanych na transakcję. Przekroczenie limitu kończy
  operację błędem i wycofaniem metadanych; nie przechodzi na niezabezpieczony zapis.
- To nadal jedno konto, polityka katalogów i jeden wątek procesu. Browser ma
  profil read-only; nie jest to wieloużytkownikowy ACL ani per-file portal.
- Transport TLS, compositor i sterowniki nadal są w kernelu; GUI i386 także.
  Nie deklarujemy pełnej migracji wszystkich aplikacji ani Device Managera.
- Stary kernel nie odtwarza oczekującego nowego dziennika. Recovery wykonuje
  aktualny kernel; nie cofamy obrazu kernela podczas niedokończonej transakcji.

## Weryfikacja i komendy

```
./tools/ci.ps1 -Target i386
./tools/ci.ps1 -Target x86_64
./tools/ci.ps1 -Target x86_64 -Extended
python tests/security_account_native.py
python tests/network_clients_x64.py
python tests/x86_64_security_session.py
```

Testy używają jednorazowych kopii obrazów. Native test wykonuje prawdziwy kod
VFS/PollikFS i przerywa transakcję rename po każdym z 19 zapisów sektorowych;
recovery daje kompletny stary albo nowy stan. Uszkodzony log i bitmapa są
montowane read-only bez zapisów. QEMU testuje pełny SELFTEST, 100 cykli Ring 3
i386 z bilansem PMM, ograniczone prawa potomka, sesje, równoległe HTTP/chunks
i własność tokenów. Przeglądarka pobiera HTML/CSS/JS i PNG/JPEG z lokalnej
fixture; framebuffer potwierdza kolory oraz wykonanie JavaScript.

Lokalne komendy CI dla i386 i x86_64 zakończyły się PASS. Końcowa regresja
konsoli ma `CONSOLE PASS`, obejmując touch/append, historię, potoki, sygnały
i natywne kompilowanie przez TinyCC. Migracja i tworzenie konta i386 były
sprawdzone także przez interoperacyjność hasha Argon2id z implementacją natywną.

Workflow jest przygotowany lokalnie. Nie opublikowano zmian ani nie
uruchomiono go na GitHub. Nie walidowano fizycznego sprzętu, nagłej utraty
zasilania rzeczywistego dysku ani wszystkich wariantów RAM/rozdzielczości.
