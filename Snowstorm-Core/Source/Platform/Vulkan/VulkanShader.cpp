#include "VulkanShader.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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

		// Simple 64-bit FNV-1a hash (good enough for cache keys)
		uint64_t Hash64(const void* data, const size_t size)
		{
			const uint8_t* p = static_cast<const uint8_t*>(data);
			uint64_t h = 1469598103934665603ull;
			for (size_t i = 0; i < size; ++i)
			{
				h ^= p[i];
				h *= 1099511628211ull;
			}
			return h;
		}

		std::string ToHex64(const uint64_t v)
		{
			char buf[17]{};
			std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
			return {buf};
		}

		fs::path GetRepoRoot()
		{
			// Minimal approach: assume working directory is repo root when running from Rider/VS.
			// If that’s not true for you, we can thread the project root through Application config.
			return fs::current_path();
		}

		fs::path GetDxcExePath()
		{
			return GetRepoRoot() / "tools" / "dxc" / "dxc.exe";
		}

		// Fold every shared .hlsli header into the cache key. The cache key otherwise hashes only the
		// top-level .hlsl source, so editing an included header (e.g. Engine.hlsli, which defines the
		// FrameCB/MaterialCB layouts shared by all shaders) would NOT invalidate the cached SPIR-V —
		// the stale shader keeps the old cbuffer layout while C++ uploads the new one, silently
		// corrupting every cbuffer read (symptom: black/unlit geometry). A full #include dependency
		// parse is overkill for a single flat include dir; hashing all headers in it is a deliberately
		// smaller version that catches any header edit. Headers are sorted so the hash is order-stable.
		uint64_t HashIncludeHeaders()
		{
			const fs::path includeDir = GetRepoRoot() / "assets" / "shaders" / "include";
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
				h ^= Hash64(text.data(), text.size());
			}
			return h;
		}

		fs::path GetShaderCacheDir()
		{
			return GetRepoRoot() / "assets" / "cache" / "shaders";
		}

		std::wstring QuoteArg(const std::wstring& s)
		{
			// Always quote; simplest.
			return L"\"" + s + L"\"";
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
		                         const wchar_t* profile)
		{
			const std::wstring exe = dxcExe.wstring();
			const std::wstring in = inputHlsl.wstring();
			const std::wstring out = outputSpv.wstring();

			// Include base is the shaders root; shaders reference headers explicitly as "Include/Foo.hlsli"
			// (relative-to-file, so the IDE resolves them too — a bare "Foo.hlsli" needs the -I flag the IDE
			// doesn't see). DXC also resolves quote-includes relative to the including file, so this -I is a
			// belt-and-suspenders fallback.
			const fs::path includePath = GetRepoRoot() / "assets" / "shaders";
			const std::wstring includePathW = includePath.wstring();

			std::wstring cmd;
			cmd += QuoteArg(exe);
			cmd += L" -spirv -E main -T ";
			cmd += profile;
			cmd += L" -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Zpr";

			// --- DEBUG FLAGS ---
			// -Zi: Include debug information
			// -Od: Disable optimizations (makes debugging MUCH easier)
			// -fspv-debug=vulkan-with-source: Specific SPIR-V debug info
			cmd += L" -Zi -Od -fspv-debug=vulkan-with-source";

			cmd += L" -I ";
			cmd += QuoteArg(includePathW);
			cmd += L" -Fo ";
			cmd += QuoteArg(out);
			cmd += L" ";
			cmd += QuoteArg(in);

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
		                                  const char* flagsTag, std::string& outSpv)
		{
			const fs::path srcPath(sourcePath);
			const fs::path dxcExe = GetDxcExePath();
			const fs::path cacheDir = GetShaderCacheDir();

			if (!fs::exists(dxcExe))
			{
				SS_CORE_ERROR("DXC not found at {}", dxcExe.string());
				return false;
			}
			fs::create_directories(cacheDir);

			const std::string fullText = ReadTextFileOrEmpty(srcPath);
			if (fullText.empty())
			{
				SS_CORE_ERROR("Shader source is empty or missing: {}", sourcePath);
				return false;
			}

			// Cache key: hash(source + all .hlsli headers + a per-profile flags tag). Same recipe as the
			// legacy path; the flags tag keeps vs/ps/cs caches distinct.
			uint64_t h = 0;
			h ^= Hash64(fullText.data(), fullText.size());
			h ^= HashIncludeHeaders();
			h ^= Hash64(flagsTag, std::strlen(flagsTag));

			const std::string key = ToHex64(h);
			const std::string stem = srcPath.stem().string(); // e.g. "Mesh.vert" -> "Mesh.vert"
			const fs::path outSpvPath = cacheDir / (stem + "_" + key + ".spv");

			if (fs::exists(outSpvPath))
			{
				outSpv = outSpvPath.string();
				return true;
			}

			// Compile the real source file directly (DXC resolves #include via -I; the file is unmodified).
			if (!CompileStageWithDxc(dxcExe, srcPath, outSpvPath, profile))
			{
				return false;
			}

			outSpv = outSpvPath.string();
			return true;
		}

	}

	VulkanShader::VulkanShader(std::string filepath)
	    : m_Filepath(std::move(filepath))
	{
		m_VertPath = m_Filepath; // single-path == a compute shader
		Compile();
	}

	VulkanShader::VulkanShader(std::string vertPath, std::string fragPath)
	    : m_Filepath(vertPath + "|" + fragPath) // composite label for logging / library keying
	      ,
	      m_VertPath(std::move(vertPath)), m_FragPath(std::move(fragPath))
	{
		Compile();
	}

	std::string VulkanShader::GetCompiledPath(const ShaderStageKind stage) const
	{
		switch (stage)
		{
		case ShaderStageKind::Vertex:
			return m_CompiledVertSpv;
		case ShaderStageKind::Fragment:
			return m_CompiledFragSpv;
		case ShaderStageKind::Compute:
			return m_CompiledCompSpv;
		}
		return {};
	}

	void VulkanShader::Compile()
	{
		// Two-path graphics: separate vertex + fragment files, each a plain single-`main` HLSL file
		// compiled directly (no #type split). The preferred form.
		if (!m_FragPath.empty())
		{
			std::string vert, frag;
			if (!CompileStageFileToSpirvCache(m_VertPath, L"vs_6_0", "v2_vulkan1.2_dxlayout_Zpr_vs6", vert) ||
			    !CompileStageFileToSpirvCache(m_FragPath, L"ps_6_0", "v2_vulkan1.2_dxlayout_Zpr_ps6", frag))
			{
				SS_CORE_ERROR("VulkanShader: failed to compile graphics shader {}", m_Filepath);
				return;
			}
			m_CompiledVertSpv = std::move(vert);
			m_CompiledFragSpv = std::move(frag);
			++m_Version;
			return;
		}

		// Single-path: a compute shader. Since graphics shaders are now two-path (separate vert+frag
		// files), a single-path shader is unambiguously compute — compile the file directly as cs_6_0.
		const fs::path p(m_VertPath);
		if (p.extension() != ".hlsl")
		{
			SS_CORE_ERROR("VulkanShader: runtime compile expects .hlsl, got {}", m_VertPath);
			return;
		}

		std::string comp;
		if (!CompileStageFileToSpirvCache(m_VertPath, L"cs_6_0", "v2_vulkan1.2_dxlayout_Zpr_cs6", comp))
		{
			SS_CORE_ERROR("VulkanShader: failed to compile compute shader {}", m_VertPath);
			return;
		}
		m_CompiledCompSpv = std::move(comp);
		++m_Version;
	}
}
