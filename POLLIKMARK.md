# PollikMark — programowy benchmark PollikOS

Stan dokumentacji: 2026-09-18. Opis implementacji `kernel/gui/pollikmark.c`, `kernel/gfx_device.{c,h}` i `kernel/soft3d.h` oraz **istniejących**, ograniczonych pomiarów końcowych. Nie wykonano nowego benchmarku przy pisaniu tego pliku.

## Tożsamość i zakres

- Obraz: `build/PollikOS-Surface.img`; jądro **522560 / 524288 B**, zapas **1728 B**.
- SHA-256 jądra: `64756ee53d2455ceae00d3e37717d65710377a83a3a7f4560300fb125e7c574b`.
- SHA-256 ELF: `fd5fcba0ebf98accf8094b650ceace136197bb2a9e24eeb5441769bb1249ea96`.
- Końcowe źródła wyników: `build/pollikmark-1024x768.json`, `build/pollikmark-1920x1080.json`, `build/final-ui-522560.json`. Kopie PollikMark w `build/final-ui-522560/pollikmark-<rozdzielczość>/` są zgodne z tymi JSON-ami.
- QEMU `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`, `pc`, TCG, CPU `max`, RAM 256 MiB, VGA 32 MiB, bez NIC; host `Windows-10-10.0.26200-SP0`, Python 3.11.9. Obraz w trybie snapshot, osobny tymczasowy dysk danych. Sterowanie PS/2 i sondy QMP mają narzut; parametry CPU fizycznego hosta i jego obciążenie nie zostały zapisane.

To **renderer CPU, nie benchmark GPU**. Nie ma sprzętowej akceleracji 3D, pomiaru VSync ani podstaw do porównania z komercyjnymi benchmarkami. Wyniki GUI, tożsamość środowiska i pełna macierz końcowych testów: [PERFORMANCE.md](PERFORMANCE.md).

## Obsługa i powierzchnia

PollikMark zajmuje slot aplikacji 6 (siódma ikona, F7). Klawisze 1–8 uruchamiają wybrany test; Enter uruchamia sekwencję wszystkich wspieranych testów; Esc lub `s` zatrzymuje bez minimalizowania okna. `i` przełącza informacje, `n`/`p` przegląda poziomy po zatrzymaniu. Zamknięcie zwalnia bufory, zachowując surowe wyniki; ponowne uruchomienie danego poziomu zastępuje jego wynik. Rozpoczęcie Run all nie zeruje globalnie całej tablicy, więc zachowane poziomy mogą pochodzić z wcześniejszych uruchomień.

Domyślne okno ma 680×410, minimalne 480×280. Obszar wejściowy viewportu to `(win_w - 220) × (win_h - 190)`. Jest dopasowywany z zachowaniem proporcji (z całkowitoliczbowym obcięciem) do **maksymalnie 192×128 px** — nie zawsze ma dokładnie taki rozmiar. Przykłady wynikające z kodu:

| Okno | Viewport |
|---|---:|
| 680×410 | 192×91 |
| 480×280 | 192×66 |
| Maksymalizacja przy 1024×768: 1008×626 | 192×106 |
| Maksymalizacja przy 1920×1080: 1904×938 | 192×85 |

Proporcje odnoszą się do wydzielonego obszaru klienta, nie do całego ekranu. Obraz jest kopiowany **1:1, wyśrodkowany, bez skalowania końcowego i rozciągania**. Przy wysokości okna poniżej 410 podgląd jest ukryty na rzecz metryk; obliczenia mogą nadal działać.

Trzy bufory viewportu: roboczy kolor RGB888 w `u32`, głębia `float` oraz ukończony obraz do wyświetlenia. Stride i capacity są w pikselach; w tym kliencie stride = szerokość. Maksimum tych trzech buforów to 294912 B przed narzutem/zaokrągleniem alokatora. Nowe bufory są przydzielane przed zwolnieniem starych, więc chwilowy koszt resize może być większy. Nie jest to całkowita pamięć aplikacji ani kompozytora.

Zmiana rozmiaru restartuje **tylko aktualny poziom**, jeżeli rzeczywiste wymiary viewportu uległy zmianie; zachowuje wcześniejsze poziomy i tryb Run all. Nieudana alokacja zachowuje stary viewport i zgłasza komunikat. Jeśli cap daje ten sam rozmiar, poziom nie jest restartowany. Render UI wyświetla ukończony bufor i liczby; nie wykonuje samego obciążenia, alokacji ani yieldów. Praca benchmarku jest kooperacyjna w `pollikmark_poll()`.

## Dokładne obciążenia programowe

Każda iteracja graficzna zaczyna się od wyczyszczenia koloru i głębi. Ten koszt jest wliczany do czasu pracy; jednostki przepustowości zależą od testu.

| Nr / test | Poziomy i rzeczywista praca | Jednostka `rate` |
|---|---|---|
| 1 / Fill Rate | Jeden poziom: wyczyszczenie całego offscreen koloru oraz depth=1.0. To nie fill całego ekranu ani transfer do LFB. | Piksele viewportu/s, liczone raz mimo zapisów color i depth |
| 2 / 2D Shapes | **100, 500, 1000, 5000** obiektów/iterację (nie 10000). Deterministyczny cykl pięciu prymitywów opisany poniżej. | Zlecone kształty/s |
| 3 / Triangle | Jeden obracany trójkąt, macierze model/view/perspective, interpolowany RGB, bez cullingu. | Zlecone trójkąty/s |
| 4 / Cube | 12 trójkątów sześcianu; trzy poziomy: wireframe, flat, interpolowany kolor wierzchołków. Obrót, perspektywa, depth i culling. | Zlecone trójkąty/s, nie widoczne ściany/s |
| 5 / Geometry | **100, 500, 1000, 5000, 10000** małych deterministycznie rozmieszczonych trójkątów w clip space, interpolowany kolor, bez cullingu; powtarzające się pozycje i overdraw. | Zlecone trójkąty/s |
| 6 / Texture | **NOT SUPPORTED**, zero poziomów, pomijany w Run all. | N/A, nie zero wydajności |
| 7 / Compositor | Przez co najmniej 1 s rzeczywiste żądanie repaintu klienta PollikMark i różnice globalnych liczników GUI. Nie otwiera sztucznych okien ani nie symuluje ich liczby. | Ukończone klatki kompozytora/s |
| 8 / Memory | 30 poziomów: po sześć operacji dla **1, 4, 8, 16, 32 MiB**. | Umownie zliczone bajty odczytu+zapisu/s |

2D Shapes: `item % 5` wybiera prostokąt, roundrect z promieniem do 3 i pokryciem narożników, połączoną linię diagonalną, stałą bitmapę napisu **PM** (dwa glify 5×7, obszar 11×7), prostokąt half-alpha. To nie pełny renderer fontów aplikacji. Początek ma `x=(item*37 % width)-5`, `y=(item*53 % height)-3`, rozmiar nominalny `7+item%17` na `7+item%11`; obiekty są przycinane do targetu. Obiekt liczy się jako zlecony także wtedy, gdy część pikseli jest poza obszarem.

Triangle używa bazowych punktów (-0.75,-0.75), (0.75,-0.75), (0,0.75), z=0, w=1. Cube używa ośmiu wierzchołków ±1 przeskalowanych przez 0.65. Obrót korzysta z racjonalnej parametryzacji sin/cos bez libm; kamera ma przesunięcie y=-0.1, z=-3, perspektywa focal=1.7, near=0.1, far=20 i aspect viewportu. Licznik fazy obrotu jest zachowywany między uruchomieniami, więc kolejność uruchomień ma znaczenie. Geometry używa pozycji `(item*37%180)/100-0.9` i `(item*53%180)/100-0.9`, rozmiaru s=0.08; to nie kosztowna scena modeli 3D.

Interfejs soft3d zapewnia clipping w sześciu płaszczyznach jednorodnych **przed dzieleniem przez w**, odrzucanie danych niefinitywnych/poza kontraktem, CCW front w NDC i row-scissor do pracy porcjami. `GfxInfo.capabilities` zawiera clear, triangle, depth, color; brak capability tekstur. Sama obecność pola `Vertex.uv` nie oznacza implementacji samplera, filtrowania ani mapowania tekstur.

Oddzielne `pollikmark_raster[test][level]` zlicza udane zapisy pikseli rasteryzacji i ich tempo, **bez clear**. To zapisy przechodzące depth, nie unikalne piksele ekranu. Końcowe JSON-y QEMU nie eksportują tego symbolu — nie dopisujemy liczbowych wyników raster pixels/s na podstawie samej liczby trójkątów.

### Memory: co jest mierzone, a co nie

Dla każdego rozmiaru kolejność wynosi: clear, copy, blend, surface, pitched copy, allocation + touch. Nie wszystkie tryby potrzebują dwóch buforów: clear i allocation+touch jednego, pozostałe dwóch. Przed pomiarem bufory są inicjalizowane porcjami (0x35 i 0x68), po czym resetowane są próbki i zegar poziomu.

| Operacja | Praca | Jednostki na pełną iterację rozmiaru B |
|---|---|---:|
| clear | `memset` bufora A | B |
| copy | `memcpy` B→A blokami | 2B |
| blend | Średnia bajtowa dwóch buforów przez maskowanie/przesunięcia i zapis A | 3B |
| surface | `memcpy` wierszami po 1024 B (256 px) | 2B |
| pitched copy | 240 px kopiowanych na każde 256 px pitch; padding pomijany | 30B/16 |
| allocation + touch | **Jedna alokacja na poziom**, a potem iteracyjne `memset` już przydzielonego bufora | B |

`alloc_us` jest osobnym czasem jednorazowego przydzielenia wymaganych buforów. **Allocation + touch nie mierzy wielokrotnych alokacji/zwolnień ani przepustowości alokatora.** Inicjalizacja i alokacja nie należą do `work_us`; zwolnienie następuje po poziomie lub stop. Umowne bajty nie uwzględniają wszystkich szczegółów cache, write-allocate czy emulacji, więc rate nie jest bezpośrednim pomiarem magistrali RAM.

Przy pierwszej nieudanej alokacji poziom ma status 3, bufory są zwalniane i przebieg kończy się z zachowaniem wyników. Większa wolna suma RAM nie gwarantuje odpowiednio dużego ciągłego obszaru. Pełnej drabiny 1–32 MiB nie uruchomiono w końcowym QEMU.

## Budżety, zegary i statusy

- Grafika: nominalny deadline jednego poll = teraz + **2500 µs**, najwyżej **16** kroków, sprawdzanie zegara między krokami. Clear ma porcję 2048 pikseli; duży trójkąt i cube po dwa wiersze; geometria cały mały trójkąt (przy cap około ≤16×11 px); shapes jeden mały obiekt.
- Pamięć: najwyżej **256 KiB zakresu na poll** (rzeczywisty ruch bajtów zależy od trybu); nie pętla wymuszająca zmieszczenie operacji w 2500 µs.
- Limit czasu jest kooperacyjny, nie preempcją w środku prymitywu. Powolny host lub fallback zegara PIT około 8.3 ms może przekroczyć nominalny budżet; cap kroków nadal ogranicza porcję.
- Zwykły poziom kończy się statusem 1 po **co najmniej 1 s i co najmniej 3 kompletnych iteracjach**. Deadline **5 s** jest sprawdzany na wejściu do poll (poza inicjalizacją pamięci); nieukończona iteracja jest odrzucana. Nie jest to twardy limit ścienny obejmujący każdą fazę i narzut hosta.
- Limit próbek wynosi **4096**; trafienie limitu bez spełnienia normalnego zakończenia daje status 2. Live UI jest aktualizowany także w toku pracy mniej więcej co 100 ms.
- Compositor jest wyjątkiem: kończy po ≥1 s i pobiera liczniki GUI; nie wymaga trzech iteracji ani nie zbiera rozkładu odstępów.

| `status` w ABI | Znaczenie |
|---:|---|
| 0 | Brak ukończonego zapisanego wyniku; nie dowód zerowego czasu lub braku kosztu |
| 1 | Poziom ukończony zgodnie z warunkami |
| 2 | Anulowanie, deadline lub limit próbek; wynik częściowy wykluczony ze score |
| 3 | Nieudana alokacja pamięci poziomu; wykluczony ze score |

Brak buforów viewportu przy starcie/resize jest zgłaszany komunikatem i nie musi tworzyć rekordu ze statusem 3. Texture ma komunikat NOT SUPPORTED, nie fikcyjny wynik. Run all po deadline/limicie pomija wyższe obciążenia danego testu i przechodzi dalej; nieudana alokacja pamięci kończy przebieg. Stop/close zachowuje ukończone poziomy; aktualny otrzymuje status 2.

## Metryki i score

`pollikmark_results[8][30]` ma rekord 15 × `u32` w kolejności:

`status, n, mean_us, min_us, max_us, low_fps, fps, rate, work_us, units, paint_us, compose_us, present_us, total_us, alloc_us`.

Czasy są w µs. `pollikmark_intervals[4096]` przechowuje próbki bieżącego/ostatniego poziomu, nie archiwum wszystkich poziomów. Wyniki ABI są całkowitoliczbowe, iloraz obcinany i nasycany do u32.

- **Iteration FPS** = ukończone iteracje / suma ich czasów. Próbki obejmują przerwy kooperacyjne, malowanie GUI i narzut obserwacji w trakcie iteracji; to nie prezentacje viewportu/s ani FPS GPU.
- **Rate** = jednostki ukończonych iteracji / suma zmierzonego czasu kroków pracy. Nie obejmuje całego wall time, alokacji ani inicjalizacji pamięci; dla grafiki obejmuje clear, ale nie cały koszt przygotowania macierzy w `begin_frame()`. Wysoki rate przy niskim Iteration FPS nie jest sprzecznością.
- **1% low** = odwrotność średniej najwolniejszych `ceil(n/100)` czasów. Implementacja utrzymuje top-41 dla n≤4096. Hostowe `distribution` liczy p95/p99 metodą nearest-rank; przy 4–5 próbkach oba są po prostu maksimum. Nie są wiarygodną charakterystyką długiego ogona.
- Compositor: n i FPS z różnic globalnych liczników i czasu poziomu, średnie paint/compose/present/TOTAL z tych samych różnic. Pole rate = FPS. **Mean/min/max/low ustawione na 0 oznaczają brak próbkowania rozkładu**, nie zerową latencję. Tę metrykę mogą współtworzyć inne widoczne okna.

Score UI (`Score (partial)`) to średnia arytmetyczna znormalizowanych wyników **ukończonych poziomów**, a nie średnia równoważnych testów. Każdy poziom ma równą wagę, więc test z wieloma poziomami może dominować. Referencje są umownymi stałymi, nie pomiarem referencyjnego sprzętu:

| Obciążenie | Referencja dla 100 punktów |
|---|---:|
| Fill | 10000000 pikseli/s |
| Shapes | 10000 kształtów/s |
| Triangle / Cube / Geometry | 10000 trójkątów/s |
| Compositor | 60 klatek/s |
| Memory | 104857600 B/s = 100 MiB/s |

Dokładnie: dla każdego kwalifikującego się poziomu `q = floor(100 * rate / reference)`, potem `score = floor(sum(q) / liczba_poziomów)` (z nasycaniem ilorazów). Wykluczone: status≠1, Texture i każdy poziom allocation+touch. Nie dodaje się za nie zer do średniej. **60 w mianowniku nie jest osiągniętym FPS ani deklaracją celu spełnionego.** Różne viewporty, rozdzielczości, otwarte okna i zestawy zachowanych poziomów nie tworzą porównywalnego rankingu.

## Rzeczywiste wyniki końcowego QEMU

Oba raporty mają PASS dla scenariusza testowego. Skrypt sprawdził siedem ikon/dock, ukończony Triangle w domyślnym oknie, anulowanie Cube, maximize, ponowny Triangle, restore, anulowanie Memory, Compositor, komunikat Texture, guards oraz close/reopen z zachowaniem wyników i zwolnieniem buforów. **Nie wykonał Enter/Run all.** Cały proces testu według runnera trwał 6.812 / 8.375 s — to nie czas pojedynczego poziomu.

### Triangle: pierwszy ukończony przebieg, okno 680×410

Źródło: pole `triangle`, `triangle_intervals`, `distribution`. Wszystkie statusy 1. Viewport 192×91; liczby FPS/low w ABI są obcięte do całkowitych.

| Ekran | n | Mean ABI µs | Min / max µs | p95 / p99 µs | Iteration FPS ABI | 1% low ABI (host dokładniej) | Tri/s | Work µs | Units |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024×768 | 5 | 220398 | 199090 / 251308 | 251308 / 251308 | 4 | 3 (3.979181) | 501 | 9963 | 5 |
| 1920×1080 | 4 | 331983 | 276807 / 427278 | 427278 / 427278 | 3 | 2 (2.340397) | 412 | 9707 | 4 |

Surowe odstępy µs: 1024: `199090, 250250, 201063, 251308, 200280`; 1920: `299119, 276807, 324730, 427278`. Średnie hosta to odpowiednio **220398.2** i **331983.5** µs. Różnice mimo jednakowego viewportu mogą wynikać z kompozycji większego ekranu, harmonogramowania i QMP; jeden przebieg nie pozwala przypisać ich wyłącznie rasteryzacji.

### Triangle: późniejszy wynik po maksymalizacji

Źródło: **`raw_results`**, test 2, poziom 0 (indeksowanie od zera). Późniejszy Triangle nadpisał ten sam slot; dlatego nie jest identyczny z polem `triangle`. Nie wolno dopasowywać do niego wcześniejszej tablicy `triangle_intervals` ani jej p95/p99.

| Ekran / viewport z geometrii | Status | n | Mean µs | Min / max µs | Iteration FPS | 1% low | Tri/s | Work µs | Units |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024×768 / 192×106 | 1 | 4 | 269493 | 251667 / 275927 | 3 | 3 | 469 | 8525 | 4 |
| 1920×1080 / 192×85 | 1 | 3 | 493362 | 477070 / 501922 | 2 | 1 | 623 | 4809 | 3 |

p95/p99 tego późniejszego przebiegu **nie są zapisane jako osobny rozkład**; nie deklarujemy nowego pomiaru. Zero pól paint/compose/present/TOTAL w rekordzie Triangle oznacza, że nie są tu próbkowane osobno.

### Compositor: faktyczny repaint GUI

Źródło: `compositor` i końcowy slot testu 6 w `raw_results` (zgodne). Poziom ukończony, okno po restore. Średnie µs są całkowitoliczbowe.

| Ekran | Status | Klatki | FPS = rate | Paint µs | Compose µs | Present µs | TOTAL µs |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1024×768 | 1 | 13 | 12 | 3370 | 47742 | 8213 | 59326 |
| 1920×1080 | 1 | 10 | 9 | 8955 | 55294 | 19268 | 83517 |

Suma obciętych średnich może różnić się o 1 µs od obciętego TOTAL (1024: 59325 vs 59326). Nie ma tu rozkładu frame intervals: zera min/max/mean/low nie są wynikami latencji. Tego testu nie wolno utożsamiać z drag pięciu okien z PERFORMANCE.md.

### Pozostałe zachowane poziomy i częściowy score

W tablicy `raw_results` niezerowe rekordy występują wyłącznie dla Triangle, Cube, Compositor i Memory na pierwszym poziomie:

| Obciążenie | 1024×768 | 1920×1080 |
|---|---|---|
| Cube wire | status 2, n=0 — anulowany | status 2, n=0 — anulowany |
| Memory 1 MiB clear | status 2, n=0, alloc=96 µs | status 2, n=1, mean=min=max=425914 µs, FPS=2, low=2, work=544 µs, units=1048576, rate=1927529411 B/s, alloc=138 µs |
| Fill, Shapes, Geometry | Brak końcowego QEMU pomiaru: **NOT RUN** | **NOT RUN** |
| Pełne Cube i Memory | **NOT RUN**; próby anulowane | **NOT RUN**; próby anulowane |
| Texture | **NOT SUPPORTED**; komunikat sprawdzony | **NOT SUPPORTED**; komunikat sprawdzony |

Pojedyncza iteracja Memory ze statusem 2 nie jest zaakceptowanym wynikiem przepustowości pamięci i nie wchodzi do score. Zera pozostałych slotów nie są wynikami benchmarku.

**Tylko rekonstrukcja częściowego score z końcowego JSON-a**, nie odczyt liczby z UI ani pełny wynik Run all:

- 1024×768: `floor((floor(469*100/10000) + floor(12*100/60))/2) = 12`.
- 1920×1080: `floor((floor(623*100/10000) + floor(9*100/60))/2) = 10`.

Kwalifikują się zaledwie dwa poziomy: **późniejszy Triangle i Compositor**. Nie wlicza się pierwszego Triangle drugi raz, anulowanego Cube/Memory ani niewykonanych testów. Z powodu różnych viewportów i małej próby nie przedstawiamy 12 vs 10 jako rankingu wydajności ani procentowej różnicy.

## Walidacja i ograniczenia końcowe

`build/final-ui-522560.json` dokumentuje **PASS** `soft3d_native` i `pollikmark_native` (rzeczywiste źródła, zegar/alokator/usługi hosta zastąpione w harnessie) oraz **PASS** powyższego ograniczonego QEMU w obu rozdzielczościach. Natywne sprawdzenia obejmują m.in. rotację i liczniki rasteryzacji, ABI, kształty/clipping/alpha, restart resize, kontrolowane błędy alokacji, zwalnianie oraz osiem wierszy UI przy 480×280. Nie są pomiarem szybkości docelowego jądra ani dowodem przejścia pełnych workloadów QEMU.

Pełny Run all, wszystkie poziomy geometrii/kształtów, ukończone trzy tryby Cube, pełna pamięć 1–32 MiB, wymuszone wyczerpanie RAM w końcowym QEMU, długi soak i sprzęt fizyczny: **NOT RUN** w tych artefaktach. Nie przepisujemy wcześniejszych wyników jądra 521336 B jako końcowego PASS. Ogólny raport UI ma dodatkowo zastrzeżenie o pierwotnym FAIL i późniejszej zmianie asercji WM; szczegóły w PERFORMANCE.md, nie bezwarunkowe „wszystkie testy PASS”.
