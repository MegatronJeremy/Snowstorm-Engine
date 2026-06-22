# Vulkan backend review — frame loop, swapchain, device

Focused correctness review of the Vulkan RHI (`Platform/Vulkan/`), done while a build ran. These
are reasoned from the code, not run-verified — but they're well-known Vulkan patterns, so confidence
is high. References are `file:line`.

The headline: **window resize is completely unhandled**, and the validation messenger
**hard-asserts on benign messages**. Both are latent — they may not fire in the day-to-day editor
workflow (maximized window, few validation warnings), which is exactly why they've survived.

---

## Critical

### 1. Window resize is unhandled → deadlock + validation errors
There is **no swapchain recreation anywhere** — `VulkanContext` has `Init` and `Shutdown` but no
`RecreateSwapchain` (`VulkanContext.cpp`), and `BeginFrame` literally says
`// Handle resize logic here later` (`VulkanRendererAPI.cpp:116`). Three compounding problems:

- **Fence deadlock.** `BeginFrame` resets the in-flight fence (`VulkanRendererAPI.cpp:103`) *before*
  acquiring. If `vkAcquireNextImageKHR` returns `VK_ERROR_OUT_OF_DATE_KHR` it returns early
  (`:115-118`) **without submitting work that signals the fence**. The next frame's
  `vkWaitForFences` (`:102`) then waits forever → hard hang.
- **Present result ignored.** `vkQueuePresentKHR`'s return is discarded (`:167`); `OUT_OF_DATE` /
  `SUBOPTIMAL` should trigger recreation.
- **No recreation on `WindowResize`.** `Application::OnWindowResize` calls `m_Window->Resize` but
  nothing recreates the swapchain/extent, so the swapchain and the OS window desync.

Net effect: resizing the OS window hangs or spams validation. It likely hasn't bitten you because
the editor runs maximized and panel resizing goes through `ViewportResizeSystem` (offscreen RTs),
not the swapchain. **This is the same area as issue #4 (runtime present path)** — both need a real
swapchain/present path. Worth its own issue.

### 2. The validation messenger hard-asserts on *every* severity
The debug callback enables `VERBOSE | WARNING | ERROR` (`VulkanContext.cpp:126-128`) but its body
unconditionally does `SS_CORE_ERROR(...)` + `SS_CORE_ASSERT(false, "Validation failed!")`
(`:137-139`). So the **first benign info/warning crashes debug builds**. Only `ERROR` (and arguably
`WARNING`) should assert; verbose/info should log. As written, enabling validation cleanly is fragile.

---

## Correctness

### 3. `render-finished` semaphore is per-frame-in-flight, not per-image
`m_RenderFinishedSemaphores` is sized to `s_MaxFramesInFlight` and indexed by `m_CurrentFrameIndex`
(`VulkanRendererAPI.cpp:50, 150, 160`), but it's the **present wait semaphore**. Because present
consumes it asynchronously and swapchain image count (≥`minImageCount`) can exceed frames-in-flight,
the semaphore can be reused while a prior present still references it — a Khronos-documented hazard
(the official tutorial switched to **one render-finished semaphore per swapchain image**). Expect a
`VUID` validation error here once validation is active and you present across resizes.

### 4. Device selection has no discrete-GPU preference
The loop picks the **first** device with graphics+present (`VulkanContext.cpp:155-176`). On a
multi-GPU machine — yours has an **AMD RX 9060 XT + Intel HD 4600 + a Parsec virtual adapter** — this
can pick the integrated GPU or the virtual adapter. Prefer `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`
(fall back to others).

### 5. `samplerAnisotropy` is force-enabled regardless of support
`:191-195` checks support and warns if missing, then `:197` sets
`enabledFeatures.samplerAnisotropy = VK_TRUE` **unconditionally**, making the check dead code. On a
GPU without it, device creation/validation fails. Set it only when `supportedFeatures` reports it.

### 6. Swapchain uses the bare `minImageCount`
`swapInfo.minImageCount = caps.minImageCount` (`:306`). With FIFO + 2 frames in flight, requesting
the bare minimum can stall `vkAcquireNextImageKHR`. Standard practice is `minImageCount + 1`, clamped
to `maxImageCount` (when non-zero).

### 7. ImGui image counts are hardcoded and inconsistent
`ImGui_ImplVulkan_InitInfo` is told `MinImageCount = 2`, `ImageCount = 3`
(`VulkanRendererAPI.cpp:257-258`), independent of the actual swapchain image count from
`VulkanContext`. Should be derived from the real count (and re-set on recreation).

---

## Minor / robustness

- **`currentExtent == 0xFFFFFFFF` not handled** (`VulkanContext.cpp:282`): some platforms report this
  to mean "you choose"; should clamp the framebuffer size to `min/maxImageExtent`.
- **`RenderImGuiDrawData` logic is backwards** (`VulkanRendererAPI.cpp:319`): it calls
  `ImGui::Render()` only when `GetDrawData() == nullptr`, which risks rendering stale draw data.
- **`GetSwapchainTarget` allocates a new `RenderTarget` every call** (`:188-205`) — invoked per frame
  by `RenderSystem`.
- **`VK_KHR_DYNAMIC_RENDERING` extension is redundant** with `features13.dynamicRendering` under a
  1.3 instance (`VulkanContext.cpp:202`) — harmless, just noise.
- **Single queue** (graphics == present); no dedicated transfer queue, which ties into the scattered
  `vkDeviceWaitIdle` uploads (issue #11).

---

## Suggested issues (not yet filed)

| Priority | Title | Refs |
| --- | --- | --- |
| critical | Handle window resize / swapchain recreation (fixes fence deadlock + present result) | `VulkanRendererAPI.cpp:96-171`, `VulkanContext.cpp` |
| critical | Validation messenger should only assert on ERROR severity | `VulkanContext.cpp:126-140` |
| high | Per-swapchain-image render-finished semaphores | `VulkanRendererAPI.cpp:50,150,160` |
| high | Prefer discrete GPU in device selection | `VulkanContext.cpp:155-176` |
| medium | Don't force-enable samplerAnisotropy when unsupported | `VulkanContext.cpp:191-197` |
| medium | Request `minImageCount + 1` swapchain images | `VulkanContext.cpp:306` |
| low | Derive ImGui image counts from the swapchain | `VulkanRendererAPI.cpp:257-258` |

These overlap with the runtime present-path work (#4) — fixing resize/recreation is a natural
prerequisite for a robust runtime. Say the word and I'll file them as issues + add them to the board.
