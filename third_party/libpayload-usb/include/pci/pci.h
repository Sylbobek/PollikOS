#ifndef POLLIK_USB_PCI_H
#define POLLIK_USB_PCI_H
#include <stdint.h>
typedef uint32_t pcidev_t;
#define PCI_DEV(bus,dev,fn) (((bus)<<16)|((dev)<<11)|((fn)<<8))
#define REG_VENDOR_ID 0
#define REG_DEVICE_ID 2
#define REG_COMMAND 4
#define PCI_COMMAND REG_COMMAND
#define PCI_COMMAND_IO 1
#define PCI_COMMAND_MEMORY 2
#define PCI_COMMAND_MASTER 4
#define PCI_BASE_ADDRESS_0 0x10
#define PCI_BASE_ADDRESS_1 0x14
#define PCI_BASE_ADDRESS_MEM_MASK 0xfffffff0u
#define REG_HEADER_TYPE 0x0e
#define REG_SECONDARY_BUS 0x19
#define REG_BAR0 0x10
#define REG_BAR4 0x20
#define HEADER_TYPE_MULTIFUNCTION 0x80
#define HEADER_TYPE_BRIDGE 1
uint32_t pci_read_config32(pcidev_t dev,unsigned reg);
uint16_t pci_read_config16(pcidev_t dev,unsigned reg);
uint8_t pci_read_config8(pcidev_t dev,unsigned reg);
void pci_write_config32(pcidev_t dev,unsigned reg,uint32_t value);
void pci_write_config16(pcidev_t dev,unsigned reg,uint16_t value);
void pci_write_config8(pcidev_t dev,unsigned reg,uint8_t value);
#endif
