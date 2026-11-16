#include "hp_bootstrap.h"
#include "puff.h"
#include <stdlib.h>
#include <string.h>
#define TAR_LIMIT (512*1024)
uint32_t hp_bootstrap_crc32(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
static uint32_t le(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int hp_bootstrap_build(const uint8_t *a,size_t n,uint8_t **dl,size_t *len){
 if(!a||!dl||!len||n<18||n>300*1024||a[0]!=31||a[1]!=139||a[2]!=8||(a[3]&224))return -1;
 *dl=NULL;*len=0;size_t off=10,end=n-8;
 if(a[3]&4){if(off+2>end)return -1;size_t x=a[off]|(size_t)a[off+1]<<8;off+=2;if(x>end-off)return -1;off+=x;}
 for(unsigned f=8;f<=16;f<<=1)if(a[3]&f){while(off<end&&a[off])off++;if(off==end)return -1;off++;}
 if(a[3]&2){if(off+2>end)return -1;off+=2;}
 if(off>=end)return -1;
 uint8_t *tar=malloc(TAR_LIMIT);if(!tar)return -1;
 unsigned long out=TAR_LIMIT,in=end-off;
 if(puff(tar,&out,a+off,&in)||in!=end-off||out!=le(a+n-4)||hp_bootstrap_crc32(tar,out)!=le(a+n-8)){free(tar);return -1;}
 int result=-1;
 for(size_t pos=0;pos+512<=out;){
  const uint8_t *h=tar+pos;if(!h[0])break;
  size_t size=0;/* sizes are octal ASCII, bounded by archive */
  for(int k=124;k<136&&h[k]&&h[k]!=' ';k++){if(h[k]<'0'||h[k]>'7'||size>TAR_LIMIT/8)goto done;size=size*8+h[k]-'0';}
  if(size>out-pos-512)break;
  if(!memcmp(h,"sihpP1006.img",13)&&h[13]==0){
   const uint8_t *src=h+512;if(size<16||size>256*1024-64||memcmp(src,"20",2)||memcmp(src+8,"\177ELF",4))break;
   static const char prefix[]="\033%-12345X@PJL ENTER LANGUAGE=ACL\r\n",suffix[]="\033%-12345X";
   size_t total=sizeof(prefix)-1+8+size+sizeof(suffix)-1;uint8_t *p=malloc(total);if(!p)break;
   size_t at=sizeof(prefix)-1;memcpy(p,prefix,at);p[at++]=0;p[at++]=0xac;p[at++]=0xc0;p[at++]=0xde;
   uint32_t payload=size-8;p[at++]=payload>>24;p[at++]=payload>>16;p[at++]=payload>>8;p[at++]=payload;
   memcpy(p+at,src,size);at+=size;memcpy(p+at,suffix,sizeof(suffix)-1);*dl=p;*len=total;result=0;break;
  }
  pos+=512+((size+511)/512)*512;
 }
done:free(tar);return result;
}
