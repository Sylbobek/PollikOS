#ifndef POLLIKOS_IMAGE_H
#define POLLIKOS_IMAGE_H
#include <stddef.h>
/* Owned RGBA8 buffer, or NULL. PNG/JPEG/BMP and the first GIF frame. */
unsigned char *pollikos_image_decode(const void *data,size_t bytes,int *width,int *height);
void pollikos_image_free(void *pixels);
#endif
