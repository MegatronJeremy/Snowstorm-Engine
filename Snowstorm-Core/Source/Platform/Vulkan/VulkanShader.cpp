#include "VulkanShader.hpp"

#include "Snowstorm/Utility/EnginePaths.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/ShaderCompile.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Snowstorm
{
	namespace fs = std::filesystem;

	namespace
	{
		std::string ReadTextFileOrEmpty(const fs::path& p)
		{
			std::ifstream in(p, std::ios::in);
			if (!in.is_open())
				return {};

			std::stringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}

		std::string ToHex64(const uint64_t v)
		{
			return ShaderKeyToHex(v);
		}

		fs::path GetDxcExePath()
		{
			return GetEngineRoot() / "Tools" / "dxc" / "dxc.exe";
		}

		// Fold every shared .hlsli header into the cache key. The cache key otherwise hashes only the
		// top-level .hlsl source, so editing an included header (e.g. Engine.hlsli, which defines the
		// FrameCB/MaterialCB layouts shared by all shaders) would NOT invalidate the cached SPIR-V —
		// the stale shader keeps the old cbuffer layout while C++ uploads the new one, silently
		// corrupting every cbuffer read (symptom: black/unlit geometry). A full #include dependency
		// parse is overkill for a single flat include dir; hashing all headers in it is a deliberately
		// smaller version that catches any header edit. Headers are sorted and hashed with their names, so the hash is order-stable.
		uint64_t HashIncludeHeaders()
		{
			const fs::path includeDir = GetEngineRoot() / "Engine" / "Shaders" / "Include";
			std::error_code ec;
			if (!fs::exists(includeDir, ec))
			{
				return 0;
			}

			std::vector<fs::path> headers;
			for (const auto& entry : fs::directory_iterator(includeDir, ec))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".hlsli")
				{
					headers.push_back(entry.path());
				}
			}
			std::ranges::sort(headers);

			uint64_t h = 0;
			for (const fs::path& header : headers)
			{
				const std::string text = ReadTextFileOrEmpty(header);
				h = ShaderHeaderAppend(h, header.filename().string(), text);
			}
			return h;
		}

		fs::path GetShaderCacheDir()
		{
			return GetEngineRoot() / "Engine" / "cache" / "shaders";
		}

		bool RunProcessCaptureExitCode(const std::wstring& exePath,
		                               const std::wstring& cmdLine,
		                               DWORD& outExitCode,
		                               std::string& outCombinedOutput)
		{
			outCombinedOutput.clear();

			SECURITY_ATTRIBUTES sa{};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = nullptr;

			HANDLE readPipe = nullptr;
			HANDLE writePipe = nullptr;

			if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
			{
				const DWORD err = GetLastError();
				SS_CORE_ERROR("CreatePipe failed. Win32 error={}", static_cast<uint32_t>(err));
				return false;
			}

			// Ensure the read end is NOT inherited (only the write end should be inherited by the child)
			if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
			{
				const DWORD err = GetLastError();
				SS_CORE_ERROR("SetHandleInformation failed. Win32 error={}", static_cast<uint32_t>(err));
				CloseHandle(readPipe);
				CloseHandle(writePipe);
				return false;
			}

			STARTUPINFOW si{};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			si.hStdOutput = writePipe;
			si.hStdError = writePipe; // combined output

			PROCESS_INFORMATION pi{};

			std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
			mutableCmd.push_back(L'\0');

			const BOOL ok = CreateProcessW(
			    exePath.c_str(),
			    mutableCmd.data(),
			    nullptr, nullptr,
			    TRUE, // inherit handles (needed for redirected std handles)
			    CREATE_NO_WINDOW,
			    nullptr,
			    nullptr,
			    &si, &pi);

			// Parent no longer needs the write end
			CloseHandle(writePipe);
			writePipe = nullptr;

			if (!ok)
			{
				const DWORD err = GetLastError();
				SS_CORE_ERROR("Failed to run process. Win32 error={}", static_cast<uint32_t>(err));
				CloseHandle(readPipe);
				return false;
			}

			// Read output while the process runs (simple blocking loop)
			std::string output;
			output.reserve(4096);

			char buffer[4096];
			for (;;)
			{
				DWORD bytesAvailable = 0;
				if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
				{
					break;
				}

				if (bytesAvailable > 0)
				{
					DWORD bytesRead = 0;
					const DWORD toRead = (bytesAvailable < sizeof(buffer)) ? bytesAvailable : static_cast<DWORD>(sizeof(buffer));
					if (ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) && bytesRead > 0)
					{
						output.append(buffer, buffer + bytesRead);
						continue;
					}
				}

				// If no output is available, see if process ended.
				const DWORD waitRes = WaitForSingleObject(pi.hProcess, 10);
				if (waitRes == WAIT_OBJECT_0)
				{
					// Drain remaining output
					for (;;)
					{
						DWORD bytesRead = 0;
						if (!ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) || bytesRead == 0)
							break;
						output.append(buffer, buffer + bytesRead);
					}
					break;
				}
			}

			DWORD exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);

			CloseHandle(readPipe);
			readPipe = nullptr;

			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);

			outExitCode = exitCode;
			outCombinedOutput = std::move(output);
			return true;
		}

		bool CompileStageWithDxc(const fs::path& dxcExe,
		                         const fs::path& inputHlsl,
		                         const fs::path& outputSpv,
		                         const wchar_t* profile,
		                         const ShaderDefines& defines,
		                         bool debug)
		{
			const std::wstring exe = dxcExe.wstring();
			const std::wstring out = outputSpv.wstring();

			// Include base is the shaders root; shaders reference headers as "Include/Foo.hlsli", relative to
			// the file so the IDE resolves them too. -I is a fallback.
			DxcRequest request;
			request.dxcExe = exe;
			request.profile = profile;
			request.input = inputHlsl.wstring();
			request.output = out;
			request.includeDir = (GetEngineRoot() / "Engine" / "Shaders").wstring();
			request.defines = defines;
			request.debug = debug;
			const std::wstring cmd = BuildDxcCommandLine(request);

			DWORD exitCode = 1;
			std::string dxcOutput;

			if (!RunProcessCaptureExitCode(exe, cmd, exitCode, dxcOutput))
				return false;

			// Log output if DXC said anything (warnings etc.)
			if (!dxcOutput.empty())
			{
				// If you prefer, you can gate this to only failures.
				SS_CORE_INFO("DXC output:\n{}", dxcOutput);
			}

			if (exitCode != 0)
			{
				// Explicit narrowing for the (ASCII) command line, only for this error log.
				std::string narrowCmd;
				narrowCmd.reserve(cmd.size());
				for (const wchar_t wc : cmd)
					narrowCmd.push_back(static_cast<char>(wc));
				SS_CORE_ERROR("DXC failed (exit code {}). Command: {}", static_cast<uint32_t>(exitCode), narrowCmd);
				return false;
			}

			if (!fs::exists(outputSpv))
			{
				SS_CORE_ERROR("DXC reported success but output file does not exist: {}", outputSpv.string());
				return false;
			}

			return true;
		}

		// Compile a single-stage HLSL file DIRECTLY (no #type split, no temp file — the file itself is a
		// plain single-`main` shader) to a cached .spv for the given DXC profile. `flagsTag` distinguishes
		// stage profiles in the cache key so a vert and frag of the same content don't collide. Returns
		// false (outSpv empty) on failure.
		bool CompileStageFileToSpirvCache(const std::string& sourcePath, const wchar_t* profile,
		                                  const char* flagsTag, const ShaderDefines& defines, bool debug,
		                                  std::string& outSpv)
		{
			// Shader load paths are engine-relative (e.g. "Engine/Shaders/Foo.hlsl"). Resolve against the
			// engine root (exe-relative) so a moved/packaged exe still finds them; an absolute path (rare)
			// passes through unchanged.
			// Engine shaders live under the engine root; a game's shaders live in its own project. See
			// ResolveShaderSource.
			const fs::path srcPath = ResolveShaderSource(sourcePath);
			const fs::path cacheDir = GetShaderCacheDir();
			fs::create_directories(cacheDir);

			const std::string fullText = ReadTextFileOrEmpty(srcPath);
			if (fullText.empty())
			{
				SS_CORE_ERROR("Shader source is empty or missing: {}", sourcePath);
				return false;
			}

			// Defines and the opt level key the cache so variants never collide on one .spv.
			const uint64_t h = ComputeShaderKey(fullText, HashIncludeHeaders(), flagsTag, defines, debug);

			const std::string key = ToHex64(h);
			const std::string stem = srcPath.stem().string(); // e.g. "Mesh.vert" -> "Mesh.vert"
			const fs::path outSpvPath = cacheDir / (stem + "_" + key + ".spv");

			if (fs::exists(outSpvPath))
			{
				outSpv = outSpvPath.string();
				return true;
			}

			// dxc is only needed to COMPILE, so it is checked here rather than on entry. It used to be
			// required before the cache was consulted, which made a 15 MB tool a hard dependency of a run
			// that never invokes it: a build shipping a warm shader cache had to carry it anyway.
			//
			// The source text is still read above, because the cache key is derived from it. Dropping that
			// too needs a cook index recording key-per-shader, which is a larger change than this one.
			const fs::path dxcExe = GetDxcExePath();
			if (!fs::exists(dxcExe))
			{
				SS_CORE_ERROR("DXC not found at {} (needed to compile {}; no cached SPIR-V for it)",
				              dxcExe.string(), sourcePath);
				return false;
			}

			// Now that shader compilation runs on JobSystem workers, the SAME output .spv can be requested by
			// two workers at once — e.g. Mesh.vert.hlsl is shared by the DefaultLit and Mandelbrot pipelines,
			// so both hash to the identical output path. Two dxc.exe processes writing that file concurrently
			// corrupt it / fail. Serialize per output path: hold a per-key lock across the exists-check +
			// compile, so the first compiles and the second sees the finished file. A global map of these
			// locks is guarded by its own mutex. (Keyed by the full output path, so distinct shaders never
			// contend.)
			static std::mutex s_MapMutex;
			static std::unordered_map<std::string, std::unique_ptr<std::mutex>> s_PathLocks;
			std::mutex* pathLock = nullptr;
			{
				std::lock_guard mapLock(s_MapMutex);
				auto& slot = s_PathLocks[outSpvPath.string()];
				if (!slot)
				{
					slot = std::make_unique<std::mutex>();
				}
				pathLock = slot.get();
			}

			std::lock_guard compileLock(*pathLock);

			// Re-check under the lock: another worker may have finished this exact output while we waited.
			if (fs::exists(outSpvPath))
			{
				outSpv = outSpvPath.string();
				return true;
			}

			// Compile the real source file directly (DXC resolves #include via -I; the file is unmodified).
			// dxc writes -Fo incrementally, and the unlocked fs::exists fast path above (plus a concurrent
			// process, or a killed earlier run) would treat a half-written file as a finished one. So dxc
			// writes a private temp and the finished file is renamed into place, which readers see whole or
			// not at all.
			const std::string tmpSuffix = "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId()) + ".tmp";
			const fs::path tmpSpvPath = outSpvPath.string() + tmpSuffix;
			std::error_code ec;
			fs::remove(tmpSpvPath, ec);
			if (!CompileStageWithDxc(dxcExe, srcPath, tmpSpvPath, profile, defines, debug))
			{
				fs::remove(tmpSpvPath, ec);
				return false;
			}
			fs::rename(tmpSpvPath, outSpvPath, ec);
			if (ec)
			{
				// Windows rename fails if another process won the race and the target is in use; the
				// content is identical by construction, so the existing file is the result.
				fs::remove(tmpSpvPath, ec);
				if (!fs::exists(outSpvPath))
				{
					SS_CORE_ERROR("Could not publish compiled shader {}", outSpvPath.string());
					return false;
				}
			}

			outSpv = outSpvPath.string();
			return true;
		}

	}

	// NOTE: the constructors deliberately do NOT compile. Compilation spawns dxc.exe (seconds on a cold
	// cache) and is kicked onto a JobSystem worker by ShaderLibrary::Load right after construction, so the
	// object exists immediately in a not-ready state and the main thread never blocks. Compile() is called
	// on the worker (and synchronously by Recompile() for hot-reload).
	VulkanShader::VulkanShader(std::string filepath, ShaderDefines features)
	    : m_Filepath(std::move(filepath)), m_FeatureDefines(std::move(features))
	{
		m_VertPath = m_Filepath; // single-path == a compute shader
	}

	VulkanShader::VulkanShader(std::string vertPath, std::string fragPath, ShaderDefines features)
	    : m_Filepath(vertPath + "|" + fragPath) // composite label for logging / library keying
	      ,
	      m_VertPath(std::move(vertPath)), m_FragPath(std::move(fragPath)), m_FeatureDefines(std::move(features))
	{
	}

	std::string VulkanShader::GetCompiledPath(const ShaderStageKind stage) const
	{
		// Empty until Compile() finishes (m_Ready). Callers gate on IsReady()/non-empty before building a
		// pipeline; returning empty here is the "not ready yet" signal, not an error.
		std::lock_guard lock(m_Mutex);
		switch (stage)
		{
		case ShaderStageKind::Vertex:
			return m_Compiled.Vert;
		case ShaderStageKind::Fragment:
			return m_Compiled.Frag;
		case ShaderStageKind::Compute:
			return m_Compiled.Comp;
		}
		return {};
	}

	void VulkanShader::Compile()
	{
		// Runs on a JobSystem worker (or synchronously via Recompile). The slow dxc.exe work happens OUTSIDE
		// the lock; only the short result-publish is locked, then m_Ready is released so a main-thread reader
		// sees a fully-populated result or nothing. m_VertPath/m_FragPath are immutable post-construction, so
		// reading them here is race-free.

		// Build this compile's variant define list from the shader's permutation intent. THIS is the single
		// place a new permutation axis is added (one push_back) — the compile chain + cache key are generic
		// over the list, so nothing downstream changes. Read once here so both stages + the cache key agree.
		ShaderDefines defines = m_FeatureDefines; // call-site permutation, then dynamic, then capability axes
		{
			std::lock_guard lock(m_DefinesMutex);
			defines.insert(defines.end(), m_DynamicDefines.begin(), m_DynamicDefines.end());
		}
		// RT axis (#118): emit SS_RAYTRACING only when the device supports RT AND the selector isn't
		// ForceNonRT. A non-RT device never emits RT regardless (effects are force-off there anyway).
		if (Renderer::IsRayTracingSupported() &&
		    m_Permutation.load(std::memory_order_relaxed) != ShaderPermutation::ForceNonRT)
		{
			defines.emplace_back("SS_RAYTRACING=1");
		}

		// fp16 capability axis (# fp16 inference): emit SS_FP16 when the device supports fp16 shader math +
		// 16-bit storage. Only NeuralConv.comp wraps its Weights buffer in #ifdef SS_FP16; every other shader
		// ignores the define (no #ifdef), so this is a no-op for them but keeps the .spv cache key correct
		// (an fp16-capable machine caches distinct SPIR-V). A non-fp16 device never emits it -> the fp32 path.
		if (Renderer::IsFloat16Supported())
		{
			defines.emplace_back("SS_FP16=1");
		}

		// Shader optimization mode (render.shaders.debug, Unreal r.Shaders.Optimize model). Read once here so
		// both stages of a graphics shader + the cache key agree. NOT a define (that list also feeds -D args);
		// it's a separate compile flag that keys the cache on its own.
		const bool debug = CVars::ShadersDebug.Get();

		// Two-path graphics: separate vertex + fragment files, each a plain single-`main` HLSL file
		// compiled directly (no #type split). The preferred form.
		if (!m_FragPath.empty())
		{
			std::string vert, frag;
			// SM 6.5 (was 6.0): the fragment stage may use inline ray query for RT sun shadows (#118); it's a
			// strict superset so vertex/fragment shaders that don't use it compile identically.
			if (!CompileStageFileToSpirvCache(m_VertPath, L"vs_6_5", "v5_vulkan1.2_dxlayout_Zpr_preservebind_vs65", defines, debug, vert) ||
			    !CompileStageFileToSpirvCache(m_FragPath, L"ps_6_5", "v5_vulkan1.2_dxlayout_Zpr_preservebind_ps65", defines, debug, frag))
			{
				SS_CORE_ERROR("VulkanShader: failed to compile graphics shader {}", m_Filepath);
				return; // leaves m_Ready false: the pipeline never builds, rather than building from garbage
			}
			{
				std::lock_guard lock(m_Mutex);
				m_Compiled.Vert = std::move(vert);
				m_Compiled.Frag = std::move(frag);
				++m_Compiled.Version;
			}
			m_Ready.store(true, std::memory_order_release);
			return;
		}

		// Single-path: a compute shader. Since graphics shaders are now two-path (separate vert+frag
		// files), a single-path shader is unambiguously compute — compile the file directly as cs_6_5.
		// SM 6.5 (was 6.0) is required for inline ray query (RayQuery/TraceRayInline, #118) and is a strict
		// superset, so every existing compute shader still compiles unchanged.
		const fs::path p(m_VertPath);
		if (p.extension() != ".hlsl")
		{
			SS_CORE_ERROR("VulkanShader: runtime compile expects .hlsl, got {}", m_VertPath);
			return;
		}

		std::string comp;
		if (!CompileStageFileToSpirvCache(m_VertPath, L"cs_6_5", "v5_vulkan1.2_dxlayout_Zpr_preservebind_cs65", defines, debug, comp))
		{
			SS_CORE_ERROR("VulkanShader: failed to compile compute shader {}", m_VertPath);
			return;
		}
		{
			std::lock_guard lock(m_Mutex);
			m_Compiled.Comp = std::move(comp);
			++m_Compiled.Version;
		}
		m_Ready.store(true, std::memory_order_release);
	}
}
