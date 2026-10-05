#ifndef POLLIK_CALC_H
#define POLLIK_CALC_H
/* Bounded, allocation-free expression engine; no kernel or syscall dependency. */
int calc_evaluate(const char *expression, char *output, unsigned capacity);
typedef struct { char expression[128], result[96]; int evaluated; } CalcModel;
void calc_model_init(CalcModel *model);
void calc_model_key(CalcModel *model, char key);
void calc_model_button(CalcModel *model, int button);
const char *calc_button_label(int button);
typedef struct { int x,y,w,h; } CalcRect;
static inline CalcRect calc_button_rect(int width,int height,int top,int button) {
    int cell_w=(width-32)/4,cell_h=(height-top-132)/6;
    return (CalcRect){16+(button%4)*cell_w,top+116+(button/4)*cell_h,cell_w-6,cell_h-6};
}
#endif
