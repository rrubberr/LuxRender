// Phase 5: compile smoke test for the store consumer + walker driver headers.
// Explicit instantiation forces the template bodies to type-check without
// needing a live scene. Behaviour is verified by the render check.
#include "integrators/wf/pool_view.h"
#include "integrators/consumers/bidirectional_consumer.h"
#include "integrators/wf/walkers.h"

#include "core/bsdfptr_table.h"
#include "core/camera.h"
#include "core/mediumptr_table.h"
#include "core/sampler.h"

#include <iostream>

using namespace lux2;
using namespace lux2::wf;

// Force the independent-stage driver bodies to compile (never called).
namespace lux2::wf {
template bool StepEyeOnce<Sampler>(const WalkCtx &, PathStore &, SampleTable &,
                                   BidirEyeConsumer &, const Camera &,
                                   const Sampler &, JobCursor &, uint64_t,
                                   Scheduler &, uint32_t);
template bool StepLightOnce<Sampler>(const WalkCtx &, PathStore &, SampleTable &,
                                     BidirLightConsumer &, const Sampler &,
                                     uint32_t, Scheduler &);
} // namespace lux2::wf

int main()
{
    BlockArena arena;
    PathStore ps;
    SampleTable tbl;
    ps.Init(&arena, 4);
    tbl.Init(4);

    WalkCtx ctx;
    ctx.eyeDepth = 6;
    ctx.lightDepth = 6;

    BidirEyeConsumer cons(ctx, &ps, &tbl);
    // Touch the accessors so their bodies instantiate.
    (void)cons.Bounce(LaneCore{});
    (void)cons.DepthBase(LaneCore{});

    // LoadVertex needs real tables to gather through; a default-constructed
    // BsdfPtrTable / MediumPtrTable is enough to type-check the body.
    BsdfPtrTable btable;
    MediumPtrTable mtable;
    ctx.table = &btable;
    ctx.mediumTable = &mtable;
    (void)LoadVertex(ctx, ps, UInt32P(0u), Int32P(0), MaskP(false));

    std::cout << "bidirectional_consumer compiles" << std::endl;
    return 0;
}
