#include <stdlib.h>
/* As in ISO C, negating the minimum signed value is outside the contract. */
int abs(int value){return value<0?-value:value;}
long labs(long value){return value<0?-value:value;}
long long llabs(long long value){return value<0?-value:value;}
