#include "sh2_recomp/function.h"
#include <stdlib.h>
#define CHECK(x) do{if(!(x))abort();}while(0)
extern const Sh2FunctionCode fixture_function_image;
static bool service(Sh2FunctionContext*c){c->fuel=100;return true;}
int main(void){Sh2FunctionContext a={0},b={0};a.fuel=100;a.service=service;a.registers.pr=0x100;
 Sh2Function f=sh2_function_lookup(&fixture_function_image,0);CHECK(f);
 Sh2FunctionResult result=sh2_function_finish_tails(&a,f(&a));
 CHECK(result==SH2_FUNCTION_RETURNED || result==SH2_FUNCTION_EXIT);
 CHECK(a.registers.r[0]==8 && b.registers.r[0]==0);
 CHECK(!sh2_function_lookup(&fixture_function_image,0x100));return 0;}
