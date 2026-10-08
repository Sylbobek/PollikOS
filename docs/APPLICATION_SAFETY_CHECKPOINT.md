# Ochrona obrazów i aplikacji — checkpoint

Hostowe narzędzia instalacji/synchronizacji odmawiają zapisu obrazu z oczekującym,
uszkodzonym albo nieznanym prefiksem dziennika. Nie kasują go, nie formatują dysku
i nie wykonują recovery za kernel. `PollikFsImage.save` ponownie sprawdza prefiks
przed zastąpieniem pliku, używa unikalnego pliku tymczasowego i atomowego replace.
Pozostałe narzędzia synchronizacji mają własny rollback zapisu; nie deklarujemy
atomowości wszystkich narzędzi przy utracie zasilania hosta.

Notes zachowuje źródło przy błędzie zapisu, oferuje Save As i undo/redo,
obsługuje edycję UTF-8 oraz blokuje zapis obciętego/nieprawidłowego odczytu.
Limit edytora wynosi 8 191 bajtów. Files pokazuje pełną dynamiczną listę,
obsługuje tworzenie katalogów, rename, kopiowanie plików, cut/move, kosz i restore
z kontrolą konfliktów. Kopiowanie całych katalogów jest jawnie nieobsługiwane.

Wspólne fonty x86_64 zawierają polskie znaki; i386 zachowuje swój zestaw ASCII
i budżet pamięci. Format PollikFS nie zmienił się.

Dowody i testy: `tests/host_journal.py`, `tests/system_file_sync.py`,
`tests/utf8_ui_native.py`, `sdk/tests/notes_io.c`, `sdk/tests/files_io.c`
i `tests/x86_64_app_checkpoint.py`. Testy gościa pracują na kopiach dysku.
Zaliczenie komponentu I/O nie jest równoznaczne z zaliczeniem wszystkich testów GUI.
Przeglądarka używa standardowych praw aplikacji; aktualny zakres opisuje
[native browser checkpoint](NATIVE_BROWSER_CHECKPOINT.md).
