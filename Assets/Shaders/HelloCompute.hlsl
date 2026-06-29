// Minimal compute shader to verify the compute pipeline path (Phase 2): create -> bind -> dispatch
// with no descriptors. Does no useful work; exists purely so the engine can prove a VK_PIPELINE_
// BIND_POINT_COMPUTE pipeline builds and dispatches cleanly under validation. Storage-image output
// (real work) arrives in Phase 3.

#type compute

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
}
