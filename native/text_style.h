#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <winrt/base.h>

namespace callayers {
// Both the outgoing annotation and the private editor use this same stylesheet.
inline std::wstring ReadTextDocument(const wchar_t* name) {
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    auto directory=std::filesystem::path(exe).parent_path()/L"web";
    auto read=[&](const wchar_t* fileName){
        std::ifstream file(directory/fileName,std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file),{}};
        if(!file || bytes.empty())throw std::runtime_error("Missing text document");
        return std::wstring(winrt::to_hstring(bytes));
    };
    auto html=read(name);const std::wstring marker=L"/*CALLAYERS_TEXT_STYLE*/";
    auto offset=html.find(marker);if(offset==std::wstring::npos)throw std::runtime_error("Missing text stylesheet marker");
    html.replace(offset,marker.size(),read(L"text-style.css"));return html;
}
}
