# Showcase

Feature pages for the engine, each with images and measured numbers. Adapted from [Vuk Đorđević's master's thesis (ETF Belgrade, 2026)](https://github.com/MegatronJeremy/RG2/tree/master/master-rad/teza), updated to the engine as it is now.

Every measured number names its GPU, the commit that holds it and the date. Pages describe the current engine; [`AGENTS.md`](../../AGENTS.md) is the authority where they differ.

| Page | What it shows | Status |
| --- | --- | --- |
| [Hybrid RT pipeline](hybrid-rt-pipeline.md) | Raster G-buffer plus inline ray-query effects, cost per effect | written |
| [RT shadows](rt-shadows.md) | Inline per-light shadows against the experimental stochastic pass | written |
| [RT ambient occlusion](rt-ambient-occlusion.md) | Traced AO against SSAO: term, quality against the path tracer, cost | written |
| Reflections | | not written yet (#250) |
