#ifndef SS_WORKGROUP_HLSLI
#define SS_WORKGROUP_HLSLI

// Compute workgroup tile. Defaults to 8x8; the engine passes SS_WG_X / SS_WG_Y for a non-default size
// (render.wg.* CVars, Render/WorkgroupSize.hpp). The C++ dispatch math must use the same side.
#ifndef SS_WG_X
#define SS_WG_X 8
#endif
#ifndef SS_WG_Y
#define SS_WG_Y 8
#endif

#endif
