/* SDK identity helpers. */
#include <pollikos/version.h>
#include <pollikos/memory.h>
static const char sdk_version[] = "1.0.0";
const char *pollikos_sdk_version(void) { return sdk_version; }
size_t pollikos_page_size(void) { return 4096; }
