// Local port: tracked allocations let the firmware recover from JBIG OOM.
#include <stddef.h>
void *hp_jbig_malloc(size_t n);
void *hp_jbig_realloc(void *p,size_t n);
void hp_jbig_free(void *p);
void hp_jbig_oom(void);
#define malloc hp_jbig_malloc
#define realloc hp_jbig_realloc
#define free hp_jbig_free
#define abort hp_jbig_oom
