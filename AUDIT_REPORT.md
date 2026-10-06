# Audyt PollikOS

**Data:** 2026-10-05  
**Zakres:** rewizja `66d5a7b9027be78fae4d5b3c68d3477331177d48`. W drzewie były również niezacommitowane zmiany kalkulatora i testów; nie są częścią ocenianej bazy.  
**Metoda:** statyczny przegląd ścieżek systemu plików, syscalli, loaderów ELF, kopiowania danych użytkownika, logowania, kompilacji i przeglądarki. To nie jest pełny formalny przegląd każdego modułu ani certyfikacja bezpieczeństwa.

## Ocena ogólna

PollikOS jest eksperymentalnym systemem Alpha z działającym środowiskiem i386 oraz rozwijanym celem x86_64. Kod zawiera sensowne mechanizmy ochrony wskaźników użytkownika i ochrony obrazu danych podczas kompilacji. Obecny stan nie powinien być traktowany jako system do przechowywania danych wrażliwych ani do pracy z niezaufanymi programami lub treściami sieciowymi.

## Ustalenia

### P1 — Nieudany zapis poza zakresem może trwale uszkodzić inode

W `pollikfs_write()` po przerwaniu pętli z powodu przekroczenia limitu bloków kod nadal podnosi `node.size` do bieżącego offsetu i zapisuje inode. Dopiero później zwraca błąd zapisu. `pollikfs_seek()` dopuszcza offset do `0x7fffffff`, znacznie większy od limitu pliku PollikFS; `read_inode()` odrzuca potem taki rozmiar jako uszkodzony. W rezultacie pojedynczy zapis może sprawić, że istniejący plik przestanie się otwierać.

Przykładowa sekwencja syscalli: otworzyć plik do zapisu, wykonać seek do `0x7fffffff`, a następnie zapisać jeden bajt. To jest dostępne z Ring 3. PollikFS nie ma uprawnień per plik, więc proces może wskazać cudzy plik.

Źródła: [pollikfs.c:761](kernel/pollikfs.c#L761), [pollikfs.c:767](kernel/pollikfs.c#L767), [pollikfs.c:839](kernel/pollikfs.c#L839), [pollikfs.c:843](kernel/pollikfs.c#L843), [pollikfs.c:848](kernel/pollikfs.c#L848), [pollikfs.c:99](kernel/pollikfs.c#L99), [pollikfs.c:869](kernel/pollikfs.c#L869), [pollikfs.h:23](kernel/pollikfs.h#L23).

**Zalecenie:** przed modyfikacją inode odrzucać zapis, dla którego zakres przekracza limit pliku; po zapisie zerobajtowym nie utrwalać nowego rozmiaru. Dodać regresję sprawdzającą wynik na kopii obrazu dysku.

### P1 — Przeglądarka i aplikacje pulpitu działają z uprawnieniami kernela

Dokumentacja potwierdza, że aplikacje pulpitu nadal działają w jądrze. Build i386 kompiluje parser HTML, CSS, JavaScript oraz klienta przeglądarki do obrazu kernela. Przeglądarka pobiera treści z sieci, więc błędne parsowanie niezaufanych danych może uszkodzić stan kernela lub zatrzymać cały system. To ryzyko architektoniczne; podczas tego audytu nie potwierdzono konkretnego exploita.

Źródła: [README.md:78](README.md#L78), [build.ps1:53](build.ps1#L53), [build.ps1:59](build.ps1#L59), [build.ps1:76](build.ps1#L76).

**Zalecenie:** przenieść przeglądarkę i pozostałe aplikacje do Ring 3 z ograniczonym ABI. Do czasu izolacji traktować przetwarzanie niezaufanych stron jako operację mogącą zagrozić stabilności całego systemu.

### P2 — Logowanie nie izoluje plików, a hash jest tani do zgadywania offline

PollikFS nie przechowuje właścicieli ani bitów uprawnień; dokumentacja wprost zaznacza, że logowanie chroni ekran sesji, ale nie jest granicą uprawnień do plików. Proces użytkownika może korzystać z syscalli plikowych bez kontroli właściciela. Dodatkowo hasło jest przetwarzane 8192 razy zwykłym SHA-256, a sól opiera się na RTC, liczniku ticków i wejściu użytkownika zamiast źródła kryptograficznej losowości. Kopia obrazu z rekordem konta umożliwia atak słownikowy offline.

Źródła: [INSTALLATION.md:15](INSTALLATION.md#L15), [auth.c:15](kernel/auth.c#L15), [auth.c:118](kernel/auth.c#L118), [auth.c:125](kernel/auth.c#L125).

**Zalecenie:** nie przedstawiać logowania jako ochrony danych. Przed obsługą wielu użytkowników dodać sprawdzanie uprawnień do VFS/PollikFS oraz kryptograficzne źródło soli i wolniejszy, najlepiej pamięciochłonny KDF.

### P2 — Operacje PollikFS nie są transakcyjne przy awarii zasilania

Zapisy aktualizują bloki danych, tablice pośrednie, inode i superblok bez dziennika transakcji ani ścieżki odtwarzania. Usuwanie najpierw kasuje wpis katalogowy, a później zwalnia bloki i inode. Przerwanie pracy pomiędzy tymi zapisami może zostawić osierocone bloki, częściowy plik albo utracony wpis. Testy czystego restartu nie dowodzą odporności na nagłą utratę zasilania.

Źródła: [pollikfs.c:734](kernel/pollikfs.c#L734), [pollikfs.c:843](kernel/pollikfs.c#L843), [pollikfs.c:535](kernel/pollikfs.c#L535), [pollikfs.c:1078](kernel/pollikfs.c#L1078), [pollikfs.c:1086](kernel/pollikfs.c#L1086).

**Zalecenie:** zaprojektować dziennik lub copy-on-write dla metadanych i dodać testy awarii pomiędzy kolejnymi zapisami na kopiach obrazów.

### P2 — System plików używa tylko małej części 10-GiB obrazu danych

Obraz `PollikData.img` ma logiczny rozmiar 10 GiB, ale geometria PollikFS jest na stałe ograniczona do 32 768 bloków po 1 KiB, czyli 32 MiB łącznie z metadanymi. Użyteczna przestrzeń na pliki jest jeszcze mniejsza. Pozostała pojemność obrazu nie jest dostępna przez bieżący format PollikFS.

Źródła: [README.md:34](README.md#L34), [build.ps1:181](build.ps1#L181), [pollikfs.h:12](kernel/pollikfs.h#L12), [pollikfs.h:17](kernel/pollikfs.h#L17).

**Zalecenie:** jasno pokazać faktyczną pojemność użytkową albo rozszerzyć format i migrację w sposób zachowujący zgodność z istniejącymi dyskami.

### P2 — Brak automatycznej walidacji zmian przy każdym commicie

Repozytorium nie zawiera konfiguracji CI. ROADMAP odnotowuje ostatni udokumentowany test `smoke.py` jako PASS z 2026-09-18. W tym audycie nie uruchamiano kompilacji ani QEMU, więc bieżący wynik runtime pozostaje niezweryfikowany.

Źródła: [ROADMAP.md:250](ROADMAP.md#L250), [README.md:88](README.md#L88).

**Zalecenie:** dodać CI dla kompilacji i szybkich testów PollikFS/ABI; pełniejsze testy QEMU uruchamiać jako osobny etap. Testy sprzętowe nadal wymagają osobnego planu — dokumentacja nie deklaruje walidacji na fizycznym sprzęcie.

## Dobre zabezpieczenia już obecne

- Build odmawia automatycznego formatowania istniejącego dysku danych i zachowuje kopię przy jawnej migracji/formatowaniu.
- i386 waliduje zakres wskaźników użytkownika i uprawnienia stron; x86_64 kopiuje dane przez supervisor aperture po walidacji całego zakresu.
- Loader ELF64 ogranicza obszar i rozmiar obrazu oraz wymusza W^X.
- W repozytorium jest zestaw testów QEMU obejmujący rozruch, procesy, sieć, przeglądarkę i zapis plików; aktualność jego wyników wymaga odświeżenia.

## Walidacja tego raportu

`git diff --check` zakończył się kodem 0. Nie uruchamiano buildów, testów ani QEMU. Audyt obejmuje analizę statyczną źródeł w stanie wskazanym na początku dokumentu; fizyczny sprzęt nie był testowany.

