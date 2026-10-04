#include "browser.h"
#include "../net/http.h"
#include "../media.h"
static u32 g_image_pixels;
static void load(DomNode *n,const char *base,int *count) {
 if(!n)return;
 browser_work_checkpoint();
 if(!memcmp(n->tag,"img",4)&&n->src[0]&&*count<8) {
  (*count)++;char url[256];HttpResponse r;
  if(http_resolve_url(base,n->src,url,sizeof(url))&&http_get(url,&r)) {
   int w=0,h=0;
   if(r.status_code==200 && g_image_pixels<8388608u) {
    MediaGif *gif=media_gif_open((const u8*)r.body,r.body_len);
    u8 *px=gif?(u8*)media_gif_canvas(gif,0):media_decode((const u8*)r.body,r.body_len,&w,&h);
    if(gif) { w=media_gif_width(gif); h=media_gif_height(gif); }
    if(px && w>0&&h>0) {
     if(g_image_pixels+(u32)w*(u32)h>8388608u) {
      if(gif){media_gif_close(gif);gif=0;}else media_free(px);
      px=0;
     } else {
      n->gif=gif;n->image=px;n->image_w=w;n->image_h=h;
      g_image_pixels+=(u32)w*(u32)h;
      serial(gif&&media_gif_animating(gif)?"IMAGE: animated GIF decoded\n":"IMAGE: decoded\n");
     }
    }
    if(!px) { if(gif)media_gif_close(gif);serial("IMAGE: decode failed\n"); }
   } else {
    serial("IMAGE: unsupported or unsafe dimensions\n");
   }
   http_response_free(&r);
  }
 }
 for(DomNode *c=n->first_child;c;c=c->next_sibling)load(c,base,count);
}
void browser_load_images(DomNode *root,const char *url){g_image_pixels=0;int count=0;load(root,url,&count);}

void browser_media_node_destroyed(DomNode *node) {
 if(!node||!node->gif)return;
 media_gif_close(node->gif);
 node->gif=0;
 node->image=0;
}

int browser_advance_media(DomNode *node) {
 if(!node)return 0;
 int changed=0;
 if(node->gif&&media_gif_due(node->gif)) {
  const u8 *frame=media_gif_canvas(node->gif,1);
  if(frame) { node->image=(u8*)frame; changed=1; }
 }
 for(DomNode *child=node->first_child;child;child=child->next_sibling)
  changed|=browser_advance_media(child);
 return changed;
}
