#include "nbvh.h"
#include "dynload.h"

static DynamicLoader::RegisterAccelerator<nbvh_accel<4>> r4("nbvh4");
static DynamicLoader::RegisterAccelerator<nbvh_accel<8>> r8("nbvh8");
static DynamicLoader::RegisterAccelerator<nbvh_accel<16>> r16("nbvh16");