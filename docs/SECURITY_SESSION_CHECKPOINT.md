# Kernel, konta i sesje — checkpoint 1–5

Zakres: poprawka zapisu PollikFS, poświadczenia i kontrola dostępu VFS,
wspólna obsługa kont, sesje oraz rozdzielenie stanów procesu/wątku x86_64.
Punkty 6–10 nie są częścią tego checkpointu.

## Kontrakty

- `pollikfs_write()` odrzuca cały zakres przekraczający `POLLIK2_MAX_FILE`
  przed alokacją i zmianą offsetu. Nieudany zapis zerobajtowy nie powiększa
  inode. Dispatchery sprawdzają także cały zapis przed podziałem na porcje.
- Poświadczenia procesu (`uid`, `gid`, identyfikator sesji) należą do kernela
  i są dziedziczone przy spawn. Zwykłe procesy nie otrzymują UID 0. Jedynie
  jawny target SELFTEST wykonuje uprzywilejowane regresje na obrazach testowych.
- VFS normalizuje komponenty ścieżki przed sprawdzeniem polityki. Użytkownik
  może czytać publiczne konfiguracje; `/etc/account.db` i wszystkie jego
  sufiksy pozostają prywatne. Zapis jest dozwolony w `/home`, `/tmp`,
  `/usr/src` i `/usr/lib`. Dwa ostatnie katalogi zachowują dotychczasową
  funkcję zapisywalnego sysrootu deweloperskiego TinyCC. Pozostałe katalogi
  systemowe są tylko do odczytu. Operacje rename sprawdzają obie ścieżki.
- Otwarte pliki zachowują ograniczenia również przy dziedziczeniu fd.
  Odczyt, zapis, stat deskryptora i seek ponownie sprawdzają prawa wywołującego.
- Oba targety używają `account.c`. Rekord ma nadal 128 bajtów. Wersja 2 używa
  Argon2id 1.3: 19 MiB, 2 przejścia, 1 lane, sól 16 B, wynik 32 B. Wersja 1
  pozostaje czytelna i jest zastępowana po poprawnej weryfikacji hasła.
  Nowy rekord jest zapisywany i odczytywany kontrolnie jako plik tymczasowy,
  następnie zastępuje poprzedni przez istniejący mechanizm rename-replace.
  Błędy I/O, uszkodzony rekord i brak konta na skonfigurowanym dysku blokują
  logowanie; nie uruchamiają formatowania ani automatycznego resetu.
- `lock`, `logout`, `passwd` są dostępne w obu terminalach. Hasła są zbierane
  przez interfejs kernela i czyszczone z pamięci. Nie trafiają do historii
  powłoki. Nieudane logowania mają opóźnienia; x86_64 kończy logowanie po
  pięciu nieudanych próbach i wymaga restartu.
- Lock wstrzymuje wykonywanie procesów sesji. x86_64 usuwa stare okna z
  ekranu, opróżnia kolejki wejścia i po poprawnym logowaniu przywraca sesję.
  Logout kończy jej procesy, sprząta zasoby i nadaje nowemu logowaniu nowy
  identyfikator. i386 zamyka także aplikacje GUI i czyści schowek, notatkę
  roboczą oraz bufor terminala.
- `Process64State` opisuje budowę, aktywność lub zakończenie procesu.
  `Thread64State` osobno opisuje stan wykonywania. Kontekst CPU, stosy,
  FPU, kolejka i accounting należą do `Process64.thread`; usunięto union
  udostępniający je jako pola procesu.

## Granice

To nadal jedno konto lokalne i polityka katalogów, bez wieloużytkownikowych
ACL/chmod per inode. Format PollikFS pozostał zgodny i nie szyfruje danych.
Rename-replace nie zastępuje transakcji odpornych na utratę zasilania.

RDRAND jest sprawdzany przez CPUID i używany, gdy dostępny. Na starym CPU
lub TCG sól pochodzi z puli SHA-256 zasilanej timingiem wejścia i TSC;
nie jest to certyfikowane źródło entropii sprzętowej. Argon2 wymaga dostępnych
19 MiB pamięci roboczej; brak pamięci daje błąd, bez słabszego KDF jako fallback.

Proces nadal ma jeden osadzony TCB. Nie dodano wielu wątków, TLS ani SMP.
Aplikacje GUI i386 pozostają w Ring 0; kontrole API nie chronią przed błędem
pamięci w takim kodzie. Punkty 6–10 wymagają osobnego etapu.

## Walidacja

- `python tests/security_account_native.py`: realny kod PollikFS/VFS/kont
  na kopii obrazu, zapis poza zakresem, ENOSPC, odziedziczony fd, odwołanie
  sesji, wektor RFC Argon2id, migracja starego hasha, zmiana hasła i OOM.
- `python tests/pollikfs_read_native.py`: istniejąca regresja odczytu.
- Build i386 `build.ps1 -NoSync`; build x86_64 normalny i SELFTEST.
- QEMU x86_64 SELFTEST, 64 MiB: pełny zestaw wbudowanych testów zakończony
  `[X64] SELFTEST PASS`, w tym natywny TinyCC i rozdzielone stany TCB.
- `python tests/x86_64_security_session.py`: prawdziwy Ring 3, ochrona konta,
  dziedziczenie sesji, lock/błędne hasło/unlock, passwd, logout i kolejny boot.
- `python tests/x86_64_console.py --timeout 300`: istniejąca regresja powłoki,
  plików, potoków, sygnałów i natywnego kompilowania C — `CONSOLE PASS`.
- i386 QEMU: tworzenie konta Argon2id i 100 cykli Ring 3 z zachowanym bilansem PMM.
- `python tests/i386_security_account.py`: hasło utworzone przez i386 jest
  także weryfikowane przez implementację natywną sprawdzoną wektorem RFC.

Testy modyfikują wyłącznie obrazy jednorazowe. Build i386 wykonany z `-NoSync`
nie zmieniał `build/PollikData.img`. Nie wykonywano testów fizycznego sprzętu
ani pełnej macierzy wszystkich rozdzielczości i konfiguracji pamięci.
