#ifndef POLLIK_USB_INPUT_H
#define POLLIK_USB_INPUT_H
typedef struct {int dx,dy,wheel;unsigned buttons;int absolute,x,y,max_x,max_y;} UsbPointerEvent;
void usb_input_init(void);
void usb_input_poll(void);
int usb_input_read(UsbPointerEvent *event);
int usb_input_read_key(unsigned char *scan);
#endif
