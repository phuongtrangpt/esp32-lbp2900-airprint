// SPDX-License-Identifier: GPL-3.0-or-later
#include "encoder.h"
#include "printer_driver.h"
#include <stdio.h>
#include <stdlib.h>
extern const printer_driver_t hp_p1006_driver;
struct vp_encoder {FILE *out;void *job;size_t bytes;int pages,failed;};
static int write_out(void *ctx,const uint8_t *p,size_t n){vp_encoder *e=ctx;if(e->bytes+n>256u*1024u*1024u || fwrite(p,1,n,e->out)!=n){e->failed=1;return -1;}e->bytes+=n;return (int)n;}
vp_encoder *vp_begin(const char *path,int paper){
 if(paper<0||paper>=4)return NULL;
 vp_encoder *e=calloc(1,sizeof(*e));if(!e)return NULL;
 e->out=fopen(path,"wb");if(!e->out){free(e);return NULL;}
 printer_io_t io={.write=write_out,.ctx=e};
 e->job=hp_p1006_driver.job_begin(&hp_p1006_driver,&io,300,paper);
 if(!e->job){fclose(e->out);free(e);return NULL;}return e;
}
int vp_page(vp_encoder *e,const uint8_t *rgba,int w,int h,size_t stride){
 if(!e||e->failed||!rgba||w<1||h<1||w>2600||h>3600||stride<(size_t)w*4)return -1;
 uint8_t *gray=malloc(w);if(!gray)return -1;
 int rc=hp_p1006_driver.page_begin(e->job,w,h,1);
 for(int y=0;!rc&&y<h;y++){
  const uint8_t *p=rgba+(size_t)y*stride;
  for(int x=0;x<w;x++,p+=4){unsigned a=p[3],v=(77u*p[0]+150u*p[1]+29u*p[2])>>8;gray[x]=(uint8_t)(v+255u-a);}
  rc=hp_p1006_driver.page_line(e->job,y,gray);
 }
 free(gray);if(!rc)rc=hp_p1006_driver.page_end(e->job);if(!rc)e->pages++;else e->failed=1;return rc;
}
int vp_end(vp_encoder *e,int success){if(!e)return -1;int ok=success&&!e->failed&&e->pages>0;int rc=ok?hp_p1006_driver.job_end(e->job):hp_p1006_driver.job_abort(e->job);if(rc||ferror(e->out))ok=0;if(fclose(e->out))ok=0;free(e);return ok?0:-1;}
