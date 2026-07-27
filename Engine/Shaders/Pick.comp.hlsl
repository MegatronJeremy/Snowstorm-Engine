// GPU editor picking (RT). One thread traces a single ray — the camera→cursor world ray built on the CPU —
// against the scene TLAS and writes the committed instance's custom index (== TlasBuildSystem's per-entity
// build order, VulkanTlas sets instanceCustomIndex = i) so the editor can map it back to an entt::entity.
// A miss writes 0xFFFFFFFF (sentinel: nothing under the cursor → deselect). Compiled only in the
// SS_RAYTRACING permutation (RayQuery needs the inline-ray-query capability + the device extension); on a
// non-RT device the editor uses the CPU AABB path instead and never dispatches this.
//
// SceneTLAS lives in the engine's bindless set 3 (t2,space3), the SAME slot DefaultLit reads. The compute
// pipeline gap-fills set 3 from VulkanBindlessManager (see VulkanComputePipeline::Build), so no per-pick
// TLAS binding is needed — TlasBuildSystem already wrote the slot this frame.

RWStructuredBuffer<uint> Result : register(u0, space0); // [0] = committed instance index, or 0xFFFFFFFF on miss

cbuffer PickCB : register(b1, space0)
{
	float3 RayOrigin; // world-space camera position
	float RayTMax;    // far distance to trace
	float3 RayDir;    // world-space, normalized, through the clicked pixel
	float _Pad;
};

#ifdef SS_RAYTRACING
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);
#endif

[numthreads(1, 1, 1)]
void main()
{
#ifdef SS_RAYTRACING
	RayDesc ray;
	ray.Origin = RayOrigin;
	ray.Direction = RayDir;
	ray.TMin = 0.0;
	ray.TMax = RayTMax;

	// Closest hit (no ACCEPT_FIRST_HIT): picking wants the front-most instance under the cursor, so let the
	// query find the nearest committed triangle. CULL_NON_OPAQUE matches the shadow trace (alpha-tested
	// geometry isn't handled here — an instance-granular pick doesn't need sub-triangle alpha precision).
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();

	Result[0] = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? q.CommittedInstanceID() : 0xFFFFFFFFu;
#else
	Result[0] = 0xFFFFFFFFu; // no RT device: never dispatched, but keep the write well-defined
#endif
}
