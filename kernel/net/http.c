#include "http.h"
#include "net_manager.h"
#include "tls.h"
#include "net_util.h"
#include "../mem.h"

void http_response_init(HttpResponse *resp) {
    if (!resp)
        return;
    memset(resp, 0, sizeof(*resp));
}

void http_response_free(HttpResponse *resp) {
    if (!resp)
        return;
    if (resp->body) {
        kfree(resp->body);
        resp->body = 0;
    }
    resp->body_len = 0;
    resp->status_code = 0;
}

static int parse_url(const char *url, int *is_https, char *host, int max_host, u16 *port, char *path, int max_path) {
    if (!url || !url[0])
        return 0;

    const char *p = url;
    *is_https = 1;
    *port = 443;

    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == ':' && p[5] == '/' && p[6] == '/') {
        *is_https = 0;
        *port = 80;
        p += 7;
    } else if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == 's' && p[5] == ':' && p[6] == '/' && p[7] == '/') {
        *is_https = 1;
        *port = 443;
        p += 8;
    }

    int h = 0;
    while (*p && *p != '/' && *p != ':' && h < max_host - 1) {
        host[h++] = *p++;
    }
    host[h] = 0;
    if (h == 0)
        return 0;

    if (*p == ':') {
        p++;
        u32 custom_port = 0;
        while (*p >= '0' && *p <= '9') {
            custom_port = custom_port * 10 + (*p - '0');
            p++;
        }
        if (custom_port > 0 && custom_port <= 65535)
            *port = (u16)custom_port;
    }

    if (*p == '/') {
        int pt = 0;
        while (*p && pt < max_path - 1) {
            path[pt++] = *p++;
        }
        path[pt] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }

    return 1;
}

int http_resolve_url(const char *base,const char *ref,char *out,int cap) {
    if(!base||!ref||!out||cap<1)return 0;
    int n=0;
    if(!memcmp(ref,"http://",strlen(ref)>=7?7:strlen(ref)+1)||!memcmp(ref,"https://",strlen(ref)>=8?8:strlen(ref)+1)) {
        if((int)strlen(ref)>=cap)return 0;memcpy(out,ref,strlen(ref)+1);return 1;
    }
    const char *scheme=base;while(*scheme&&*scheme!=':')scheme++;
    if(!*scheme)return 0;
    int prefix=scheme-base+3;int end=prefix;while(base[end]&&base[end]!='/'&&base[end]!='?'&&base[end]!='#')end++;
    if(ref[0]=='/'&&ref[1]=='/')prefix=scheme-base+1;
    else if(ref[0]=='/')prefix=end;
    else {prefix=end;for(int i=end;base[i]&&base[i]!='?'&&base[i]!='#';i++)if(base[i]=='/')prefix=i+1;}
    if(prefix+(int)strlen(ref)+2>cap)return 0;
    memcpy(out,base,prefix);n=prefix;
    if(ref[0]!='/'&&prefix==end)out[n++]='/';
    while(*ref&&*ref!='#')out[n++]=*ref++;out[n]=0;return 1;
}

static int parse_hex(const char *s, int len) {
    int val = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;
        val = (val << 4) | digit;
    }
    return val;
}

static int str_prefix(const char *s, const char *pre) {
    while (*pre) {
        char a = *s++;
        char b = *pre++;
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return 0;
    }
    return 1;
}

/* Return 1 only when a complete framed response has arrived, -1 on invalid framing. */
static int framed_complete(const u8 *data,int n,int head) {
    int end=-1;
    for(int i=0;i+3<n;i++)if(!memcmp(data+i,"\r\n\r\n",4)){end=i+4;break;}
    if(end<0)return n>8192?-1:0;
    if(head)return 1;
    int length=-1,chunked=0;
    for(int i=0;i<end;) {
        int j=i;while(j<end && data[j]!='\r')j++;
        char line[256];int len=j-i;if(len>255)len=255;
        memcpy(line,data+i,len);line[len]=0;
        if(str_prefix(line,"Content-Length:")) {
            if(j-i>255)return -1;
            int k=15;while(line[k]==' ')k++;
            int v=0,digits=0;
            while(line[k]>='0'&&line[k]<='9') {if(v>262144/10)return -1;v=v*10+line[k++]-'0';digits++;}
            if(!digits||v>262144||(length>=0&&length!=v))return -1;
            length=v;
        }
        if(str_prefix(line,"Transfer-Encoding:")) {
            if(j-i>255)return -1;
            const char *v=line+18;while(*v==' ')v++;
            if(!str_prefix(v,"chunked"))return -1;
            chunked=1;
        }
        i=j+2;
    }
    if(chunked) {
        if(length>=0)return -1;
        int pos=end;
        for(;;) {
            int j=pos;while(j+1<n && !(data[j]=='\r'&&data[j+1]=='\n'))j++;
            if(j+1>=n)return 0;
            int size=0,digits=0;
            for(int k=pos;k<j && data[k]!=';';k++) {
                int v=data[k]>='0'&&data[k]<='9'?data[k]-'0':data[k]>='a'&&data[k]<='f'?data[k]-'a'+10:data[k]>='A'&&data[k]<='F'?data[k]-'A'+10:-1;
                if(v<0||size>262144/16)return -1;
                size=size*16+v;digits++;
            }
            if(!digits||size>262144)return -1;
            pos=j+2;
            if(!size) {
                if(pos+2<=n&&!memcmp(data+pos,"\r\n",2))return 1;
                for(int k=pos;k+3<n;k++)if(!memcmp(data+k,"\r\n\r\n",4))return 1;
                return 0;
            }
            if(size>n-pos-2)return 0;
            if(memcmp(data+pos+size,"\r\n",2))return -1;
            pos+=size+2;
        }
    }
    return length>=0 ? n-end>=length : 0;
}

int http_request(const char *method, const char *url, const char *extra_headers, const u8 *post_data, int post_len, HttpResponse *resp) {
    if (!resp) return 0;
    http_response_init(resp);
    if (!url || !method || post_len < 0 || (post_len && !post_data)) { resp->error=1;return 0; }

    int redirects = 0;
    char current_url[256];
    int ulen = 0;
    while (url[ulen] && ulen < 255) {
        current_url[ulen] = url[ulen];
        ulen++;
    }
    current_url[ulen] = 0;

redirect_loop:
    http_response_init(resp);
    if (redirects >= 5) {
        resp->error = 1;
        serial("HTTP: Too many redirects\n");
        return 0;
    }

    int is_https = 1;
    char host[128];
    char path[128];
    u16 port = 443;

    if (!parse_url(current_url, &is_https, host, sizeof(host), &port, path, sizeof(path))) {
        resp->error = 1;
        serial("HTTP: Invalid URL\n");
        return 0;
    }

    u8 remote_ip[4];
    if (!net_manager_resolve_host(host, remote_ip)) {
        resp->error = 2;
        serial("HTTP: DNS failed for ");
        serial(host);
        serial("\n");
        return 0;
    }

    TcpSocket *tcp_sock = net_manager_connect_tcp(remote_ip, port);
    if (!tcp_sock) {
        resp->error = 3;
        serial("HTTP: TCP connect failed\n");
        return 0;
    }

    TlsSocket *tls_sock = 0;
    if (is_https) {
        tls_sock = tls_connect(tcp_sock, host, 400);
        if (!tls_sock) {
            resp->error = 4;
            tcp_close(tcp_sock);
            serial("HTTP: TLS handshake failed\n");
            return 0;
        }
    }

    /* Build HTTP request */
    char req_buf[1024];
    int rlen = 0;

    #define APPEND_STR(s) do { const char *as = (s); while (*as && rlen < 1020) req_buf[rlen++] = *as++; } while (0)
    APPEND_STR(method);
    APPEND_STR(" ");
    APPEND_STR(path);
    APPEND_STR(" HTTP/1.1\r\nHost: ");
    APPEND_STR(host);
    if ((is_https && port!=443)||(!is_https && port!=80)) {
        char port_text[12];number(port_text,port);APPEND_STR(":");APPEND_STR(port_text);
    }
    APPEND_STR("\r\nUser-Agent: PollikOS/0.1\r\nAccept: text/html,application/xhtml+xml,text/*,*/*\r\nAccept-Encoding: identity\r\nConnection: close\r\n");

    if (post_len > 0) {
        APPEND_STR("Content-Length: ");
        char cl_buf[16];
        number(cl_buf, (u32)post_len);
        APPEND_STR(cl_buf);
        APPEND_STR("\r\n");
    }

    if (extra_headers && extra_headers[0]) {
        APPEND_STR(extra_headers);
    }
    APPEND_STR("\r\n");

    /* Send request headers */
    if (is_https) {
        tls_send(tls_sock, (const u8 *)req_buf, rlen);
        if (post_data && post_len > 0)
            tls_send(tls_sock, post_data, post_len);
    } else {
        tcp_send(tcp_sock, (const u8 *)req_buf, rlen);
        if (post_data && post_len > 0)
            tcp_send(tcp_sock, post_data, post_len);
    }

    serial("HTTP: Request sent: ");
    serial(method);
    serial(" ");
    serial(path);
    serial("\n");

    /* Read response */
    u32 raw_cap = 16384;
    u8 *raw_buf = (u8 *)kmalloc(raw_cap);
    if (!raw_buf) {
        if (is_https)
            tls_close(tls_sock);
        else
            tcp_close(tcp_sock);
        resp->error = 1;
        return 0;
    }

    int raw_len = 0;
    u8 chunk[1024];

    u32 read_start=ticks;
    for (;;) {
        /* Also service on successful reads, not only idle sockets. */
        net_service_wait();
        if(ticks-read_start>1500) {resp->error=1;break;}
        int n = is_https ? tls_read(tls_sock, chunk, sizeof(chunk)) : tcp_read(tcp_sock, chunk, sizeof(chunk));
        if (n > 0) {
            read_start=ticks;
            if (raw_len + n > 262144) break;
            if (raw_len + n >= (int)raw_cap) {
                raw_cap *= 2;
                u8 *grown = (u8 *)krealloc(raw_buf, raw_cap);
                if (!grown) {
                    resp->error = 1;
                    break;
                }
                raw_buf=grown;
            }
            memcpy(raw_buf + raw_len, chunk, (unsigned)n);
            raw_len += n;
            int complete=framed_complete(raw_buf,raw_len,str_prefix(method,"HEAD"));
            if(complete<0) {resp->error=1;break;}
            if(complete)break;
        } else {
            if (is_https ? tls_is_eof(tls_sock) : tcp_is_eof(tcp_sock))
                break;
            net_manager_poll();
        }
    }

    if (is_https)
        tls_close(tls_sock);
    else
        tcp_close(tcp_sock);

    if (resp->error || raw_len <= 0) {
        if (raw_buf)
            kfree(raw_buf);
        resp->error = 1;
        serial("HTTP: Empty response\n");
        return 0;
    }

    /* Find header-body separator \r\n\r\n */
    int body_offset = -1;
    for (int i = 0; i < raw_len - 3; i++) {
        if (raw_buf[i] == '\r' && raw_buf[i + 1] == '\n' && raw_buf[i + 2] == '\r' && raw_buf[i + 3] == '\n') {
            body_offset = i + 4;
            break;
        }
    }

    if (body_offset < 0) {
        kfree(raw_buf);
        resp->error = 1;
        return 0;
    }

    /* Parse status code */
    raw_buf[body_offset - 2] = 0; /* Null-terminate headers */
    char *hstr = (char *)raw_buf;
    if (str_prefix(hstr, "HTTP/1.0 ") || str_prefix(hstr, "HTTP/1.1 ")) {
        char *sc = hstr + 9;
        while (*sc == ' ')
            sc++;
        int code = 0;
        for (int i = 0; i < 3 && sc[i] >= '0' && sc[i] <= '9'; i++)
            code = code * 10 + (sc[i] - '0');
        resp->status_code = code;
    }

    /* Parse headers */
    char *line = hstr;
    while (*line) {
        char *next = line;
        while (*next && *next != '\r' && *next != '\n')
            next++;
        while (*next == '\r' || *next == '\n')
            *next++ = 0;

        if (str_prefix(line, "Content-Length:")) {
            char *v = line + 15;
            while (*v == ' ')
                v++;
            int cl = 0;
            while (*v >= '0' && *v <= '9')
                cl = cl * 10 + (*v++ - '0');
            resp->content_length = cl;
        } else if (str_prefix(line, "Content-Type:")) {
            char *v = line + 13;
            while (*v == ' ')
                v++;
            int cti = 0;
            while (*v && *v != ';' && *v != '\r' && cti < 63)
                resp->content_type[cti++] = *v++;
            resp->content_type[cti] = 0;
        } else if (str_prefix(line, "Location:")) {
            char *v = line + 9;
            while (*v == ' ')
                v++;
            int li = 0;
            while (*v && *v != '\r' && li < 127)
                resp->location[li++] = *v++;
            resp->location[li] = 0;
        } else if (str_prefix(line, "Transfer-Encoding:")) {
            if (str_prefix(line + 18, " chunked"))
                resp->is_chunked = 1;
        }

        line = next;
    }

    /* Handle redirects */
    if ((resp->status_code == 301 || resp->status_code == 302 || resp->status_code == 303 ||
         resp->status_code == 307 || resp->status_code == 308) && resp->location[0]) {
        redirects++;
        char next_url[256];
        if(!http_resolve_url(current_url,resp->location,next_url,sizeof(next_url))) {kfree(raw_buf);resp->error=1;return 0;}
        memcpy(current_url,next_url,strlen(next_url)+1);
        if(resp->status_code==303){method="GET";post_len=0;post_data=0;}
        kfree(raw_buf);
        serial("HTTP: Redirecting to ");
        serial(current_url);
        serial("\n");
        goto redirect_loop;
    }

    /* Process Body */
    int raw_body_len = raw_len - body_offset;
    if(str_prefix(method,"HEAD"))raw_body_len=0;
    else if(resp->content_length>0 && !resp->is_chunked) {
        if(raw_body_len<resp->content_length) {kfree(raw_buf);resp->error=1;return 0;}
        raw_body_len=resp->content_length;
    }
    u8 *body_src = raw_buf + body_offset;

    if (resp->is_chunked) {
        /* Decode chunked transfer encoding */
        resp->body = (u8 *)kmalloc((u32)raw_body_len + 1);
        if (!resp->body) { kfree(raw_buf); resp->error=1; return 0; }
        int out_len = 0;
        int in_pos = 0;

        while (in_pos < raw_body_len) {
            int line_end = in_pos;
            while (line_end < raw_body_len - 1 && !(body_src[line_end] == '\r' && body_src[line_end + 1] == '\n'))
                line_end++;
            if (line_end >= raw_body_len - 1)
                break;

            int chunk_sz = parse_hex((char *)(body_src + in_pos), line_end - in_pos);
            in_pos = line_end + 2;

            if (chunk_sz <= 0)
                break;

            if (in_pos + chunk_sz > raw_body_len)
                chunk_sz = raw_body_len - in_pos;

            memcpy(resp->body + out_len, body_src + in_pos, (unsigned)chunk_sz);
            out_len += chunk_sz;
            in_pos += chunk_sz + 2; /* Skip trailing \r\n */
        }
        resp->body[out_len] = 0;
        resp->body_len = out_len;
    } else {
        resp->body = (u8 *)kmalloc((u32)raw_body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body_src, (unsigned)raw_body_len);
            resp->body[raw_body_len] = 0;
            resp->body_len = raw_body_len;
        }
    }

    kfree(raw_buf);

    serial("HTTP: Response status: ");
    char sbuf[12];
    number(sbuf, (u32)resp->status_code);
    serial(sbuf);
    serial(", body length: ");
    number(sbuf, (u32)resp->body_len);
    serial(sbuf);
    serial(" bytes\n");

    memcpy(resp->final_url, current_url, strlen(current_url) + 1);
    if (!resp->body) {resp->error=1;return 0;}
    return 1;
}

int http_get(const char *url, HttpResponse *resp) {
    return http_request("GET", url, 0, 0, 0, resp);
}

int http_post(const char *url, const char *content_type, const u8 *data, int data_len, HttpResponse *resp) {
    char extra[128];
    int ei = 0;
    const char *ct = "Content-Type: ";
    while (*ct) extra[ei++] = *ct++;
    while (*content_type && ei < 120) extra[ei++] = *content_type++;
    extra[ei++] = '\r';
    extra[ei++] = '\n';
    extra[ei] = 0;
    return http_request("POST", url, extra, data, data_len, resp);
}

int http_head(const char *url, HttpResponse *resp) {
    return http_request("HEAD", url, 0, 0, 0, resp);
}

int http_get_public_ip(char *out_ip, int max_len) {
    if (!out_ip || max_len <= 0)
        return 0;

    HttpResponse resp;
    serial("HTTP: Fetching public IP via HTTPS...\n");
    if (http_get("https://api.ipify.org", &resp) && resp.status_code == 200 && resp.body) {
        int i = 0;
        while (i < resp.body_len && i < max_len - 1 && resp.body[i] > ' ') {
            out_ip[i] = (char)resp.body[i];
            i++;
        }
        out_ip[i] = 0;
        http_response_free(&resp);
        serial("HTTP: Public IP is ");
        serial(out_ip);
        serial("\n");
        return 1;
    }
    http_response_free(&resp);
    return 0;
}
