# PollikOS — końcowe pomiary GUI (Surface)

Stan dokumentacji: 2026-09-18. Główne tabele zestawiają istniejące artefakty, bez ponownego benchmarku. Po ich opracowaniu dodatkowo uruchomiono standardowy `tests/surface_bounds.py` przy domyślnych **3440×1440**: **PASS**, wszystkie siedem powierzchni maximize/restore, runtime LFB i zewnętrzne guardy, na tym samym jądrze 522560 B z potwierdzoną zgodnością ELF. Log: `build/surface-3440x1440.log`. Ten dodatkowy test nie obejmuje opcji `--wm` ani pomiaru FPS i nie należy do wcześniejszych manifestów. Dodano też `.vscode/c_cpp_properties.json` dla IntelliSense Clang i386; diagnostyka wcześniej zgłaszanych nagłówków/`__asm__` została usunięta bez zmian jądra.

**Wniosek:** w zapisanym scenariuszu drag/resize obserwowano około **20 FPS**, z narzutem sterowania i odczytów QMP. Nie jest to dowód 60/120 FPS. Podczas drag liczniki paintów klientów i czasu paint wynoszą dokładnie zero w obu rozdzielczościach. Brak porównywalnego, zweryfikowanego BEFORE — nie wyliczamy przyspieszenia.

## 1. Tożsamość i środowisko

| Pole | Wartość zapisana w artefaktach |
|---|---|
| Obraz | `build/PollikOS-Surface.img`, `snapshot=on` |
| Jądro | **522560 / 524288 B; zapas 1728 B** |
| SHA-256 jądra | `64756ee53d2455ceae00d3e37717d65710377a83a3a7f4560300fb125e7c574b` |
| SHA-256 ELF | `fd5fcba0ebf98accf8094b650ceace136197bb2a9e24eeb5441769bb1249ea96` |
| Zgodność obrazu | Bajty ładowane z ELF porównane z obrazem od offsetu 4608; nie tylko daty plików |
| QEMU | `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`, maszyna `pc`, TCG, CPU `max` |
| RAM / grafika | 256 MiB; VGA, 32 MiB VRAM; 1024×768 oraz 1920×1080 |
| Sieć / dysk danych pomiarów GUI | `-nic none`; tymczasowy dysk 64 MiB, nie użytkowy `PollikData.img` |
| Host / Python | `Windows-10-10.0.26200-SP0` / `3.11.9`; model CPU hosta i jego obciążenie niezapisane |
| Obserwator | PS/2 przez QMP/HMP, odczyty pamięci, zatrzymania vCPU na granicach etapów; narzut wliczony |

Źródła: `build/benchmark-{1024x768,1920x1080}.json`, `build/perf-{1024x768,1920x1080}.json`, `build/pollikmark-{1024x768,1920x1080}.json`, `build/final-522560-tests.json`, `build/final-ui-522560.json`. Wszystkie te pomiary wskazują powyższe jądro. Wyników wcześniejszego jądra 521336 B nie przenosimy do tabel AFTER. Pełna metodologia i ograniczone wyniki PollikMark: [POLLIKMARK.md](POLLIKMARK.md).

## 2. BEFORE i DIFFERENCE

| Historyczna deklaracja BEFORE | Wartość ze starej dokumentacji | Status |
|---|---:|---|
| Średni FPS | 15 | **NOT VERIFIED** |
| Średnia klatka | 66.3 ms | **NOT VERIFIED** |
| Paint | 100.0 ms | **NOT VERIFIED** |
| Compose / present | około 0.5 ms | **NOT VERIFIED** |
| Full redraws | 54 | **NOT VERIFIED** |
| Drag / resize | około 612 / 616 pakietów | **NOT VERIFIED** |

**DIFFERENCE: not comparable.** Stara instrumentacja i scenariusz nie stanowią poprawnej bazy porównawczej (tak samo oznaczają to JSON-y). Nie ma procentowej poprawy, mnożnika szybkości ani potwierdzonej przyczynowej diagnozy BEFORE. Historyczne twierdzenia o każdym renderowanym znaku, 16 testach narożnika na piksel czy braku backlogu nie są pomiarami tego przebiegu. Obecne JSON-y nie zawierają osobnego licznika rasteryzowanych glifów ani pomiaru opóźnienia wejście–obraz.

## 3. Scenariusz i definicje metryk

`tests/benchmark_gui.py` korzysta z `tests/gui_metrics.py`:

1. Otwiera Welcome, Files, Terminal, Notes i Settings; potwierdza pięć widocznych okien. Następnie przez 10 s porusza tylko kursorem. Etap `five_windows_10s` obejmuje także otwieranie okien, nie ciągły repaint pięciu klientów.
2. Przez około 10 s przeciąga Terminal z trzymanym przyciskiem, a następnie przez około 10 s zmienia jego rozmiar. Sprawdza capture, geometrię, skonsumowanie ruchu oraz piksel powierzchni, sceny i runtime LFB. Nie wysyła kolejnej porcji w ciemno.
3. Wykonuje 20 rzeczywistych cykli maximize/restore zielonym przyciskiem, z weryfikacją stanu i geometrii.
4. Przez około 3 s przechodzi między różnymi ikonami docka (odstęp 68 px). Rejestr zawiera siedem aplikacji; trafienia indeksów 2, 3 i 4 są zapisane jako `dock_hit_proof`.
5. Pobiera podsumowanie terminalowego `perf`; źródłem tabel etapów są jednak różnice liczników, nie pojedynczy odczyt ekranu terminala.

`actual_fps` (actualFPS) = liczba ukończonych klatek etapu / `duration_guest_s`. Średni paint, compose, present i TOTAL to odpowiednie sumy czasu / liczba klatek **całego etapu**, także klatek bez paintu klienta. TOTAL = paint + compose + present; nie jest czasem ściennym ani odstępem prezentacji. `render_throughput_fps` = klatki × 1000000 / suma TOTAL w µs: odwrotność średniego kosztu pracy renderera, **nie obserwowany FPS GUI**.

Czas hosta i czas gościa mają różne granice odczytu i narzut obserwatora. Są podane osobno; nie korygowano ich arbitralnie. Jedna operacja skryptu może generować kilka klatek i wiele pakietów PS/2. GUI jest zdarzeniowe, więc klatki kursora/docka nie dowodzą równie szybkiego pełnego repaintu. PASS oznacza spełnienie asercji, nie osiągnięcie docelowego FPS.

## 4. AFTER — rzeczywiste średnie całych etapów

Czasy w **µs na ukończoną klatkę**. Wartości zaokrąglone do 3 miejsc; TOTAL obliczony z `counts.total_time_us / counts.frame_count`.

| Rozdzielczość | Etap | actualFPS | Paint µs | Compose µs | Present µs | TOTAL µs |
|---|---|---:|---:|---:|---:|---:|
| 1024×768 | pięć okien + kursor | 37.772 | 102.992 | 1610.447 | 85.429 | 1798.868 |
| 1024×768 | drag | 19.590 | 0.000 | 11492.270 | 3730.577 | 15222.847 |
| 1024×768 | resize | 19.850 | 1242.793 | 10125.747 | 3039.505 | 14408.045 |
| 1024×768 | maximize/restore | 21.655 | 2724.313 | 16623.506 | 2135.253 | 21483.072 |
| 1024×768 | dock hover | 33.078 | 0.000 | 2409.030 | 189.919 | 2598.949 |
| 1920×1080 | pięć okien + kursor | 37.324 | 42.477 | 1549.276 | 164.365 | 1756.117 |
| 1920×1080 | drag | 19.746 | 0.000 | 16184.217 | 4794.010 | 20978.227 |
| 1920×1080 | resize | 19.474 | 1485.199 | 11845.372 | 5440.000 | 18770.571 |
| 1920×1080 | maximize/restore | 17.685 | 2580.410 | 23834.289 | 5820.627 | 32235.325 |
| 1920×1080 | dock hover | 34.808 | 0.000 | 2519.295 | 176.429 | 2695.724 |

### Czasy i surowe liczniki etapów

Skróty: F = `frame_count`, pełne = `full_redraw_count`, rect = `damage_rects_count`, K = `cursor_frames`, D = `dock_frames`, P = `client_paint_count`. Operacje oznaczają potwierdzone ruchy/zmiany hover, a dla maximize/restore całe cykle.

| Rozdzielczość / etap | Host s | Gość s | Operacje | F | Pełne | Rect | K | D | P |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 / pięć okien | 10.484 | 10.430938 | 383 | 394 | 7 | 401 | 387 | 0 | 11 |
| 1024 / drag | 10.063 | 10.005317 | 195 | 196 | 0 | 392 | 0 | 0 | **0** |
| 1024 / resize | 10.032 | 9.974930 | 197 | 198 | 1 | 396 | 0 | 0 | 197 |
| 1024 / maximize | 7.672 | 7.665700 | 20 | 166 | 81 | 247 | 85 | 0 | 80 |
| 1024 / dock | 3.016 | 2.992887 | 50 | 99 | 0 | 198 | 0 | 99 | 0 |
| 1920 / pięć okien | 10.484 | 10.502681 | 374 | 392 | 6 | 398 | 386 | 0 | 11 |
| 1920 / drag | 10.046 | 10.027230 | 197 | 198 | 0 | 396 | 0 | 0 | **0** |
| 1920 / resize | 10.046 | 10.064476 | 195 | 196 | 1 | 392 | 0 | 0 | 195 |
| 1920 / maximize | 9.359 | 9.386481 | 20 | 166 | 81 | 247 | 85 | 0 | 80 |
| 1920 / dock | 3.016 | 3.016571 | 53 | 105 | 0 | 210 | 0 | 105 | 0 |

| Rozdzielczość / etap | Suma paint µs | Suma compose µs | Suma present µs | Suma TOTAL µs | Piksele composed | Piksele presented |
|---|---:|---:|---:|---:|---:|---:|
| 1024 / pięć okien | 40579 | 634516 | 33659 | 708754 | 6014072 | 6178660 |
| 1024 / drag | **0** | 2252485 | 731193 | 2983678 | 59745552 | 59492320 |
| 1024 / resize | 246073 | 2004898 | 601822 | 2852793 | 60847508 | 65644742 |
| 1024 / maximize | 452236 | 2759502 | 354452 | 3566190 | 63915464 | 64625874 |
| 1024 / dock | 0 | 238494 | 18802 | 257296 | 9028008 | 8900100 |
| 1920 / pięć okien | 16651 | 607316 | 64431 | 688398 | 12948064 | 13161852 |
| 1920 / drag | **0** | 3204475 | 949214 | 4153689 | 60355296 | 60099480 |
| 1920 / resize | 291099 | 2321693 | 1066240 | 3679032 | 61524932 | 66273450 |
| 1920 / maximize | 428348 | 3956492 | 966224 | 5351064 | 168176072 | 168886482 |
| 1920 / dock | 0 | 264526 | 18525 | 283051 | 9575160 | 9439500 |

To bezpośrednio potwierdza zero paintów klientów w zmierzonym drag, nie ogólną obietnicę zerowego kosztu GUI. Compose i present pozostają znaczące. Pełny benchmark trwał według własnego JSON-a 43.234 / 45.516 s; zewnętrzny runner raportuje 43.313 / 45.594 s (inny zakres obejmujący uruchomienie procesu).

## 5. Historia, p95, p99 i 1% low

`frame_history` opisuje koszt wykonania klatki; `interval_history` odstępy między klatkami. Oba bufory obejmują **ostatnie maksymalnie 128 próbek**, nie cały etap i mogą zawierać poprzedni etap. Szczególnie historia docka (99/105 nowych klatek) obejmuje też wcześniejszy maximize/restore. Średniej historii nie wolno zastępować średnią z sum etapowych ani odwrotnie.

Percentyle używają uporządkowanej próbki i nearest-rank: element `ceil(p × n)`. 1% low = 1000000 / średnia najwolniejszych `ceil(n/100)` czasów w µs; nie odwrotność p99 ani średnia najniższych FPS. Dla 128 próbek to zaledwie dwa czasy. `frame_history.low_1pct_fps` jest odwrotnością kosztu pracy, nie płynnością prezentacji. Do tej ostatniej bliższe jest low z `interval_history`, nadal z narzutem QMP.

W każdej komórce poniżej: **p95 / p99 µs; 1% low FPS**. Każdy bufor ma tu n=128. Surowe tablice oraz mean/min/max są zachowane w `stages[].stats.duration_history` i `interval_history` oraz odpowiadających im podsumowaniach JSON.

| Rozdzielczość / etap | Koszt klatki: p95 / p99; low | Odstępy: p95 / p99; low |
|---|---:|---:|
| 1024 / pięć okien | 1413 / 1795; 444.840 | 26733 / 42915; 18.323 |
| 1024 / drag | 26523 / 29210; 33.662 | 75147 / 96105; 10.251 |
| 1024 / resize | 26811 / 43382; 21.302 | 69955 / 93015; 10.404 |
| 1024 / maximize | 52512 / 70413; 13.883 | 74851 / 77680; 12.653 |
| 1024 / dock | 32437 / 49338; 17.628 | 56238 / 66607; 14.744 |
| 1920 / pięć okien | 1373 / 1472; 676.590 | 26503 / 51971; 18.857 |
| 1920 / drag | 27871 / 30921; 26.960 | 69936 / 82834; 11.360 |
| 1920 / resize | 27461 / 30629; 25.953 | 70543 / 112495; 8.771 |
| 1920 / maximize | 77417 / 97417; 10.205 | 100001 / 103002; 9.474 |
| 1920 / dock | 5141 / 76603; 12.804 | 52916 / 100001; 9.852 |

### Osobne krótkie sprawdzenie telemetrii `perf-*.json`

To nie dodatkowe etapy powyższego benchmarku. `duration_s` hosta wynosi 0.875 / 1.078 s, podczas gdy `stats.elapsed_us` wynosi 1018079 / 1349486. `stats.fps` to okresowe wskazanie telemetrii, nie `actual_fps` powyższego scenariusza. Źródło zegara: `clock_source=1`, rozdzielczość raportowana 1 µs; TSC 2510217 / 2495971 kHz. Jest to deklarowana ziarnistość zegara, nie dowód dokładności pomiaru 1 µs.

| Rozdzielczość | Klatki / odstępy | `stats.fps` | Średni koszt µs | p95 / p99 kosztu µs | Średni odstęp µs | p95 / p99 odstępu µs | Low kosztu / low odstępów |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1024×768 | 25 / 24 | 23 | 15796.76 | 49424 / 124793 | 35162 | 70727 / 73367 | 8.013 / 13 |
| 1920×1080 | 32 / 31 | 24 | 17898.00 | 75797 / 205150 | 33674 | 93843 / 100644 | 4.874 / 9 |

Low kosztu pochodzi z hostowego `history.low_1pct_fps`; low odstępów jest całkowitym `stats.low_1pct_fps`. Ich różnica jest zamierzona. Zera nieodświeżonych pól podsumowania `stats` w benchmarku nie oznaczają zerowych percentyli — rozkłady host oblicza z surowych tablic.

| Rozdzielczość | Suma paint / compose / present / TOTAL µs | Pełne / rect / K / D / P | Piksele composed / presented |
|---|---:|---:|---:|
| 1024×768 | 10201 / 336368 / 48350 / 394919 | 8 / 33 / 17 / 0 / 9 | 6323756 / 6348258 |
| 1920×1080 | 46292 / 425900 / 100544 / 572736 | 7 / 39 / 25 / 0 / 8 | 14556544 / 14634466 |

Zapisany zakres asercji: clock, cache kursora, kopiowanie, surface+scene+LFB, księgowanie czasu, kolejność percentyli. Próbki zawierają koszt uruchomienia i działań kontrolnych; nie są ustabilizowanym benchmarkiem samego drag.

## 6. Końcowe testy — wyłącznie zapisane wykonania

Poniższe PASS pochodzą z istniejących raportów, nie z uruchomienia testów podczas pisania dokumentacji. Testy natywne korzystają z rzeczywistych źródeł i stubów usług, **nie uruchamiają obrazu jądra**.

| Zestaw / zakres | Status i źródło |
|---|---|
| Benchmark GUI, perf — 1024×768 i 1920×1080 | **PASS**, `final-522560-tests.json` |
| Boot z 64 i 256 MiB RAM | **PASS**, `final-522560-tests.json`; nie pełny test pamięci PollikMark |
| Browser JS, e2e, responsive 1024×768 | **PASS**, `final-522560-tests.json` |
| Native: gui_registry, held_drag, app_layout, browser_cooperative, soft3d_native, pollikmark_native | **PASS**, `final-ui-522560.json` |
| Browser responsive, corners_guards, resize_layout, PollikMark QEMU — obie rozdzielczości | **PASS**, `final-ui-522560.json`; PollikMark to ograniczony scenariusz, nie Run all |
| Standardowy surface_bounds, bez `--wm` — obie rozdzielczości | **PASS**, `final-ui-522560.json`; także ponowne sprawdzenie niezmienionych standardowych kryteriów z kodami wyjścia 0/0 |
| Dodatkowy surface_bounds `--wm` — obie rozdzielczości | **Pierwotnie FAIL; później PASS po zmianie asercji**, patrz zastrzeżenie poniżej |
| Smoke — domyślne 1024×768 | **PASS**, `final-ui-522560.json` |
| Smoke 1920×1080 | **NOT RUN** — skrypt bez parametru rozdzielczości |
| PollikMark pełny Run all, pełna drabina pamięci 1–32 MiB, pomiary wszystkich obciążeń | **NOT RUN** w zapisanym końcowym QEMU |
| Standardowy surface_bounds — dodatkowe 3440×1440 | **PASS**, późniejszy przebieg opisany na początku dokumentu; bez `--wm` i benchmarku FPS |
| Inne niewymienione konfiguracje, sprzęt fizyczny, VSync, próba długotrwała, pozostałe testy niewymienione w raportach | **NOT RUN** dla tego końcowego zestawienia |

**Zastrzeżenie WM:** status całego raportu UI brzmi `PASS_REQUESTED_SUITE_WITH_WM_ASSERTION_CHANGE_CAVEAT`, nie bezwarunkowe „wszystko PASS”. Pierwotny dodatkowy test oczekiwał MAXIMIZED po minimalizacji; obserwowano MINIMIZED. Późniejsza asercja uwzględnia stan MINIMIZED i zachowaną geometrię oraz stan sprzed minimalizacji. Jej PASS **nie jest PASS pierwotnego kryterium**. Zachowano `build/final-ui-522560/initial-attempt.json` oraz katalogi `initial-surface_bounds-wm-*`; raport wskazuje brak zmian jądra.

Raport `final-522560-tests.json` oznacza browser responsive 1920 jako NOT RUN w tamtym przebiegu (ówczesny skrypt bez argumentu rozdzielczości). Końcowy raport UI dokumentuje późniejsze rzeczywiste wykonanie obu rozdzielczości po rozszerzeniu skryptu; nie należy powielać wcześniejszego NOT RUN jako stanu końcowego.

Raport UI zapisuje zmiany harnessów: parametr rozdzielczości browser responsive, snapshot obrazu w smoke i powyższą asercję WM. Raporty zachowują tożsamość jądra/ELF; nie traktujemy modyfikacji testu jako optymalizacji produkcyjnej. Metadane `PollikData.img` pozostały niezmienione według raportu, a testy UI używały dysków tymczasowych. To ograniczona obserwacja, nie globalny dowód braku wycieków lub uszkodzeń w każdej sytuacji.

