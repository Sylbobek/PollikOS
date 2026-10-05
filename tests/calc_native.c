#include "../common/calc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
    const char *expr[]={"2+3*4","(2+3)*4","-2^2","2^-2","2^3^2","0.1+0.2","sqrt(81)","abs(-3.5)","min(7,max(2,4))","200*10%","1e3+4","1e20","1e-10"};
    const char *expected[]={"14","20","-4","0.25","512","0.3","9","3.5","4","20","1004","1e+20","1e-10"};
    char out[96];
    for(unsigned i=0;i<sizeof expr/sizeof *expr;i++){CHECK(calc_evaluate(expr[i],out,sizeof out));CHECK(!strcmp(out,expected[i]));printf("CALC %s = %s\n",expr[i],out);}
    const char *bad[]={"1/0","sqrt(-1)","2^0.5","1e999","1e308*10","min(2)","sqrt(1,2)","1..2","1+","()","nope(1)","","2 3"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)CHECK(!calc_evaluate(bad[i],out,sizeof out));
    char deep[128];memset(deep,'-',126);deep[126]='1';deep[127]=0;CHECK(!calc_evaluate(deep,out,sizeof out));
    CalcModel m;calc_model_init(&m);calc_model_key(&m,'2');calc_model_key(&m,'+');calc_model_key(&m,'3');calc_model_key(&m,13);CHECK(!strcmp(m.result,"5"));
    calc_model_key(&m,'*');calc_model_key(&m,'4');calc_model_key(&m,13);CHECK(!strcmp(m.result,"20"));
    calc_model_button(&m,22);calc_model_key(&m,13);CHECK(!strcmp(m.result,"-20"));
    calc_model_key(&m,'9');calc_model_key(&m,13);CHECK(!strcmp(m.result,"9"));
    calc_model_button(&m,4);calc_model_key(&m,13);CHECK(!strcmp(m.result,"3"));
    calc_model_button(&m,0);for(int i=0;i<300;i++)calc_model_key(&m,'1');CHECK(strlen(m.expression)==127);
    for(int i=0;i<24;i++){CalcRect r=calc_button_rect(320,440,34,i);CHECK(r.x>=0&&r.y>=34&&r.x+r.w<=320&&r.y+r.h<=440);}
    CHECK(!calc_evaluate("1",out,2));
    puts("PASS calculator: precedence, decimals, powers, functions, percent, scientific notation, errors/depth/bounds, button/keyboard model");return 0;
}
