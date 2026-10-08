#ifndef POLLIKOS_NET_H
#define POLLIKOS_NET_H
#include <stdint.h>
#include <pollikos/syscall.h>

/* HTTP parsing and request state run in the calling Ring 3 process. */
long pollikos_http_open(const char *url);
long pollikos_http_read(long handle,void *buffer,unsigned long capacity);
long pollikos_http_close(long handle);
/* Metadata remains available until close; status is EAGAIN before headers. */
long pollikos_http_status(long handle);
long pollikos_http_url(long handle,char *buffer,unsigned long capacity);
long pollikos_http_header(long handle,const char *name,char *buffer,unsigned long capacity);
int pollikos_url_resolve(const char *base,const char *reference,char *out,unsigned long capacity);
#endif
