#include <libpayload.h>
#define virt_to_phys(p) usb_lp_virt_to_phys(p)
#define phys_to_virt(p) usb_lp_phys_to_virt(p)
