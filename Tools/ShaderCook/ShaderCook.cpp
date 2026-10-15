// Offline shader cook: compiles Engine/Shaders/*.hlsl to SPIR-V with the same dxc command line the
// runtime uses (Snowstorm/Render/ShaderCompile.hpp), for the RGA occupancy gate on a machine with no GPU.
//
//   ShaderCook --dxc <dxc.exe> --shaders <dir> --out <dir> --permutations <file> [--variants rt,base] [--dry-run]
//
// Output names: <stem>[__<feature>]_<variant>.spv, the form rga-occupancy.py parses.

#include "Snowstorm/Render/ShaderCompile.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace Snowstorm;

namespace
{
	struct Feature
	{
		std::string tag;
		std::vector<std::string> defines;
	};

	std::string Trim(const std::string& s)
	{
		const auto b = s.find_first_not_of(" \t\r");
		if (b == std::string::npos)
			return {};
		return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
	}

	std::map<std::string, std::vector<Feature>> LoadPermutations(const fs::path& file)
	{
		std::map<std::string, std::vector<Feature>> out;
		std::ifstream in(file);
		std::string line;
		while (std::getline(in, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#')
				continue;
			std::vector<std::string> cols;
			std::stringstream ss(line);
			std::string col;
			while (std::getline(ss, col, '|'))
				cols.push_back(Trim(col));
			if (cols.size() < 2 || cols[0].empty() || cols[1].empty())
				continue;
			Feature f{cols[1], {}};
			if (cols.size() > 2)
			{
				std::stringstream ds(cols[2]);
				std::string d;
				while (ds >> d)
					f.defines.push_back(d);
			}
			out[cols[0]].push_back(std::move(f));
		}
		return out;
	}

	// 'Reflection.comp.hlsl' -> cs_6_5; a stage-less name is compute, as in the engine.
	const wchar_t* ProfileFor(const fs::path& src)
	{
		const std::string stem = src.stem().string();
		const auto dot = stem.rfind('.');
		const std::string stage = dot == std::string::npos ? "" : stem.substr(dot + 1);
		if (stage == "vert")
			return L"vs_6_5";
		if (stage == "frag")
			return L"ps_6_5";
		return L"cs_6_5";
	}

	std::vector<std::string> Split(const std::string& s)
	{
		std::vector<std::string> r;
		std::stringstream ss(s);
		std::string t;
		while (std::getline(ss, t, ','))
			if (!t.empty())
				r.push_back(t);
		return r;
	}

	std::string Narrow(const std::wstring& w)
	{
		return {w.begin(), w.end()};
	}

#ifdef _WIN32
	int RunDxc(std::wstring cmd, std::string& output)
	{
		SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
		HANDLE rd = nullptr, wr = nullptr;
		if (!CreatePipe(&rd, &wr, &sa, 0))
			return -1;
		SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = wr;
		si.hStdError = wr;
		PROCESS_INFORMATION pi{};
		const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		CloseHandle(wr);
		if (!ok)
		{
			CloseHandle(rd);
			return -1;
		}
		char buf[4096];
		DWORD n = 0;
		while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
			output.append(buf, n);
		WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(rd);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return static_cast<int>(code);
	}
#endif
}

int main(int argc, char** argv)
{
	fs::path dxc = "Tools/dxc/dxc.exe", shaders = "Engine/Shaders", out = "Engine/cache/shaders-cook",
	         perms = "Scripts/shader-permutations.txt";
	std::string variantArg = "rt";
	bool dryRun = false;

	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		const bool hasValue = i + 1 < argc;
		if (a == "--dry-run")
			dryRun = true;
		else if (a == "--dxc" && hasValue)
			dxc = argv[++i];
		else if (a == "--shaders" && hasValue)
			shaders = argv[++i];
		else if (a == "--out" && hasValue)
			out = argv[++i];
		else if (a == "--permutations" && hasValue)
			perms = argv[++i];
		else if (a == "--variants" && hasValue)
			variantArg = argv[++i];
		else
		{
			std::cerr << "FAIL: unknown or incomplete argument '" << a << "'\n";
			return 1;
		}
	}

	// Device-capability axes the runtime injects. rt is the heaviest register case, base the ForceNonRT path.
	const std::map<std::string, std::vector<std::string>> capability{
	    {"base", {}}, {"rt", {"SS_RAYTRACING=1", "SS_FP16=1"}}};
	std::vector<std::string> variants;
	for (const std::string& v : Split(variantArg))
		if (capability.count(v))
			variants.push_back(v);
	if (variants.empty())
	{
		std::cerr << "FAIL: no valid variants in '" << variantArg << "' (known: base, rt)\n";
		return 1;
	}
	if (!fs::is_directory(shaders))
	{
		std::cerr << "FAIL: shader dir not found at " << shaders << "\n";
		return 1;
	}
	if (!dryRun && !fs::is_regular_file(dxc))
	{
		std::cerr << "FAIL: dxc not found at " << dxc << "\n";
		return 1;
	}
#ifndef _WIN32
	if (!dryRun)
	{
		std::cerr << "FAIL: dxc.exe runs on Windows only; use --dry-run to print the command lines\n";
		return 1;
	}
#endif

	const auto features = LoadPermutations(perms);

	std::vector<fs::path> sources;
	for (const auto& e : fs::directory_iterator(shaders))
		if (e.is_regular_file() && e.path().extension() == ".hlsl")
			sources.push_back(e.path());
	std::sort(sources.begin(), sources.end());

	if (!dryRun)
	{
		fs::create_directories(out);
		// Anything left from an earlier run would be analysed as a live module by rga-occupancy.py.
		size_t stale = 0;
		for (const auto& e : fs::directory_iterator(out))
			if (e.path().extension() == ".spv" && fs::remove(e.path()))
				++stale;
		if (stale)
			std::cout << "cleared " << stale << " stale .spv\n";
	}

	size_t ok = 0, fail = 0;
	for (const fs::path& src : sources)
	{
		const std::string stem = src.stem().string();
		std::vector<Feature> feats;
		if (const auto it = features.find(stem); it != features.end())
			feats = it->second;
		else
			feats.push_back({});

		for (const Feature& f : feats)
		{
			for (const std::string& v : variants)
			{
				DxcRequest r;
				r.dxcExe = dxc.wstring();
				r.profile = ProfileFor(src);
				r.input = src.wstring();
				r.includeDir = shaders.wstring();
				r.defines = f.defines;
				for (const std::string& d : capability.at(v))
					r.defines.push_back(d);
				const std::string name = stem + (f.tag.empty() ? "" : "__" + f.tag) + "_" + v + ".spv";
				const fs::path spv = out / name;
				r.output = spv.wstring();
				const std::wstring cmd = BuildDxcCommandLine(r);

				if (dryRun)
				{
					std::cout << name << ": " << Narrow(cmd) << "\n";
					++ok;
					continue;
				}
#ifdef _WIN32
				std::string log;
				const int code = RunDxc(cmd, log);
				if (code == 0 && fs::exists(spv))
					++ok;
				else
				{
					++fail;
					std::cout << "  FAIL " << name << ": " << log.substr(0, 400) << "\n";
				}
#endif
			}
		}
	}

	std::cout << (dryRun ? "planned " : "cooked ") << ok << " ok, " << fail << " failed -> " << out.string() << "\n";
	return fail ? 1 : 0;
}
