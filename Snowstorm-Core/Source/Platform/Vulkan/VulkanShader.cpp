#include "VulkanShader.hpp"

#include "Snowstorm/Core/Log.hpp"

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
		enum class Section : uint8_t { None, Vertex, Fragment };

		struct SplitResult
		{
			std::string Vertex;
			std::string Fragment;
		};

		std::string ReadTextFileOrEmpty(const fs::path& p)
		{
			std::ifstream in(p, std::ios::in);
			if (!in.is_open())
				return {};

			std::stringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}

		bool WriteTextFile(const fs::path& p, const std::string& text)
		{
			std::ofstream out(p, std::ios::out | std::ios::trunc);
			if (!out.is_open())
				return false;
			out.write(text.data(), static_cast<std::streamsize>(text.size()));
			return true;
		}

		SplitResult SplitByTypeMarkers(const std::string& src)
		{
			SplitResult r{};
			std::istringstream iss(src);
			std::string line;
			Section cur = Section::None;

			while (std::getline(iss, line))
			{
				if (line.starts_with("#type"))
				{
					if (line.find("vertex") != std::string::npos) cur = Section::Vertex;
					else if (line.find("fragment") != std::string::npos) cur = Section::Fragment;
					else cur = Section::None;
					continue;
				}

				switch (cur)
				{
				case Section::Vertex:   r.Vertex.append(line).push_back('\n'); break;
				case Section::Fragment: r.Fragment.append(line).push_back('\n'); break;
				default: break;
				}
			}

			return r;
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
			return{buf};
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

			std::wstring cmd;
			cmd += QuoteArg(exe);
			cmd += L" -spirv -E main -T ";
			cmd += profile;
			cmd += L" -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Zpr";
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
				SS_CORE_ERROR("DXC failed (exit code {}). Command: {}", static_cast<uint32_t>(exitCode),
				              std::string(cmd.begin(), cmd.end()));
				return false;
			}

			if (!fs::exists(outputSpv))
			{
				SS_CORE_ERROR("DXC reported success but output file does not exist: {}", outputSpv.string());
				return false;
			}

			return true;
		}

		bool CompileHlslWithTypesToSpirvCache(const std::string& sourcePath,
		                                      std::string& outVertSpv,
		                                      std::string& outFragSpv)
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

			const SplitResult split = SplitByTypeMarkers(fullText);
			if (split.Vertex.empty() || split.Fragment.empty())
			{
				SS_CORE_ERROR("Shader file must contain both '#type vertex' and '#type fragment': {}", sourcePath);
				return false;
			}

			// Cache key: hash(source text + fixed flags). Later include: include file contents, defines, etc.
			constexpr const char* kFlagsKey = "spirv_vulkan1.2_fvk-use-dx-layout_Zpr_vs6_ps6_main";
			uint64_t h = 0;
			h ^= Hash64(fullText.data(), fullText.size());
			h ^= Hash64(kFlagsKey, std::strlen(kFlagsKey));

			const std::string key = ToHex64(h);
			const std::string stem = srcPath.stem().string(); // DefaultLit

			// Temp input HLSL files (so DXC can report filenames and handle includes later)
			const fs::path tmpVert = cacheDir / (stem + "_" + key + ".vert.hlsl");
			const fs::path tmpFrag = cacheDir / (stem + "_" + key + ".frag.hlsl");

			// Compiled outputs
			const fs::path outVert = cacheDir / (stem + "_" + key + ".vert.spv");
			const fs::path outFrag = cacheDir / (stem + "_" + key + ".frag.spv");

			// If outputs already exist, reuse (fast hot reload when file timestamps change but content didn't)
			if (fs::exists(outVert) && fs::exists(outFrag))
			{
				outVertSpv = outVert.string();
				outFragSpv = outFrag.string();
				return true;
			}

			if (!WriteTextFile(tmpVert, split.Vertex))
			{
				SS_CORE_ERROR("Failed to write temp vertex HLSL: {}", tmpVert.string());
				return false;
			}

			if (!WriteTextFile(tmpFrag, split.Fragment))
			{
				SS_CORE_ERROR("Failed to write temp fragment HLSL: {}", tmpFrag.string());
				return false;
			}

			if (!CompileStageWithDxc(dxcExe, tmpVert, outVert, L"vs_6_0"))
				return false;

			if (!CompileStageWithDxc(dxcExe, tmpFrag, outFrag, L"ps_6_0"))
				return false;

			outVertSpv = outVert.string();
			outFragSpv = outFrag.string();
			return true;
		}
	}

	VulkanShader::VulkanShader(std::string filepath)
		: m_Filepath(std::move(filepath))
	{
		Compile();
	}

	std::string VulkanShader::GetCompiledPath(const ShaderStageKind stage) const
	{
		switch (stage)
		{
		case ShaderStageKind::Vertex: return m_CompiledVertSpv;
		case ShaderStageKind::Fragment: return m_CompiledFragSpv;
		case ShaderStageKind::Compute: return {};
		}
		return {};
	}

	void VulkanShader::Compile()
	{
		const fs::path p(m_Filepath);

		if (p.extension() != ".hlsl")
		{
			SS_CORE_ERROR("VulkanShader: runtime compile expects .hlsl, got {}", m_Filepath);
			return;
		}

		std::string vert, frag;
		if (!CompileHlslWithTypesToSpirvCache(m_Filepath, vert, frag))
		{
			SS_CORE_ERROR("VulkanShader: failed to compile {}", m_Filepath);
			return;
		}

		m_CompiledVertSpv = std::move(vert);
		m_CompiledFragSpv = std::move(frag);

		++m_Version;
	}
}
