# Pollik Web — natywna aplikacja x86_64

Przeglądarka `/bin/browser.pol` działa w PollikOS w Ring 3. HTML, CSS,
JavaScript i obrazy są przetwarzane w gościu. Host nie wykonuje stron ani nie
przesyła gotowego obrazu przeglądarki. Aplikacja ma standardowe prawa użytkownika;
ochrona pamięci i kontrola dostępu do usług kernela pozostają aktywne.

## Implementacja

- libhubbub/libdom: rzeczywisty parser HTML5, naprawa struktury dokumentu,
  encje, konwersja do drzewa renderera PollikOS.
- libcss: parser, kaskada, specyficzność, `!important`, selektory atrybutów,
  `nth-child`, media i obliczanie obsługiwanych wartości `calc`.
- QuickJS: natywny runtime JavaScript, Promise/async, zmiany DOM i stylów,
  zdarzenia, `DOMContentLoaded`, timery, GET `fetch` z `text`, `json`,
  `arrayBuffer`, metadanymi odpowiedzi i jednokrotnym odczytem body.
- SDK HTTP/1.1: DNS/TCP/TLS gościa, przekierowania do ośmiu etapów,
  względne URL, chunked transfer, maksymalnie cztery aktywne żądania.
  TLS nadal weryfikuje certyfikat i nazwę hosta. QEMU wymaga RDRAND i poprawnego RTC.
- PNG/JPEG: dekodowanie i rysowanie w procesie aplikacji.
- Strona startowa `about:home`, pasek adresu/wyszukiwanie, historia wstecz/dalej,
  odświeżanie/zatrzymanie, przewijanie, formularze GET i nowe okno.
- Zakładki: Ctrl+D, lista Ctrl+B, trwały plik `/home/.pollik-web-bookmarks`.
  Zapis źródłowego HTML: Ctrl+S do `/home/Downloads`. Zapisy używają pliku
  tymczasowego i rename; błąd jest widoczny w pasku stanu.
- Loader ELF64 przyjmuje plik do 4 MiB; nadal maksymalnie 1 024 strony obrazu,
  W^X i zakaz zajmowania obszarów sterty/stosu/mmap. Małe programy zachowują
  poprzedni adres segmentu danych. ABI i386 pozostaje bez zmian.

## Granice zgodności

To nie jest jeszcze przeglądarka o zgodności Chromium/Firefox. Layout i malowanie
pozostają implementacją PollikOS, mimo użycia upstreamowych parserów i kaskady.
Flex/grid, typografia, Unicode, selekcja i formularze mają ograniczoną obsługę.
Nie ma pełnego Web API, importów modułów, XHR/WebSocket, service workers,
cookies/logowania, POST, dekodowania gzip/Brotli, HTTP/2, DRM ani kompletnego
odtwarzacza multimediów w stronie. Transport żąda `Accept-Encoding: identity`
i odrzuca nieobsługiwaną kompresję. Ctrl+N otwiera proces/okno, nie kartę.

Limity obejmują dokument 2 MiB, arkusze 512 KiB, 64 zasoby każdego rodzaju,
16 384 węzły DOM, 1 024 powiązania DOM/JS, 512 listenerów, 64 timery,
odpowiedź fetch do 4 MiB i VM JS do 128 MiB. To limity zasobów, nie obietnica
zgodności dowolnej strony. i386 zachowuje istniejącą przeglądarkę i backend Elk.

## Weryfikacja

`tests/web_engine_x64.py`: parser i style są uruchamiane w natywnym procesie
gościa; sprawdza naprawę tabel HTML5, encje, selektory/kaskadę/calc/media,
`innerHTML`, rozwiązywanie URL i 30 cykli parsowania/stylowania/zwalniania.

`tests/browser_images_x64.py`: rzeczywisty HTTP oraz framebuffer QEMU;
testuje obrazy PNG/JPEG, zewnętrzny CSS i JS, przekierowania, zdarzenia,
timery, fetch/Promise/bodyUsed, zakładki, zapis i stronę startową.
Opcja `--https https://example.org` dodaje publiczny HTTPS przez sieć gościa.
Test używa wyłącznie kopii dysku. Wyniki konkretnego uruchomienia zapisują się
w `build/x86_64/<wariant>/browser-images-native.log` i plikach PNG.

8 października 2026: normalny wariant przeszedł natywne HTTP/HTML/CSS/JS,
PNG/JPEG (po 3 072 piksele każdego obrazu), 5 163 piksele stylu zmienionego
przez JS, timery/fetch/DOMContentLoaded i zapisy aplikacji. Publiczny
`https://example.org` zwrócił 200 po poprawnie zweryfikowanym handshake TLS.
Wcześniejsze przerwanie inicjalizacji QuickJS usunięto: dokument nie rejestruje
ponownie metod `querySelector`/`querySelectorAll` już nadanych przez wrapper DOM.
Zrzut strony startowej potwierdzono na rzeczywistym framebufferze gościa.

Budowanie: `powershell -File build-x86_64.ps1` lub wariant `-Production`.
Launcher: `powershell -File run-x86_64.ps1 -PersistData` zachowuje zapisy na
dysku danych po zamknięciu QEMU. Bez tej opcji launcher używa tymczasowego
snapshotu; pliki i zakładki znikają po restarcie, zgodnie z jego komunikatem.
Test normalnego wariantu: ustaw `POLLIK_X64_VARIANT=kernel`.
Żaden z tych testów nie stanowi certyfikacji całego Internetu ani sprzętu.
