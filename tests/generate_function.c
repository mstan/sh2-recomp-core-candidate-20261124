#include "sh2_recomp/function.h"
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=2)return 1;FILE*f=fopen(argv[1],"w");if(!f)return 2;
 const uint8_t bytes[]={0xe0,5,0x70,3,0x00,0x0b,0x00,0x09};const uint32_t entry=0;
 Sh2FunctionImage image={.base=0,.bytes=bytes,.size=sizeof bytes,.entries=&entry,.entry_count=1};
 bool ok=sh2_emit_functions(f,stderr,"fixture_function",&image);fclose(f);return ok?0:3;
}
