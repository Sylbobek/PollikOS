#include "parser.h"
#include <stdlib.h>
long example_parse_decimal(const char *text, int *ok) {
    char *end = 0;
    long value = strtol(text, &end, 10);
    if (ok) *ok = end && *end == 0 && end != text;
    return value;
}
