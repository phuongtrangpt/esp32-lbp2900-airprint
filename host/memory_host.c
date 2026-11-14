// Native allocation accounting: tracks driver allocations, independent of RSS / libc caches.
#include "printer_driver.h"
#include "capt_transport.h"
#include "capt-command.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CAPT_SIM_NO_RASTER_CHECK
#include "capt_sim.h"
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t); void *__real_realloc(void *,size_t); void __real_free(void *);
static struct { void *p; size_t n; } allocations[16384];
static size_t live,peak;
static int fail_after=-1;
static int rejected(void) { if(fail_after<0)return 0; if(!fail_after)return 1; fail_after--; return 0; }
static void record(void *p,size_t n) {
    if (!p) return;
    for(int i=0;i<16384;i++) if(!allocations[i].p) { allocations[i].p=p; allocations[i].n=n; live+=n; if(live>peak)peak=live; return; }
    abort();
}
static void forget(void *p) {
    for(int i=0;i<16384;i++) if(allocations[i].p==p) { live-=allocations[i].n; allocations[i].p=NULL; return; }
}
void *__wrap_malloc(size_t n) { if(rejected())return NULL; void *p=__real_malloc(n); record(p,n); return p; }
void *__wrap_calloc(size_t n,size_t s) { if(rejected())return NULL; void *p=__real_calloc(n,s); record(p,n*s); return p; }
void __wrap_free(void *p) { if(p)forget(p); __real_free(p); }
void *__wrap_realloc(void *p,size_t n) {
    if(rejected())return NULL;
    void *q=__real_realloc(p,n);
    if(q || !n) { if(p)forget(p); record(q,n); } return q;
}
static int discard(void *ctx,const uint8_t *p,size_t n) { return 0; }
static void run(const char *id,int paper,int pages,int noise) {
    const printer_driver_t *d=printer_driver_find(id); assert(d);
    fake_t f={0}; printer_io_t link=!strcmp(id,"canon-lbp2900")?io(&f):(printer_io_t){.write=discard};
    assert(live==0); peak=0;
    void *j=d->job_begin(d,&link,600,paper); assert(j);
    size_t retained=live;
    int w=d->papers[paper].width*600/2540,h=d->papers[paper].height*600/2540;
    unsigned char row[5200]; uint32_t rng=1;
    for(int page=0;page<pages;page++) {
        assert(d->page_begin(j,w,h,1)==0);
        for(int y=0;y<h;y++) {
            if(noise)for(int x=0;x<w;x++) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; row[x]=rng; }
            assert(d->page_line(j,y,noise?row:NULL)==0);
        }
        assert(d->page_end(j)==0);
        assert(live==retained); // absolutely no retained growth across pages
    }
    assert(d->job_end(j)==0); assert(live==0);
    printf("%s %s: %d pages; retained=%zu peak=%zu bytes; growth=0; after-job=0; noise=%d\n",id,d->papers[paper].name,pages,retained,peak,noise);
    fflush(stdout);
}
int main(void) {
    run("canon-lbp2900",0,100,0);
    run("hp-p1006",0,100,0);
    run("hp-p1006",0,2,1);
    run("hp-p1006",1,100,0);
    run("generic-pcl5",0,100,0);
    run("generic-postscript",0,100,0);
    const printer_driver_t *d=printer_driver_find("hp-p1006");
    const int faults[]={0,3,15,100};
    for(int i=0;i<4;i++) {
        printer_io_t link={.write=discard}; void *j=d->job_begin(d,&link,600,0); assert(j);
        assert(!d->page_begin(j,4960,7015,1));
        for(int y=0;y<7015;y++) assert(!d->page_line(j,y,NULL));
        fail_after=faults[i]; assert(d->page_end(j)<0); fail_after=-1;
        assert(d->job_end(j)<0); assert(live==0);
    }
    puts("HP compression OOM at 4 allocation points: recovered, no retained allocation");
}
