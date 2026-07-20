#include "Snowstorm/Utility/FileDialog.hpp"

#include <shobjidl.h>

#include <system_error>

namespace Snowstorm::FileDialog
{
	namespace
	{
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
	}

	std::filesystem::path OpenFile(const std::vector<FileDialogFilter>& filters, const std::filesystem::path& defaultPath)
	{
		std::filesystem::path selectedPath;

		const HRESULT initResult = CoInitializeEx(
		    nullptr,
		    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		// RPC_E_CHANGED_MODE = COM is already initialized on this thread with a different apartment
		// model; the dialog still works, we just must not CoUninitialize what we didn't init (same
		// handling as SaveFile/OpenFolder).
		if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE)
			return {};

		IFileOpenDialog* fileDialog = nullptr;

		HRESULT hr = CoCreateInstance(
		    CLSID_FileOpenDialog,
		    nullptr,
		    CLSCTX_ALL,
		    IID_PPV_ARGS(&fileDialog));

		if (SUCCEEDED(hr))
		{
			// Keep the wide strings alive for the SetFileTypes call — COMDLG_FILTERSPEC only stores
			// pointers into them, it doesn't copy.
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
			}
			for (size_t i = 0; i < filters.size(); ++i)
				specs.push_back({names[i].c_str(), patterns[i].c_str()});

			if (!specs.empty())
				fileDialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());

			if (const std::filesystem::path defaultDir = AbsoluteNative(defaultPath); !defaultDir.empty())
			{
				IShellItem* defaultFolder = nullptr;

				hr = SHCreateItemFromParsingName(
				    defaultDir.c_str(),
				    nullptr,
				    IID_PPV_ARGS(&defaultFolder));

				if (SUCCEEDED(hr))
				{
					fileDialog->SetDefaultFolder(defaultFolder);

					defaultFolder->Release();
				}
			}

			hr = fileDialog->Show(nullptr);

			if (SUCCEEDED(hr))
			{
				IShellItem* selectedItem = nullptr;

				hr = fileDialog->GetResult(&selectedItem);

				if (SUCCEEDED(hr))
				{
					PWSTR rawPath = nullptr;

					hr = selectedItem->GetDisplayName(
					    SIGDN_FILESYSPATH,
					    &rawPath);

					if (SUCCEEDED(hr) && rawPath != nullptr)
					{
						selectedPath = std::filesystem::path(rawPath);

						CoTaskMemFree(rawPath);
					}

					selectedItem->Release();
				}
			}

			fileDialog->Release();
		}

		if (initResult == S_OK || initResult == S_FALSE)
		{
			CoUninitialize();
		}

		return selectedPath;
	}

	std::filesystem::path SaveFile(
	    const std::vector<FileDialogFilter>& filters,
	    const std::filesystem::path& defaultPath)
	{
		std::filesystem::path selectedPath;

		const HRESULT initializeResult = CoInitializeEx(
		    nullptr,
		    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		// RPC_E_CHANGED_MODE tolerated, same as OpenFile/OpenFolder (see OpenFile).
		if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE)
			return {};

		IFileSaveDialog* dialog = nullptr;

		HRESULT hr = CoCreateInstance(
		    CLSID_FileSaveDialog,
		    nullptr,
		    CLSCTX_ALL,
		    IID_PPV_ARGS(&dialog));

		if (SUCCEEDED(hr))
		{
			FILEOPENDIALOGOPTIONS options = 0;

			hr = dialog->GetOptions(&options);

			if (SUCCEEDED(hr))
			{
				// FOS_FORCEFILESYSTEM: only real filesystem items, no virtual shell locations.
				// FOS_PATHMUSTEXIST: the CONTAINING folder must exist (the file itself is new).
				// FOS_OVERWRITEPROMPT: ask before silently replacing an existing file.
				hr = dialog->SetOptions(
				    options |
				    FOS_FORCEFILESYSTEM |
				    FOS_PATHMUSTEXIST |
				    FOS_OVERWRITEPROMPT);
			}

			// Keep the wide strings alive for the SetFileTypes call below — COMDLG_FILTERSPEC only
			// stores pointers into them, it doesn't copy (same constraint as in OpenFile).
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
			}

			for (std::size_t i = 0; i < filters.size(); ++i)
			{
				specs.push_back({names[i].c_str(),
				                 patterns[i].c_str()});
			}

			if (!specs.empty())
			{
				hr = dialog->SetFileTypes(
				    static_cast<UINT>(specs.size()),
				    specs.data());

				if (SUCCEEDED(hr))
				{
					// 1-based index into the specs array set above — pick the first filter as
					// the dialog's initially-selected file type.
					dialog->SetFileTypeIndex(1);

					// Extension the dialog appends automatically if the user types a name with
					// no extension — take it from the first (default-selected) filter.
					const std::wstring defaultExtension =
					    ToWide(filters.front().Extension);

					dialog->SetDefaultExtension(
					    defaultExtension.c_str());
				}
			}

			// defaultPath can be either a folder to start in, or a full suggested save path
			// (folder + filename) — detect which and set the dialog up accordingly. SHCreateItemFromParsingName
			// needs an ABSOLUTE native path, so normalize first (a relative project path would silently fail).
			if (const std::filesystem::path absDefault = AbsoluteNative(defaultPath); !absDefault.empty())
			{
				std::error_code errorCode;

				if (std::filesystem::is_directory(
				        absDefault,
				        errorCode))
				{
					IShellItem* defaultFolder = nullptr;

					hr = SHCreateItemFromParsingName(
					    absDefault.c_str(),
					    nullptr,
					    IID_PPV_ARGS(&defaultFolder));

					if (SUCCEEDED(hr))
					{
						dialog->SetFolder(defaultFolder);
						defaultFolder->Release();
					}
				}
				else
				{
					// Not a directory: treat absDefault as folder + suggested filename, and set
					// each half separately (SetFolder only accepts an existing folder item).
					const std::filesystem::path parentPath =
					    absDefault.parent_path();

					if (!parentPath.empty())
					{
						IShellItem* defaultFolder = nullptr;

						hr = SHCreateItemFromParsingName(
						    parentPath.c_str(),
						    nullptr,
						    IID_PPV_ARGS(&defaultFolder));

						if (SUCCEEDED(hr))
						{
							dialog->SetFolder(defaultFolder);
							defaultFolder->Release();
						}
					}

					if (!absDefault.filename().empty())
					{
						dialog->SetFileName(
						    absDefault.filename().c_str());
					}
				}
			}

			dialog->SetTitle(L"Save File");

			hr = dialog->Show(nullptr);

			if (SUCCEEDED(hr))
			{
				IShellItem* selectedItem = nullptr;

				hr = dialog->GetResult(&selectedItem);

				if (SUCCEEDED(hr))
				{
					PWSTR rawPath = nullptr;

					hr = selectedItem->GetDisplayName(
					    SIGDN_FILESYSPATH,
					    &rawPath);

					if (SUCCEEDED(hr) && rawPath != nullptr)
					{
						selectedPath =
						    std::filesystem::path(rawPath);

						CoTaskMemFree(rawPath);
					}

					selectedItem->Release();
				}
			}

			dialog->Release();
		}

		// Only uninitialize what we initialized: on RPC_E_CHANGED_MODE the COM apartment belongs to
		// someone else on this thread.
		if (initializeResult == S_OK ||
		    initializeResult == S_FALSE)
		{
			CoUninitialize();
		}

		return selectedPath;
	}

	std::filesystem::path OpenFolder(
	    const std::filesystem::path& defaultPath)
	{
		std::filesystem::path selectedPath;

		const HRESULT initializeResult = CoInitializeEx(
		    nullptr,
		    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		// RPC_E_CHANGED_MODE tolerated, same as OpenFile/SaveFile (see OpenFile).
		if (FAILED(initializeResult) &&
		    initializeResult != RPC_E_CHANGED_MODE)
		{
			return {};
		}

		IFileOpenDialog* dialog = nullptr;

		HRESULT hr = CoCreateInstance(
		    CLSID_FileOpenDialog,
		    nullptr,
		    CLSCTX_ALL,
		    IID_PPV_ARGS(&dialog));

		if (SUCCEEDED(hr))
		{
			FILEOPENDIALOGOPTIONS options = 0;

			hr = dialog->GetOptions(&options);

			if (SUCCEEDED(hr))
			{
				hr = dialog->SetOptions(
				    options |
				    FOS_PICKFOLDERS |
				    FOS_FORCEFILESYSTEM |
				    FOS_PATHMUSTEXIST);
			}

			if (const std::filesystem::path defaultDir = AbsoluteNative(defaultPath); SUCCEEDED(hr) && !defaultDir.empty())
			{
				IShellItem* defaultFolder = nullptr;

				hr = SHCreateItemFromParsingName(
				    defaultDir.c_str(),
				    nullptr,
				    IID_PPV_ARGS(&defaultFolder));

				if (SUCCEEDED(hr))
				{
					dialog->SetFolder(defaultFolder);

					defaultFolder->Release();
				}
			}

			dialog->SetTitle(L"Select Folder");

			hr = dialog->Show(nullptr);

			if (SUCCEEDED(hr))
			{
				IShellItem* selectedItem = nullptr;

				hr = dialog->GetResult(&selectedItem);

				if (SUCCEEDED(hr))
				{
					PWSTR rawPath = nullptr;

					hr = selectedItem->GetDisplayName(
					    SIGDN_FILESYSPATH,
					    &rawPath);

					if (SUCCEEDED(hr) && rawPath != nullptr)
					{
						selectedPath = std::filesystem::path(rawPath);
						CoTaskMemFree(rawPath);
					}

					selectedItem->Release();
				}
			}

			dialog->Release();
		}

		if (initializeResult == S_OK ||
		    initializeResult == S_FALSE)
		{
			CoUninitialize();
		}

		return selectedPath;
	}
}
