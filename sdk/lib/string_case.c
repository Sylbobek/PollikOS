#include <string.h>
static unsigned char lower(unsigned char c){return c>='A'&&c<='Z'?(unsigned char)(c+32):c;}
int strncasecmp(const char *a,const char *b,size_t n){
    while(n--){unsigned char x=lower((unsigned char)*a++),y=lower((unsigned char)*b++);
        if(x!=y)return (int)x-(int)y;if(!x)return 0;}
    return 0;
}
int strcasecmp(const char *a,const char *b){
    for(;;){unsigned char x=lower((unsigned char)*a++),y=lower((unsigned char)*b++);
        if(x!=y)return (int)x-(int)y;if(!x)return 0;}
}
