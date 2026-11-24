#include "printer_driver.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(void *ctx,const uint8_t *p,size_t n) { return fwrite(p,1,n,ctx)==n?0:-1; }
static int fail(void *ctx,const uint8_t *p,size_t n) { return -1; }
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
static void check_gray_scaling(const printer_driver_t *d)
{
    FILE *scaled = tmpfile(), *reference = tmpfile();
    assert(scaled && reference);
    printer_io_t scaled_io = {.write = write_file, .ctx = scaled};
    printer_io_t reference_io = {.write = write_file, .ctx = reference};
    void *scaled_job = d->job_begin(d, &scaled_io, 300, 0);
    void *reference_job = d->job_begin(d, &reference_io, 600, 0);
    assert(scaled_job && reference_job);
    assert(d->page_begin(scaled_job, 2480, 3507, 1) == 0);
    assert(d->page_begin(reference_job, 4960, 7014, 1) == 0);

    uint8_t row[2480], doubled[4960];
    memset(row, 255, sizeof(row));
    for (int y = 0; y < 3507; ++y) {
        // Cover every gray level in each dither phase and test white margins.
        for (int x = 97; x < 620; ++x)
            row[x] = (uint8_t)(x / 2 + y / 2);
        for (int x = 0; x < 2480; ++x)
            doubled[2 * x] = doubled[2 * x + 1] = row[x];
        assert(d->page_line(scaled_job, y, row) == 0);
        assert(d->page_line(reference_job, 2 * y, doubled) == 0);
        assert(d->page_line(reference_job, 2 * y + 1, doubled) == 0);
    }
    assert(d->page_end(scaled_job) == 0 && d->job_end(scaled_job) == 0);
    assert(d->page_end(reference_job) == 0 && d->job_end(reference_job) == 0);

    rewind(scaled);
    rewind(reference);
    uint8_t actual[4096], expected[4096];
    size_t count;
    do {
        count = fread(actual, 1, sizeof(actual), scaled);
        assert(count == fread(expected, 1, sizeof(expected), reference));
        assert(memcmp(actual, expected, count) == 0);
    } while (count);
    assert(feof(scaled) && feof(reference));
    fclose(scaled);
    fclose(reference);
    puts("HP P1006 300 dpi gray: XQX matches the expanded 600 dpi reference");
}
#endif

int main(int argc,char **argv) {
#ifdef PRINTSERVER_PROFILE_HP_P1008
    const printer_driver_t *d = printer_driver_match(0x03f0, 0x4917, NULL);
    assert(d && !strcmp(d->id, "hp-p1008") && d->npapers == 3);
    assert(!printer_driver_match(0x03f0, 0x3e17, "MFG:HP;MDL:LaserJet P1006;"));
    assert(!printer_driver_match(0, 0, "MFG:HP;MDL:LaserJet P1008;"));
#else
    const printer_driver_t *d=printer_driver_match(0x03f0,0x3e17,NULL);
    assert(d && !strcmp(d->id,"hp-p1006") && d->npapers == 4);
    assert(printer_driver_match(0,0,"MFG:HP;MDL:LaserJet P1006;")==d);
    assert(!printer_driver_match(0x03f0, 0x4917, "MFG:HP;MDL:LaserJet P1008;"));
#endif
    assert(!d->scanner && !d->color);
    assert(!d->job_begin(d,&(printer_io_t){.write=fail},600,0));
    assert(!d->job_begin(d,&(printer_io_t){.write=fail},300,0));
    for (int paper=0;paper<d->npapers;paper++) {
        char path[200]; snprintf(path,sizeof(path),"%s-%d.xqx",argc>1?argv[1]:"/tmp/hp",paper);
        FILE *out=fopen(path,"wb"); assert(out);
        printer_io_t io={.write=write_file,.ctx=out}; void *j=d->job_begin(d,&io,600,paper); assert(j);
        int w=d->papers[paper].width*600/2540,h=d->papers[paper].height*600/2540;
        unsigned char *line=malloc(w*3); assert(line);
        for (int page=0;page<2;page++) {
            int bpp=page?3:1; assert(d->page_begin(j,w,h,bpp)==0);
            assert(d->page_line(j,1,NULL)<0);
            for (int y=0;y<h;y++) {
                for(int x=0;x<w;x++) { unsigned char v=((x/37+y/29+page)%3)?255:0;
                    for(int k=0;k<bpp;k++) line[x*bpp+k]=v; }
                assert(d->page_line(j,y,line)==0);
            }
            assert(d->page_end(j)==0);
        }
        free(line); assert(d->job_end(j)==0); fclose(out);
    }
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    assert(d->nresolutions == 1 && d->resolutions[0] == 300);
    check_gray_scaling(d);
    FILE *partial = tmpfile();
    assert(partial && d->job_abort);
    printer_io_t io = {.write = write_file, .ctx = partial};
    void *job = d->job_begin(d, &io, 600, 0);
    assert(job && d->page_begin(job, 4960, 7015, 1) == 0);
    assert(d->page_line(job, 0, NULL) == 0);
    assert(d->job_abort(job) == 0);
    const char tail[] = "\033%-12345X@PJL EOJ\n\033%-12345X";
    char result[sizeof(tail) - 1];
    assert(fseek(partial, -(long)sizeof(result), SEEK_END) == 0);
    assert(fread(result, 1, sizeof(result), partial) == sizeof(result));
    assert(!memcmp(result, tail, sizeof(result)));
    fclose(partial);
    puts("HP P1006 incomplete raster: discards page and writes end of job");
#endif
    printf("%s: USB/model matching, declared paper sizes, gray/RGB 2-page streams, bad sequence and sink failure passed\n", d->make_model);
}
