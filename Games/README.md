# Games

## Games

`Games/` holds games built on the engine, not parts of it. The engine has no knowledge of anything
there; a host opts in by linking a game and calling its one registration function, and both
`Snowstorm-Editor` and `Snowstorm-Runtime` link both games so either scene can be authored and played.

**`Games/Pong` is the example.** It is the whole of what building on
Snowstorm takes: two components (`PongComponents.cpp`, registered exactly as an engine component is),
one system in `SystemPhase::Logic` with `RunsInEditMode() == false` so the scene stays still while it
is authored, a `RegisterPongSystems(World&)` seam, a hand-authored `Pong.world`, and a ~25-line
executable over `GameLayer`. No third-party dependency, no build flag, no licence carve-out. It
self-plays (an unattended paddle tracks the ball), which is what makes it verifiable headlessly: a
`SS_SMOKE_FRAMES` run logs rallies and scores with no keyboard attached.

Doom runs on the engine too, but from [its own repository](https://github.com/MegatronJeremy/Snowstorm-Doom),
which consumes Snowstorm as a submodule. It lives there rather than here because doomgeneric is GPL-2.0
against this project's public domain, and because it is a poor teaching artifact: most of its 1550 lines
are emulator plumbing rather than engine API.

**A host must link a game `WHOLE_ARCHIVE` even if it never calls into it.** Component registrations are
static initializers in TUs nothing references, and the failure is silent in every direction: the
component vanishes from the inspector, `SceneSerializer` skips it with a bare `continue`, and the next
save deletes the block from the `.world`. That is why the editor links both games, not just the one it
is running. `GameRegistrationTests` asserts a game's components survived, and was verified to fail
without the flag.

**A game adds GPU work with `RendererService::EnqueueGraphPass`**, from any phase before Render. The pass
joins that frame's render graph after the texture uploads and ahead of IBL, shadows and every viewport,
once per frame. A texture it writes that a material samples through bindless goes in the pass's `PublishSampled`: the graph
transitions it to Sampled right after the pass (graphics queue only), since it transitions only declared
accesses and a bindless read is undeclared. `PublishSampled` counts as a write in `BuildDependencies`, so a
declared reader still orders after the writer; the ordering of an undeclared bindless reader (the material
draw in `Forward`) still rests on graphics passes keeping their order. `RenderGraphScheduleTests` pin the
edge; the transition itself has not run on a GPU.
