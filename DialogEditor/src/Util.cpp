#include "Util.h"
#include <fstream>

namespace de
{
    std::wstring Widen(std::string_view utf8)
    {
        if (utf8.empty())
            return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), count);
        return out;
    }

    std::string Narrow(std::wstring_view wide)
    {
        if (wide.empty())
            return {};
        const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), count, nullptr, nullptr);
        return out;
    }

    std::wstring ToEditText(std::string_view utf8)
    {
        std::wstring in = Widen(utf8);
        std::wstring out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == L'\r')
                continue;
            if (in[i] == L'\n')
                out += L"\r\n";
            else
                out += in[i];
        }
        return out;
    }

    std::string FromEditText(std::wstring_view text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (wchar_t c : text)
        {
            if (c != L'\r')
                out += c;
        }
        return Narrow(out);
    }

    std::optional<std::string> ReadFileText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return text;
    }

    std::wstring LastErrorMessage(DWORD error)
    {
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message = buffer ? buffer : std::format(L"Error {}", error);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r'))
            message.pop_back();
        return message;
    }

    bool WriteFileText(const fs::path& path, std::string_view text, std::wstring& error)
    {
        const fs::path temp = path.wstring() + L".saving";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                error = L"Could not create " + temp.wstring();
                return false;
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!file)
            {
                error = L"Could not write " + temp.wstring();
                return false;
            }
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = LastErrorMessage();
            DeleteFileW(temp.c_str());
            return false;
        }
        return true;
    }

    std::optional<fs::path> FindAssetsFolder(std::wstring& error)
    {
        std::error_code ec;

        // 1. Command line: /asset <full path>   (also accepts -asset and /assets)
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::wstring arg = argv[i];
                for (auto& c : arg)
                    c = static_cast<wchar_t>(towlower(c));
                if ((arg == L"/asset" || arg == L"-asset" || arg == L"/assets" || arg == L"-assets") && i + 1 < argc)
                {
                    fs::path folder = argv[i + 1];
                    LocalFree(argv);
                    if (fs::is_directory(folder, ec))
                        return fs::absolute(folder, ec);
                    error = L"The folder given with /asset does not exist:\n" + folder.wstring();
                    return std::nullopt;
                }
            }
            LocalFree(argv);
        }

        // 2. Search the executable's folder and its parents.
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        fs::path dir = fs::path(buffer).parent_path();
        for (;;)
        {
            const fs::path candidate = dir / L"assets";
            if (fs::is_directory(candidate, ec))
                return candidate;
            if (!dir.has_parent_path() || dir.parent_path() == dir)
                break;
            dir = dir.parent_path();
        }
        return std::nullopt;
    }

    std::optional<fs::path> BrowseForFolder(HWND owner, const wchar_t* title)
    {
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return std::nullopt;
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title);
        if (FAILED(dialog->Show(owner)))
            return std::nullopt;
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item)))
            return std::nullopt;
        PWSTR path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            return std::nullopt;
        fs::path result = path;
        CoTaskMemFree(path);
        return result;
    }

    std::wstring ValidateDialogFileName(std::wstring& name)
    {
        while (!name.empty() && iswspace(name.front()))
            name.erase(name.begin());
        while (!name.empty() && iswspace(name.back()))
            name.pop_back();
        if (name.empty())
            return L"Enter a file name.";
        if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
            return L"A file name can't contain any of these characters: \\ / : * ? \" < > |";
        if (name.size() < 5 || !EqualsNoCase(std::wstring_view(name).substr(name.size() - 5), L".json"))
            name += L".json";
        if (name == L".json" || name.starts_with(L"."))
            return L"Enter a file name.";
        static const wchar_t* reserved[] = { L"con", L"prn", L"aux", L"nul", L"com1", L"lpt1" };
        const std::wstring stem = fs::path(name).stem().wstring();
        for (const wchar_t* r : reserved)
            if (EqualsNoCase(stem, r))
                return L"That name is reserved by Windows.";
        return {};
    }

    bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
    {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }
}
