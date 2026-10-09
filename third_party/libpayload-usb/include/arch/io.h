#include "../../../../kernel/hal.h"
#define inb(p) hal_port_read8(p)
#define inw(p) hal_port_read16(p)
#define inl(p) hal_port_read32(p)
#define outb(v,p) hal_port_write8(p,v)
#define outw(v,p) hal_port_write16(p,v)
#define outl(v,p) hal_port_write32(p,v)
