// ModelTests.cpp - portable tests for the editor's lossless JSON layer and dialog model.
// These files have no Windows dependencies, so the tests build with any C++23 compiler:
//
//   g++ -std=c++23 -I../src ModelTests.cpp ../src/LosslessJson.cpp ../src/DialogModel.cpp -o tests
//   ./tests ../../PlutoFall/assets
//
// (In a Visual Studio Developer Command Prompt:
//   cl /std:c++latest /EHsc /utf-8 /I..\src ModelTests.cpp ..\src\LosslessJson.cpp ..\src\DialogModel.cpp)
#include "DialogModel.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace de;
using namespace de::json;

namespace
{
    int g_failures = 0;

#define CHECK(condition)                                                                  \
    do                                                                                    \
    {                                                                                     \
        if (!(condition))                                                                 \
        {                                                                                 \
            std::cout << "FAILED (line " << __LINE__ << "): " #condition "\n";            \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (false)

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    template <class Fn>
    std::string Edit(std::string_view source, Fn&& edit)
    {
        Document doc;
        std::string error;
        if (!doc.Parse(source, error))
        {
            std::cout << "parse error: " << error << "\n";
            ++g_failures;
            return {};
        }
        edit(doc);
        return doc.Serialize();
    }

    void RoundTripAssets(const std::filesystem::path& assets)
    {
        int count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assets))
        {
            if (entry.path().extension() != ".json")
                continue;
            const std::string text = ReadFile(entry.path());
            Document doc;
            std::string error;
            CHECK(doc.Parse(text, error));
            CHECK(doc.Serialize() == text);
            ++count;
        }
        std::cout << "Round-tripped " << count << " asset files unchanged.\n";
    }

    void PreservesTrivia()
    {
        const std::string text = "\xEF\xBB\xBF// header\r\n{\r\n  /* c */ \"a\" : 1.50e+2, // tail\r\n"
                                 "  \"b\": [1,2,],\r\n  \"x\": {\"y\": null}\r\n}\r\n";
        Document doc;
        std::string error;
        CHECK(doc.Parse(text, error));
        CHECK(doc.Serialize() == text);
        CHECK(doc.Newline() == "\r\n");
    }

    void EditsKeepFormatting()
    {
        const std::string source = "{\n    // keep me\n    \"title\": \"Old\",\n    \"segments\": [\n        {\n"
                                   "            \"id\": \"a\",\n            \"entries\": []\n        }\n    ],\n"
                                   "    \"custom\": { \"z\": 1 }\n}\n";
        const std::string out = Edit(source, [](Document& d)
        {
            Node* root = d.Root();
            d.SetString(*root, "title", "New");
            Node* segments = root->Find("segments");
            Node* entries = segments->At(0)->Find("entries");
            Node* entry = d.Insert(*entries, 0, MakeObject());
            d.SetString(*entry, "speaker", "Vega");
            d.Insert(*segments, 1, MakeObject());
        });
        CHECK(out.find("// keep me") != std::string::npos);
        CHECK(out.find("\"custom\": { \"z\": 1 }") != std::string::npos);
        CHECK(out.find("\n                {\n                    \"speaker\": \"Vega\"\n                }\n") != std::string::npos);
    }

    void MoveAndRemove()
    {
        const std::string list = "{\n  \"s\": [\n    \"a\",\n    // about b\n    \"b\",\n    \"c\"\n  ]\n}";
        CHECK(Edit(list, [](Document& d) { d.Move(*d.Root()->Find("s"), 2, 0); }) ==
              "{\n  \"s\": [\n    \"c\",\n    \"a\",\n    // about b\n    \"b\"\n  ]\n}");
        CHECK(Edit(list, [](Document& d)
        {
            Node* s = d.Root()->Find("s");
            while (s->Size())
                d.RemoveAt(*s, 0);
        }) == "{\n  \"s\": []\n}");
        CHECK(Edit("[1, 2]", [](Document& d) { d.Insert(*d.Root(), 0, MakeString("z")); }) == "[\"z\", 1, 2]");
    }

    void DialogModel(const std::filesystem::path& assets)
    {
        Document doc;
        std::string error;
        CHECK(doc.Parse(ReadFile(assets / "dialog" / "intro.json"), error));
        CHECK(model::Languages(doc).contains("es"));
        Node* segments = model::Segments(doc, false);
        CHECK(segments && segments->Size() == 3);
        Node* question = model::Entries(doc, *segments->At(0), false)->At(3);
        CHECK(model::GetString(&model::QuestionHolder(*question), "key") == "route");
        CHECK(model::ResponseId(model::Responses(doc, *question, false)->At(0)) == "belt");
        CHECK(model::RenameGotoTargets(doc, "belt", "asteroids") == 1);

        // Plain strings are shared by all languages until another language is written.
        Document plain;
        CHECK(plain.Parse("{\"segments\":[{\"id\":\"a\",\"entries\":[{\"text\":\"Hello\"}]}]}", error));
        Node* entry = model::Entries(plain, *model::Segments(plain, false)->At(0), false)->At(0);
        CHECK(model::GetLoc(entry, "text", "es") == "Hello");
        model::SetLoc(plain, *entry, "text", "es", "Hola");
        CHECK(model::GetLoc(entry, "text", "en") == "Hello");
        CHECK(model::GetLoc(entry, "text", "es") == "Hola");

        std::string code = " pt_BR ";
        CHECK(model::NormalizeLanguageCode(code) && code == "pt-br");
        std::string bad = "english";
        CHECK(!model::NormalizeLanguageCode(bad));
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path assets = argc > 1 ? argv[1] : "../../PlutoFall/assets";
    RoundTripAssets(assets);
    PreservesTrivia();
    EditsKeepFormatting();
    MoveAndRemove();
    DialogModel(assets);
    std::cout << (g_failures == 0 ? "All tests passed.\n" : "Some tests FAILED.\n");
    return g_failures == 0 ? 0 : 1;
}
