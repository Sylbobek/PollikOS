#include "calc.h"
typedef struct { const char *p; const char *error; unsigned depth; } Parser;
static void space(Parser *p) { while(*p->p==' '||*p->p=='\t')p->p++; }
static int take(Parser *p,char c) { space(p);if(*p->p!=c)return 0;p->p++;return 1; }
static double sum(Parser *p);
static double unary(Parser *p);
static double finite(Parser *p,double v) {
    if(!__builtin_isfinite(v))p->error="Number out of range";
    return v;
}
static int name_is(const char *a,unsigned n,const char *b) {
    unsigned i=0;while(i<n&&b[i]&&a[i]==b[i])i++;return i==n&&!b[i];
}
/* Exact binary64 integer validation; no float-to-int conversion of NaN or
 * overflow, and no dependency on x87 comparison/control-word intermediates.
 * All supported host/kernel/SDK targets use IEEE binary64 doubles. */
static int integer_exponent(double value,int *out) {
    union { double value; unsigned long long bits; } u={value};
    unsigned hi=(unsigned)(u.bits>>32),lo=(unsigned)u.bits;
    unsigned exp=(hi>>20)&2047;
    if(!(hi&0x7fffffff)&&!lo) {*out=0;return 1;}
    if(exp<1023||exp>1033)return 0;
    unsigned shift=20-(exp-1023);
    if(lo||(hi&((1u<<shift)-1)))return 0;
    unsigned n=(0x100000u|(hi&0xfffffu))>>shift;
    if(n>1024)return 0;
    *out=(hi>>31)?-(int)n:(int)n;return 1;
}
static double primary(Parser *p) {
    if(++p->depth>32){p->error="Expression too deep";p->depth--;return 0;}
    space(p);double v=0;
    if(take(p,'(')) {v=sum(p);if(!take(p,')'))p->error="Expected closing parenthesis";}
    else if((*p->p>='a'&&*p->p<='z')) {
        const char *name=p->p;while(*p->p>='a'&&*p->p<='z')p->p++;
        unsigned n=(unsigned)(p->p-name);
        if(name_is(name,n,"pi"))v=3.141592653589793;
        else if(name_is(name,n,"e"))v=2.718281828459045;
        else if(take(p,'(')) {
            double a=sum(p),b=0;int two=take(p,',');if(two)b=sum(p);
            if(!take(p,')'))p->error="Expected closing parenthesis";
            if(name_is(name,n,"sqrt")&&!two) {
                if(a<0)p->error="Square root of negative number";
                else v=__builtin_sqrt(a);
            } else if(name_is(name,n,"abs")&&!two)v=a<0?-a:a;
            else if(name_is(name,n,"min")&&two)v=a<b?a:b;
            else if(name_is(name,n,"max")&&two)v=a>b?a:b;
            else p->error="Unknown function or wrong arguments";
        } else p->error="Unknown name";
    } else {
        unsigned digits=0;
        while(*p->p>='0'&&*p->p<='9'){v=v*10+*p->p++-'0';digits++;}
        if(*p->p=='.') {
            p->p++;double factor=.1;
            while(*p->p>='0'&&*p->p<='9'){v+=(*p->p++-'0')*factor;factor*=.1;digits++;}
        }
        if(!digits)p->error="Expected a number";
        if(*p->p=='e'||*p->p=='E') {
            p->p++;int negative=take(p,'-');if(!negative)(void)take(p,'+');
            unsigned exponent=0,count=0;
            while(*p->p>='0'&&*p->p<='9'){
                if(exponent<1000)exponent=exponent*10+*p->p-'0';p->p++;count++;
            }
            if(!count||exponent>308)p->error="Invalid exponent";
            else while(exponent--)v=negative?v/10:v*10;
        }
    }
    p->depth--;return finite(p,v);
}
static double power(Parser *p) {
    double v=primary(p);
    while(!p->error&&take(p,'%'))v/=100;
    if(!p->error&&take(p,'^')) {
        double exponent=unary(p);int n;
        if(p->error)return 0;
        if(!integer_exponent(exponent,&n)) {
            p->error="Power requires integer exponent (-1024..1024)";return 0;
        }
        int negative=n<0;if(negative)n=-n;
        if(negative&&v==0){p->error="Division by zero";return 0;}
        double base=negative?1/v:v,result=1;
        while(n&&!p->error){if(n&1)result=finite(p,result*base);n>>=1;if(n)base=finite(p,base*base);}
        v=result;
    }
    return finite(p,v);
}
static double unary(Parser *p) {
    if(++p->depth>32){p->error="Expression too deep";p->depth--;return 0;}
    double v;
    if(take(p,'-'))v=-unary(p);else if(take(p,'+'))v=unary(p);else v=power(p);
    p->depth--;return v;
}
static double product(Parser *p) {
    double v=unary(p);
    while(!p->error) {
        if(take(p,'*'))v=finite(p,v*unary(p));
        else if(take(p,'/')){double rhs=unary(p);if(rhs==0)p->error="Division by zero";else v=finite(p,v/rhs);}
        else break;
    }
    return v;
}
static double sum(Parser *p) {
    double v=product(p);
    while(!p->error) {
        if(take(p,'+'))v=finite(p,v+product(p));
        else if(take(p,'-'))v=finite(p,v-product(p));
        else break;
    }
    return v;
}
static void copy_text(char *out,unsigned cap,const char *s) {
    if(!cap)return;unsigned n=0;while(s[n]&&n+1<cap){out[n]=s[n];n++;}out[n]=0;
}
static void format(double value,char *out,unsigned cap) {
    char s[80];unsigned n=0;
    if(value<0){s[n++]='-';value=-value;}
    int exponent=0,scientific=value!=0&&(value>=1e12||value<1e-6);
    if(scientific) {
        while(value>=10){value/=10;exponent++;}
        while(value<1){value*=10;exponent--;}
    }
    value+=.0000000005; /* nine decimal places, rounded, not truncated */
    if(scientific&&value>=10){value/=10;exponent++;}
    double scale=1;while(scale*10<=value)scale*=10;
    do {int digit=(int)(value/scale);if(digit>9)digit=9;s[n++]=(char)('0'+digit);value-=digit*scale;scale/=10;}while(scale>=1);
    s[n++]='.';
    for(unsigned i=0;i<9;i++) {value*=10;int digit=(int)value;if(digit>9)digit=9;s[n++]=(char)('0'+digit);value-=digit;}
    while(n&&s[n-1]=='0')n--;if(n&&s[n-1]=='.')n--;
    if(n==2&&s[0]=='-'&&s[1]=='0'){s[0]='0';n=1;}
    if(scientific){s[n++]='e';s[n++]=exponent<0?'-':'+';if(exponent<0)exponent=-exponent;
        if(exponent>=100)s[n++]=(char)('0'+exponent/100);
        if(exponent>=10)s[n++]=(char)('0'+exponent/10%10);
        s[n++]=(char)('0'+exponent%10);
    }
    s[n]=0;copy_text(out,cap,s);
}
int calc_evaluate(const char *expression,char *output,unsigned capacity) {
    if(!expression||!output||capacity<80)return 0;
    unsigned length=0;while(expression[length]&&length<128)length++;
    if(!length||length>=128){copy_text(output,capacity,"Expression must contain 1..127 characters");return 0;}
    Parser p={expression,0,0};double v=sum(&p);space(&p);
    if(!p.error&&*p.p)p.error="Unexpected input";
    if(p.error){copy_text(output,capacity,p.error);return 0;}
    format(v,output,capacity);return 1;
}
void calc_model_init(CalcModel *m){m->expression[0]=0;copy_text(m->result,sizeof m->result,"0");m->evaluated=0;}
static void append(CalcModel *m,const char *s){unsigned n=0;while(m->expression[n])n++;while(*s&&n+1<sizeof m->expression)m->expression[n++]=*s++;m->expression[n]=0;}
void calc_model_key(CalcModel *m,char key) {
    if(key==27){calc_model_init(m);return;}
    if(key==8){unsigned n=0;while(m->expression[n])n++;if(n)m->expression[n-1]=0;m->evaluated=0;return;}
    if(key==10||key==13||key=='='){m->evaluated=calc_evaluate(m->expression,m->result,sizeof m->result);return;}
    if(key<32||key>126)return;
    if(m->evaluated) {
        if(key=='+'||key=='-'||key=='*'||key=='/'||key=='^'||key=='%')copy_text(m->expression,sizeof m->expression,m->result);
        else m->expression[0]=0;
        m->evaluated=0;
    }
    char s[]={key,0};append(m,s);
}
static const char *labels[24]={"C","DEL","(",")","sqrt","^","%","/","7","8","9","*","4","5","6","-","1","2","3","+","0",".","+/-","="};
const char *calc_button_label(int button){return button>=0&&button<24?labels[button]:"";}
void calc_model_button(CalcModel *m,int button) {
    if(button<0||button>=24)return;
    if(button==0)calc_model_key(m,27);else if(button==1)calc_model_key(m,8);
    else if(button==4){if(m->evaluated){copy_text(m->expression,sizeof m->expression,"sqrt(");append(m,m->result);append(m,")");m->evaluated=0;}else append(m,"sqrt(");}
    else if(button==22){char old[128];copy_text(old,sizeof old,m->evaluated?m->result:m->expression);calc_model_init(m);append(m,"-(");append(m,old);append(m,")");}
    else calc_model_key(m,labels[button][0]);
}
