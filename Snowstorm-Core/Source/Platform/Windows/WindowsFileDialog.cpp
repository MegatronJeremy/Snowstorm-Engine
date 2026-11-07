#include "Snowstorm/Utility/FileDialog.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

#include <system_error>

namespace Snowstorm::FileDialog
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		// SetFileTypes/SHCreateItemFromParsingName need wide strings; FileDialogFilter/paths are UTF-8 std::string.
		std::wstring ToWide(const std::string& s)
		{
			if (s.empty())
				return {};

			const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
			std::wstring result(size, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), result.data(), size);
			return result;
		}

		// SHCreateItemFromParsingName requires an ABSOLUTE, native (backslash) path; a relative one (e.g. the
		// active project's "Projects/Sandbox/assets/scenes") silently fails to parse, so the dialog ignores the
		// default folder. Make it absolute against the CWD and native-separator'd. Returns empty on error so the
		// caller skips SetDefaultFolder rather than passing garbage.
		std::filesystem::path AbsoluteNative(const std::filesystem::path& p)
		{
			if (p.empty())
				return {};
			std::error_code ec;
			std::filesystem::path abs = std::filesystem::absolute(p, ec);
			if (ec)
				return {};
			return abs.make_preferred();
		}

		// COM for the dialog's lifetime. RPC_E_CHANGED_MODE means COM is already initialized on this thread with
		// a different apartment model: the dialog still works, but what this did not initialize it must not
		// uninitialize.
		class ComScope
		{
		public:
			ComScope()
			    : m_Result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
			{
			}
			~ComScope()
			{
				if (m_Result == S_OK || m_Result == S_FALSE)
				{
					CoUninitialize();
				}
			}
			ComScope(const ComScope&) = delete;
			ComScope& operator=(const ComScope&) = delete;

			[[nodiscard]] bool Usable() const { return SUCCEEDED(m_Result) || m_Result == RPC_E_CHANGED_MODE; }

		private:
			HRESULT m_Result;
		};

		bool AddOptions(IFileDialog& dialog, const FILEOPENDIALOGOPTIONS extra)
		{
			FILEOPENDIALOGOPTIONS options = 0;
			return SUCCEEDED(dialog.GetOptions(&options)) && SUCCEEDED(dialog.SetOptions(options | extra));
		}

		// Returns whether a non-empty filter list was set.
		bool SetFileTypes(IFileDialog& dialog, const std::vector<FileDialogFilter>& filters)
		{
			// COMDLG_FILTERSPEC only points into these strings, so they must outlive the SetFileTypes call.
			std::vector<std::wstring> names;
			std::vector<std::wstring> patterns;
			std::vector<COMDLG_FILTERSPEC> specs;
			names.reserve(filters.size());
			patterns.reserve(filters.size());
			specs.reserve(filters.size());
			for (const FileDialogFilter& filter : filters)
			{
				names.push_back(ToWide(filter.Name));
				patterns.push_back(L"*." + ToWide(filter.Extension));
				specs.push_back({names.back().c_str(), patterns.back().c_str()});
			}
			return !specs.empty() && SUCCEEDED(dialog.SetFileTypes(static_cast<UINT>(specs.size()), specs.data()));
		}

		// SetDefaultFolder applies only when the user has no recent folder for this dialog, SetFolder always.
		void SetStartFolder(IFileDialog& dialog, const std::filesystem::path& folder, const bool asDefault)
		{
			ComPtr<IShellItem> item;
			if (folder.empty() || FAILED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
			{
				return;
			}
			if (asDefault)
			{
				dialog.SetDefaultFolder(item.Get());
			}
			else
			{
				dialog.SetFolder(item.Get());
			}
		}

		// Empty when the user cancelled or the pick is not a filesystem path.
		std::filesystem::path ShowAndGetPath(IFileDialog& dialog)
		{
			ComPtr<IShellItem> item;
			PWSTR rawPath = nullptr;
			if (FAILED(dialog.Show(nullptr)) || FAILED(dialog.GetResult(&item)) ||
			    FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr)
			{
				return {};
			}
			std::filesystem::path selected(rawPath);
			CoTaskMemFree(rawPath);
			return selected;
		}
	}

	std::filesystem::path OpenFile(const std::vector<FileDialogFilter>& filters, const std::filesystem::path& defaultPath)
	{
		const ComScope com;
		ComPtr<IFileOpenDialog> dialog;
		if (!com.Usable() || FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog))))
		{
			return {};
		}
		SetFileTypes(*dialog.Get(), filters);
		SetStartFolder(*dialog.Get(), AbsoluteNative(defaultPath), true);
		return ShowAndGetPath(*dialog.Get());
	}

	std::filesystem::path SaveFile(const std::vector<FileDialogFilter>& filters, const std::filesystem::path& defaultPath)
	{
		const ComScope com;
		ComPtr<IFileSaveDialog> dialog;
		if (!com.Usable() || FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog))))
		{
			return {};
		}

		// FOS_FORCEFILESYSTEM: only real filesystem items, no virtual shell locations.
		// FOS_PATHMUSTEXIST: the CONTAINING folder must exist (the file itself is new).
		// FOS_OVERWRITEPROMPT: ask before silently replacing an existing file.
		AddOptions(*dialog.Get(), FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);

		if (SetFileTypes(*dialog.Get(), filters))
		{
			// The first filter is the initially selected type (1-based index), and its extension is the one
			// appended to a name typed without one.
			dialog->SetFileTypeIndex(1);
			const std::wstring defaultExtension = ToWide(filters.front().Extension);
			dialog->SetDefaultExtension(defaultExtension.c_str());
		}

		// defaultPath is either a folder to start in or a suggested save path (folder + filename). SetFolder
		// accepts only an existing folder item, so a suggested path is split into the two halves.
		if (const std::filesystem::path absDefault = AbsoluteNative(defaultPath); !absDefault.empty())
		{
			std::error_code ec;
			const bool isFolder = std::filesystem::is_directory(absDefault, ec);
			SetStartFolder(*dialog.Get(), isFolder ? absDefault : absDefault.parent_path(), false);
			if (!isFolder && !absDefault.filename().empty())
			{
				dialog->SetFileName(absDefault.filename().c_str());
			}
		}

		dialog->SetTitle(L"Save File");
		return ShowAndGetPath(*dialog.Get());
	}

	std::filesystem::path OpenFolder(const std::filesystem::path& defaultPath)
	{
		const ComScope com;
		ComPtr<IFileOpenDialog> dialog;
		if (!com.Usable() || FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog))))
		{
			return {};
		}
		if (AddOptions(*dialog.Get(), FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))
		{
			SetStartFolder(*dialog.Get(), AbsoluteNative(defaultPath), false);
		}
		dialog->SetTitle(L"Select Folder");
		return ShowAndGetPath(*dialog.Get());
	}
}
