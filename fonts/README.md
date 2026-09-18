# Pollik Sans

Autorska czcionka interfejsu PollikOS: geometryczne kształty, zaokrąglone zakończenia, małe i wielkie litery. Kontury zapisano jako własne ścieżki M/L/Q/C w `build_font.py`. Nie użyto czcionek systemowych, SF Pro ani innych zewnętrznych fontów.

Generator rasteruje krzywe z ośmiokrotnym nadpróbkowaniem do pięciu natywnych rozmiarów. Kernel odczytuje 16 poziomów pokrycia i miesza kolor litery z rzeczywistym tłem. Maski są częścią kernela, dlatego czytelny tekst działa od startu bez systemu plików i bez biblioteki renderującej fonty.

Zakres: 95 znaków ASCII; 14/18/26/40/52 px. Jest to format czcionki wbudowanej w PollikOS, a nie instalowalny plik TTF/OTF.

Regeneracja: `python fonts/build_font.py` z zainstalowanym Pillow. Powstają `kernel/font_data.h` i próbka `fonts/PollikSans.png`.
