# Terminal i administrator — 9 października 2026

Natywny terminal x86_64 używa tej samej powłoki `pollish`, co konsola szeregowa.
Komendy działają wewnątrz PollikOS. Katalog `commands` pokazuje 60 nazw komend
wbudowanych oraz rzeczywiste pliki wykonywalne w PATH; `help NAZWA` wyświetla
opis z tego samego rejestru. Aplikacje `.pol` można uruchamiać bez rozszerzenia,
np. `notes`, `browser` i `calculator`; `which` podaje odnalezioną ścieżkę.

## Przykłady

```
commands
help sudo
calc (2+3)*4
math sqrt(144)+max(3,7)
sysinfo
free
df
ps
date
whoami
id
devices
net
which browser
cp /home/note.txt /home/copy.txt
head -n 5 /home/note.txt
cat /home/note.txt | wc
find /home
fetch https://example.org
```

`sysinfo`, `ps`, `free` i `df` korzystają z nowych, wersjonowanych snapshotów
kernela. RAM oznacza pamięć fizyczną zarządzaną przez PMM. `df` rozdziela
pojemność nośnika od pojemności PollikFS; powiększony obraz 30 GiB nie staje
się automatycznie systemem plików o tej wielkości. Czas `date` pochodzi z RTC
i jest prezentowany jako UTC. `ps` pokazuje prawdziwe procesy, stan głównego
wątku i zarezerwowaną stertę brk, nie całkowity RSS procesu.

## Uprawnienia

```
sudo volume 50
sudo touch /etc/example
admin
id
echo configuration > /etc/example
volume 50
exit
```

`admin` lub `sudo -i` otwiera osobną powłokę z `USER_CAP_ADMIN_ALL`.
Kernel pyta o hasło, a zgoda jest przypisana do konkretnego programu
`/bin/pollish` i zużywana przy jednym spawn. Hasło obsługuje kernel;
nie trafia do historii poleceń. `sudo KOMENDA ARGUMENTY` wykonuje jedną
komendę. Zwykła powłoka zachowuje swoje prawa. Administrator przekazuje
swoje uprawnienia uruchamianym aplikacjom, a `exit` wraca do powłoki rodzica.
Prompt administratora kończy się `#`, zwykły `>`.

Operatory przekierowania są wykonywane przez bieżącą powłokę. Do zapisu w
chronionym katalogu przez `>` należy najpierw użyć `admin`; samo
`sudo echo ... > /etc/plik` nie podnosi praw powłoki wykonującej przekierowanie.
`cp` używa pliku tymczasowego i rename zamiast niszczenia celu przy błędzie
kopiowania. Zapisy przez już otwarty uchwyt także weryfikują bieżące prawa.
Zwykły proces nie może wysyłać sygnałów do procesu administratora ani innych
sesji. Tryb administratora zachowuje izolację pamięci kernel/userspace.

`volume`, `airplane`, `sound`, `reboot`, `shutdown` i `poweroff` są podłączone
do istniejących usług urządzeń. Brakujące sterowniki nie są emulowane przez
fałszywe sukcesy. `kill`, `sleep`, sesje `lock/logout/passwd`, dotychczasowe
komendy plikowe, środowisko, historia, potoki i przekierowania pozostają dostępne.

## Zakres i testy

To katalog wszystkich komend obecnie dostępnych w powłoce i PATH PollikOS,
nie deklaracja implementacji każdej komendy i opcji systemów Linux/Windows.
Nie dodano wieloużytkownikowego `useradd`, pakietów, montowania nieobsługiwanych
systemów plików ani automatycznego formatowania. i386 zachowuje własną powłokę
i dotychczasowe ABI; nowe snapshoty i rejestr dotyczą x86_64.

Test docelowy: `python tests/terminal_commands_x64.py`. Obejmuje arytmetykę,
snapshoty/nieprawidłowe wskaźniki, realne pliki/potoki, blokady zwykłego konta,
anulowanie i błędne hasło, `sudo`, zapis administratora, dziedziczenie praw,
powrót po `exit` oraz klawiaturę i pliki terminala GUI. Używa kopii dysku.
`sdk/tests/system_query.c` dodatkowo sprawdza blokadę sygnałów od ograniczonego
potomka do administratora. CI uruchamia ten test po normalnym buildzie.

Bufory odbiorcze TCP x86_64 są teraz stronami kernela przydzielanymi na
otwarcie i zwalnianymi na zamknięcie/abort. Pozwala to zachować osiem gniazd
bez 256 KiB statycznego BSS i bez naruszania granic bootstrapu SELFTEST.
Format PollikFS i dane użytkownika pozostają zgodne.
