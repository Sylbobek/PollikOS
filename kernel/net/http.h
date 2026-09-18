#ifndef POLLIK_HTTP_H
#define POLLIK_HTTP_H

#include "../system.h"

typedef struct {
    int status_code;
    char content_type[64];
    char location[128];
    int content_length;
    int is_chunked;
    u8 *body;
    int body_len;
    int error;
    char final_url[256];
} HttpResponse;

void http_response_init(HttpResponse *resp);
void http_response_free(HttpResponse *resp);
int http_request(const char *method, const char *url, const char *extra_headers, const u8 *post_data, int post_len, HttpResponse *resp);
int http_get(const char *url, HttpResponse *resp);
int http_post(const char *url, const char *content_type, const u8 *data, int data_len, HttpResponse *resp);
int http_head(const char *url, HttpResponse *resp);
int http_get_public_ip(char *out_ip, int max_len);

int http_resolve_url(const char *base, const char *reference, char *out, int capacity);
#endif
