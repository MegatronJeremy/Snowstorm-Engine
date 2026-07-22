// Depth-only shadow pass, fragment stage. No color attachment, nothing to output — depth is written by
// the fixed-function stage. Exists only because the graphics pipeline expects a fragment entry point.
// Paired with Shadow.vert.hlsl.
void main()
{
}
