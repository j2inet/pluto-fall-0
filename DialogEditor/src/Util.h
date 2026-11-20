// Util.h - string conversion, file IO and asset folder discovery.
#pragma once
#include "Common.h"

namespace de
{
    std::wstring Widen(std::string_view utf8);
    std::string Narrow(std::wstring_view wide);

    // Converts between the "\n" line endings stored in JSON and the "\r\n" used by edit controls.
    std::wstring ToEditText(std::string_view utf8);
    std::string FromEditText(std::wstring_view text);

    std::optional<std::string> ReadFileText(const fs::path& path);
    // Writes through a temporary file so a failed save never truncates the original.
    bool WriteFileText(const fs::path& path, std::string_view text, std::wstring& error);

    std::wstring LastErrorMessage(DWORD error = GetLastError());

    // Finds the assets folder: the "/asset <path>" command-line argument if given,
    // otherwise the first "assets" folder found in the executable's folder or any parent.
    // `error` is set when /asset names a folder that does not exist.
    std::optional<fs::path> FindAssetsFolder(std::wstring& error);

    // Lets the user browse for a folder. Returns nullopt if cancelled.
    std::optional<fs::path> BrowseForFolder(HWND owner, const wchar_t* title);

    // Checks a file name typed by the user and appends ".json" if needed.
    // Returns an error message, or an empty string when valid.
    std::wstring ValidateDialogFileName(std::wstring& name);

    bool EqualsNoCase(std::wstring_view a, std::wstring_view b);
}
