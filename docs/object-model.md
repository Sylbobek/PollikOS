# Model obiektów PollikOS

Obecny stan i plan migracji są częścią źródła prawdy: [`ARCHITECTURE.md`](../ARCHITECTURE.md).

## Stan kodu

- `ProcessControlBlock` w `kernel/process.h` opisuje PID/PPID, stan, katalog stron, stosy, fd table, kolejkę zdarzeń i kolejkę IPC.
- `vfs_file_t` jest współdzielonym opisem otwartego pliku z `ref_count`; wskaźniki do niego są przechowywane w deskryptorach procesu.
- Sockety syscall mają globalną tablicę slotów w `kernel/syscall.c`, zakres `100+` i PID właściciela. `CONNECT`, `SEND`, `RECV` i `CLOSE` sprawdzają właściciela, a reaper procesu zamyka jego sockety przed zwolnieniem PCB. Sloty nie korzystają jeszcze ze wspólnego handle table.
- Mutexy/semafory/eventy jako ogólne obiekty jądra, wspólny namespace, access maski, security descriptors i uchwyty z generacją nie istnieją.

Nazwy struktur nie oznaczają kompletnego modelu obiektowego. Sockety mają sprawdzanego właściciela, ale fd plików i socket sloty nadal są osobnymi mechanizmami bez wspólnych praw dostępu i formatu uchwytów.

## Docelowy kontrakt (projekt)

Nowy typ obiektu powinien definiować typ, niepowtarzalny handle, prawa operacji, właściciela, licznik referencji i deterministyczny lifecycle. Pobranie obiektu z handle table sprawdza typ i prawa przed dostępem; zamknięcie usuwa wpis i zwalnia obiekt dopiero po zejściu referencji. PID ani surowy wskaźnik kernela nie są handle.

Migracja powinna najpierw dodać testowany handle table dla istniejących VFS open descriptions, a następnie przenosić sockety, eventy i urządzenia. Do czasu migracji obecne fd i socket slots pozostają odrębnymi mechanizmami i nie należy deklarować ich jako zabezpieczone.
