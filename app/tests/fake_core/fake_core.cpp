// Stand-in for a core's generated code: an image table and nothing else.
#include "mc360_init.h"

struct PPCFuncMapping {
  unsigned long long guest;
  void* host;
};
static PPCFuncMapping kMappings[] = {{0, nullptr}};

const rex::PPCImageInfo PPCImageConfig = {
    .code_base = 0x82100000,
    .code_size = 0x800000,
    .image_base = 0x82000000,
    .image_size = 0x1000000,
    .func_mappings = kMappings,
};
