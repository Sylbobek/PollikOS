# PollikOS Driver API

Opisuje faktycznie istniejące styki. Główny status architektury: [`ARCHITECTURE.md`](../ARCHITECTURE.md).

## Dostępne dziś

### HAL port-I/O (pierwszy krok fazy 1)

`kernel/hal.h` udostępnia `hal_port_read8/16/32` i `hal_port_write8/16/32`; instrukcje x86 są w `kernel/hal.c`. `system.h` zachowuje `inb/outb/inw/outw/inl/outl` jako adaptery, dzięki czemu obecne sterowniki nie zmieniają zachowania podczas tej granicy.

Ten kontrakt abstrahuje wyłącznie instrukcje port-I/O. Nie wykrywa urządzeń, nie zarządza ich cyklem życia i nie jest jeszcze neutralnym transportem dla MMIO, DMA, przerwań ani ACPI.

### Konkretne interfejsy sterowników

- Sieć: `NetworkInterface` (`kernel/net/net_if.h`) zawiera callbacki `send_frame` i `poll`; RTL8139 inicjalizuje je w `kernel/net/rtl8139.c`.
- Bloki: ATA/AHCI udostępniają operacje sektorowe i flush w `storage.c`/`ahci.c`. `storage.c` wybiera backend.
- Inne urządzenia (RTC/PCI, PS/2, framebuffer) mają funkcje modułowe, ale bez wspólnego deskryptora urządzenia i rejestracji sterownika.

## Ograniczenia i kierunek rozwoju

PollikFS używa teraz kontraktu `BlockDevice` (`kernel/block_device.h`) z read/write/flush, początkiem FS i granicą okna sektorów. Adapter wykorzystuje istniejący wybór ATA/AHCI w storage; format PollikFS pozostaje zgodny. Ten sam kontrakt obsługuje journal/recovery i testy na pamięciowym urządzeniu sektorowym. Dopiero realni konsumenci uzasadnią rejestr urządzeń, warstwę filtrów lub stosy sterowników. Sterowniki userspace, dynamiczne ładowanie, ogólny Device Manager i hotplug nie są zaimplementowane.
