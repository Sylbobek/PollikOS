/* Shared HTTP URL resolution for redirects, HTML/CSS assets and JavaScript. */
#include <pollikos/net.h>
#include <string.h>
int pollikos_url_resolve(const char *base,const char *reference,char *out,unsigned long capacity) {
    if(!base || !reference || !out || capacity<2)return 0;
    char joined[3072];size_t n=0,scheme=0;
    if(!strncmp(base,"https://",8))scheme=8;else if(!strncmp(base,"http://",7))scheme=7;
    if(strstr(reference,"://")) {
        size_t length=strlen(reference);if(length>=sizeof(joined))return 0;memcpy(joined,reference,length+1);
    } else {
        if(!scheme)return 0;
        size_t authority=scheme;while(base[authority] && base[authority]!='/' && base[authority]!='?' && base[authority]!='#')++authority;
        size_t prefix=authority;
        if(reference[0]=='/' && reference[1]=='/'){prefix=scheme-2;}
        else if(reference[0]=='#'){prefix=strlen(base);const char *hash=strchr(base,'#');if(hash)prefix=(size_t)(hash-base);}
        else if(reference[0]=='?'){prefix=strlen(base);for(size_t i=authority;base[i];i++)if(base[i]=='?'||base[i]=='#'){prefix=i;break;}}
        else if(reference[0]!='/'){
            size_t end=authority;while(base[end] && base[end]!='?' && base[end]!='#')++end;
            for(size_t i=authority;i<end;i++)if(base[i]=='/')prefix=i+1;
        }
        size_t length=strlen(reference);int slash=prefix==authority && reference[0]!='/' && reference[0]!='?' && reference[0]!='#';
        if(prefix+slash+length>=sizeof(joined))return 0;
        memcpy(joined,base,prefix);n=prefix;if(slash)joined[n++]='/';memcpy(joined+n,reference,length+1);
    }
    size_t start=!strncmp(joined,"https://",8)?8:!strncmp(joined,"http://",7)?7:0;
    if(!start)return 0;
    while(joined[start] && joined[start]!='/' && joined[start]!='?' && joined[start]!='#')++start;
    size_t end=start;while(joined[end] && joined[end]!='?' && joined[end]!='#')++end;
    if(start>=capacity)return 0;memcpy(out,joined,start);n=start;
    if(joined[start]=='/') {
        size_t at=start+1;out[n++]='/';
        while(at<end){size_t next=at;while(next<end && joined[next]!='/')next++;
            size_t length=next-at;
            if(length==2 && joined[at]=='.' && joined[at+1]=='.'){if(n>start+1){if(out[n-1]=='/')--n;while(n>start+1 && out[n-1]!='/')--n;}}
            else if(!(length==1 && joined[at]=='.')) {
                if(n+length+(next<end)>=capacity)return 0;
                memcpy(out+n,joined+at,length);n+=length;if(next<end)out[n++]='/';
            }
            at=next+1;
        }
    }
    size_t tail=strlen(joined+end);if(n+tail>=capacity)return 0;memcpy(out+n,joined+end,tail+1);return 1;
}
