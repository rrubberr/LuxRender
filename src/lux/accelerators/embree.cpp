#include "embree.h"
#include "dynload.h"

static lux::DynamicLoader::RegisterAccelerator<lux::embree_accel> r("embree");