#include "browser.h"
#include "../net/http.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 512
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(n) kmalloc(n)
#define STBI_REALLOC(p,n) krealloc(p,n)
#define STBI_FREE(p) kfree(p)
#include "../../third_party/stb/stb_image.h"
static void load(DomNode *n,const char *base,int *count) {
 if(!n)return;
 browser_work_checkpoint();
 if(!memcmp(n->tag,"img",4)&&n->src[0]&&*count<8) {
  (*count)++;char url[256];HttpResponse r;
  if(http_resolve_url(base,n->src,url,sizeof(url))&&http_get(url,&r)) {
   int w,h,c;
   if(r.status_code==200 && stbi_info_from_memory(r.body,r.body_len,&w,&h,&c) && w>0&&h>0&&w<=512&&h<=512&&w*h<=65536) {
    n->image=stbi_load_from_memory(r.body,r.body_len,&n->image_w,&n->image_h,&c,4);
    if(n->image)serial("IMAGE: PNG/JPEG decoded\n");
    else { serial("IMAGE: decode failed: "); serial(stbi_failure_reason()); serial("\n"); }
   } else {
    serial("IMAGE: unsupported or unsafe dimensions\n");
   }
   http_response_free(&r);
  }
 }
 for(DomNode *c=n->first_child;c;c=c->next_sibling)load(c,base,count);
}
void browser_load_images(DomNode *root,const char *url){int count=0;load(root,url,&count);}
