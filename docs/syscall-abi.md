# PollikOS syscall ABI

Status bieżący; źródłem prawdy o planie całości jest [`ARCHITECTURE.md`](../ARCHITECTURE.md).

## Odczyt wersji ABI

Oba targety udostępniają `PollikAbiInfo` z `include/pollikos_abi.h`. Aplikacja
ustawia `size` na pojemność bufora, a syscall zapisuje maksymalnie tyle bajtów,
ile rozumie kernel; zwraca liczbę zapisanych bajtów. Minimalny bufor ma 8 bajtów,
żeby zawierał rozmiar i wersję major/minor. Za mały bufor daje `-EINVAL`, a
niepoprawny wskaźnik `-EFAULT`. Pole `size` zawiera rozmiar struktury znany
kernelowi, również gdy bufor klienta jest krótszy.

Wersja 1.0 nie łączy numeracji ani konwencji wywołań targetów. `architecture`,
`transport` i `operation_namespace` identyfikują właściwy kontrakt. Dotychczasowe
numery pozostają bez zmian; zapytanie dostało nowy numer na każdym targetcie.
Maska `features` opisuje dostępne grupy API. Wspólny format odpowiedzi pozwala
SDK wykryć target i wersję bez udawania zgodności między i386 i x86_64.

## i386

- Wejście: `int 0x80`; liczba syscalla w `EAX`.
- Argumenty: `EBX`, `ECX`, `EDX`, `ESI`, `EDI` w tej kolejności.
- Wynik: `EAX`; błędy zwracane jako ujemne wartości `E*` z `kernel/syscall.h`.
- SDK: inline wrappers w `include/pollikos.h`.
- Dispatcher: `kernel/syscall.c::syscall_dispatch`; procesy ELF32 korzystają z `copy_from_user`, `copy_to_user`, `copy_string_from_user` i `user_range_valid` z VMM.
- Zapytanie wersji: `SYS_ABI_INFO` (`171`), wskaźnik struktury w `EBX`; publiczny wrapper `pollikos_get_abi_info`.

### Numery zadeklarowane w nagłówku

| Numer | Stała | Dispatcher i działanie |
| ---: | --- | --- |
| 1 | `SYS_EXIT` | Tak — kończy bieżący proces |
| 2 | `SYS_FORK` | Nie — obecnie kończy się `-ENOSYS` |
| 3 | `SYS_READ` | Tak — VFS read do sprawdzonego bufora user |
| 4 | `SYS_WRITE` | Tak — stdout/stderr lub VFS write |
| 5 | `SYS_OPEN` | Tak — kopiuje ścieżkę do bufora kernela, otwiera VFS |
| 6 | `SYS_CLOSE` | Tak — VFS fd lub specjalny zakres socketów |
| 11 | `SYS_SPAWN` | Tak — kopiuje ścieżkę i tworzy proces ELF |
| 12 | `SYS_EXEC` | Nie — obecnie `-ENOSYS` |
| 13 | `SYS_TIME` | Tak — czas systemowy/ticks |
| 19 | `SYS_SEEK` | Tak — przesunięcie VFS |
| 20 | `SYS_GETPID` | Tak |
| 24 | `SYS_YIELD` | Tak |
| 45 | `SYS_ALLOC` | Tak — rozszerza stertę procesu przez `process_sbrk` |
| 91 | `SYS_FREE` | Nie — obecnie `-ENOSYS` |
| 162 | `SYS_SLEEP` | Tak |
| 163–166 | `SYS_WAIT_EVENT`, `POLL_EVENT`, `SEND_IPC`, `RECV_IPC` | Tak — kolejki zdarzeń i komunikatów w `process.c` |
| 167–170 | `SYS_SOCKET`, `CONNECT`, `SEND`, `RECV` | Tak — ograniczony TCP userspace |

`SYS_SOCKET` zwraca slot z zakresu `100+`; tabela pozostaje globalna, ale dispatcher zapisuje PID właściciela i odrzuca operacje innego procesu. Reaper zamyka sockety właściciela przy zakończeniu procesu. Sloty pozostają odrębnym mechanizmem od deskryptorów plików i wymagają wspólnego Object/Handle Managera.

## x86_64

Target `kernel/arch/x86_64` ma osobną numerację syscalli i rejestry opisane w `USER_EXECUTION.md` oraz własne ABI plików. `USER_ABI_INFO` (`0x504f0000`) zwraca ten sam format opisu ABI; wskaźnik struktury trafia w `RDI`. ABI 1.0 x86_64 pozostaje niezależne od i386: wspólny opis wersji nie zmienia ich numeracji ani nie zapewnia zgodności binarnej.
