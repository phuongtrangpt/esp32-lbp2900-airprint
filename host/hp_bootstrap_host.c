#include "hp_bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
int main(int argc,char **argv){
 assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);unsigned char*a=malloc(n);assert(a&&fread(a,1,n,f)==(size_t)n);fclose(f);
 unsigned char *dl=NULL;size_t len=0;assert(hp_bootstrap_build(a,n,&dl,&len)==0);assert(len==222891);
 f=fopen(argv[2],"wb");assert(f);assert(fwrite(dl,1,len,f)==len);fclose(f);free(dl);
 for(size_t k=0;k<18;k++)assert(hp_bootstrap_build(a,k,&dl,&len)<0);
 a[n-8]^=1;assert(hp_bootstrap_build(a,n,&dl,&len)<0);free(a);
 puts("HP bootstrap: valid gzip/tar conversion, CRC corruption and truncated headers passed");
}
