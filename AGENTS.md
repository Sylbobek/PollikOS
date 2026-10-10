# PollikOS — zasady rozwoju

## Zakres i architektura

- Docelowa architektura systemu to wyłącznie **x86_64**. Zachowaj autorski
  kernel PollikOS w C i NASM; nie zastępuj go gotowym jądrem.
- Nie dodawaj ponownie osobnego targetu i386 ani ELF32. Istniejący target i386
  pozostaje do osobnego etapu migracji; nie usuwaj go podczas przygotowania baseline.
- Zachowaj wymagany kod przejściowy BIOS: tryb rzeczywisty, protected mode
  oraz wejście do long mode. `bits 16`/`bits 32` nie oznacza osobnego systemu i386.
- Zachowuj działające funkcje kernela i aplikacji x86_64. Nie usuwaj wspólnego
  kodu, nagłówków, bibliotek ani narzędzi nadal wymaganych przez x86_64.
- Nie zmieniaj numerów syscalli ani publicznych struktur ABI bez wersjonowania.
  Źródła kontraktu: `include/pollikos_abi.h`, `kernel/arch/x86_64/user_abi.h`
  i publiczne nagłówki `sdk/include/pollikos/`.

## Ochrona danych i working tree

- Nigdy nie formatuj, nie usuwaj ani nie nadpisuj istniejących obrazów danych
  użytkownika, również obrazów `PollikData-system*.img` w katalogach builda.
- Testy wykonuj na nowo utworzonych obrazach tymczasowych, jednorazowych kopiach
  lub snapshotach. Sprawdź wszystkie ścieżki zapisu przed uruchomieniem narzędzia.
- Nie wykonuj destrukcyjnych operacji instalacji na fizycznym dysku.
- Normalny build x86_64 synchronizuje `PollikData-system.img`, a `-Production`
  również profil 30 GiB. Do baseline używaj izolowanej kopii źródeł bez obrazów
  użytkownika; szczegóły w `docs/migration/X64_BASELINE.md`.
- Na początku sprawdź git status i HEAD. Nie nadpisuj niezwiązanych zmian
  znajdujących się już w working tree. Nie resetuj ani nie stashuj ich automatycznie.
- Nie wykonuj automatycznego `git push`.

## Implementacja i weryfikacja

- Działaj autonomicznie; stosuj najmniejsze poprawne zmiany, testy regresji
  i kontrolowany refaktor. Krótki plan przygotuj przy złożonym lub ryzykownym zadaniu.
- Najpierw wyszukuj symbole i zależności przez `rg`; czytaj potrzebne fragmenty.
  Łącz powiązane operacje, ogranicz logi i nie analizuj ponownie niezmienionego kodu.
- Korzystaj przede wszystkim z kodu i dokumentacji repozytorium. Unikaj
  niepotrzebnych zależności zewnętrznych i researchu.
- Nie zastępuj działających funkcji atrapami ani fałszywymi wynikami.
- Naprawiaj przyczynę błędu. Nie osłabiaj testów ani zabezpieczeń dla uzyskania PASS.
- Każdą większą zmianę dokumentuj. Po każdym etapie wykonuj build i odpowiednie
  testy; przy zmianach kernela, ABI, pamięci, filesystemu lub bezpieczeństwa
  wykonuj pełny build x86_64 i stosowne regresje QEMU.
- Dla x86_64 sprawdzaj normalny build i SELFTEST. CI: `tools/ci.ps1 -Target x86_64`,
  rozszerzone regresje: `-Extended`; pamięć/boot: `tests/x86_64_boot.py`,
  storage: `tests/x86_64_storage.py`. Najpierw potwierdź izolację dysków.
- Oznaczaj wyniki **PASS / FAIL / NOT RUN**. Brak uruchomienia, nieaktualny log,
  uruchomiony lecz niezakończony proces ani sam marker częściowy nie stanowią PASS.
- Rozróżniaj analizę kodu, build, test natywny, QEMU i fizyczny sprzęt.
  Nie deklaruj parytetu funkcji ani bezpieczeństwa poza zweryfikowanym zakresem.
- Priorytety: poprawność > bezpieczeństwo > kompatybilność > wydajność
  > oszczędność tokenów. Pracuj tylko w zatwierdzonym zakresie bieżącego etapu.

## Raport

Po ukończeniu zadania podaj najwyżej cztery krótkie punkty: **Zrobione**, **Pliki**,
**Testy** (PASS / FAIL / NOT RUN), **Problemy** (rzeczywiste blokery).
Szczegóły dużych etapów zapisuj w dokumentacji zamiast w długiej odpowiedzi.
