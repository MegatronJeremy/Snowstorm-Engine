#include "stub.cpp"
#include "tl.hpp"
using namespace Snowstorm;
GpuScope S(const char*n,uint32_t d,uint64_t s,uint64_t e,GpuQueue q=GpuQueue::Graphics){GpuScope g;g.Name=n;g.Depth=d;g.StartNs=s;g.EndNs=e;g.Queue=q;return g;}
int main(){auto ev=BuildGpuZoneEvents({S("F",0,100,200),S("Sky",1,100,200),S("P",0,200,300)},GpuQueue::Graphics);
for(auto&e:ev)printf("%u %d\n",e.Scope,e.Begin);
printf("%zu\n",BuildGpuZoneEvents({S("E",0,50,50)},GpuQueue::Graphics).size());}
