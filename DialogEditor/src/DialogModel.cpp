#include "DialogModel.h"
#include <algorithm>
#include <format>

namespace de::model
{
    namespace
    {
        std::string Lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return out;
        }

        int FindLanguage(const Node& object, std::string_view language)
        {
            const std::string want = Lower(language);
            for (size_t i = 0; i < object.elements.size(); ++i)
            {
                if (Lower(object.elements[i].key) == want)
                    return static_cast<int>(i);
            }
            return -1;
        }

        template <class Fn>
        void ForEachEntry(Document& doc, Fn&& fn)
        {
            Node* segments = Segments(doc, false);
            if (!segments)
                return;
            for (size_t s = 0; s < segments->Size(); ++s)
            {
                Node* segment = segments->At(s);
                if (!segment || !segment->IsObject())
                    continue;
                Node* entries = Entries(doc, *segment, false);
                if (!entries)
                    continue;
                for (size_t e = 0; e < entries->Size(); ++e)
                {
                    Node* entry = entries->At(e);
                    if (entry && entry->IsObject())
                        fn(*entry);
                }
            }
        }
    }

    std::string GetString(const Node* object, std::string_view key)
    {
        if (!object)
            return {};
        const Node* value = object->Find(key);
        return value ? value->Str() : std::string();
    }

    void SetString(Document& doc, Node& object, std::string_view key, std::string_view value, bool removeIfEmpty)
    {
        if (value.empty() && removeIfEmpty)
        {
            doc.Remove(object, key);
            return;
        }
        doc.SetString(object, key, value);
    }

    std::string GetLoc(const Node* object, std::string_view key, std::string_view language)
    {
        if (!object)
            return {};
        const Node* value = object->Find(key);
        if (!value)
            return {};
        if (value->IsString())
            return value->text;
        if (value->IsObject())
        {
            const int index = FindLanguage(*value, language);
            if (index >= 0)
                return value->elements[static_cast<size_t>(index)].value->Str();
        }
        return {};
    }

    std::string DisplayLoc(const Node* object, std::string_view key, std::string_view language)
    {
        if (!object)
            return {};
        const Node* value = object->Find(key);
        if (!value)
            return {};
        if (value->IsString())
            return value->text;
        if (!value->IsObject() || value->elements.empty())
            return {};
        for (std::string_view lang : { language, std::string_view("en") })
        {
            const int index = FindLanguage(*value, lang);
            if (index >= 0)
                return value->elements[static_cast<size_t>(index)].value->Str();
        }
        return value->elements.front().value->Str();
    }

    void SetLoc(Document& doc, Node& object, std::string_view key, std::string_view language, std::string_view value)
    {
        Node* existing = object.Find(key);
        if (existing && existing->IsString())
        {
            if (Lower(language) == "en")
            {
                doc.SetString(object, key, value);
                return;
            }
            // Convert the shared plain string into per-language text.
            const std::string original = existing->text;
            Node* converted = doc.Set(object, key, json::MakeObject());
            if (!original.empty())
                doc.SetString(*converted, "en", original);
            doc.SetString(*converted, language, value);
            return;
        }
        if (!existing || !existing->IsObject())
        {
            if (value.empty() && !existing)
                return;
            existing = doc.Set(object, key, json::MakeObject());
        }
        const int index = FindLanguage(*existing, language);
        if (index >= 0)
            doc.SetString(*existing, existing->elements[static_cast<size_t>(index)].key, value);
        else
            doc.SetString(*existing, language, value);
    }

    void CollectLanguages(const Node* value, std::set<std::string>& languages)
    {
        if (!value || !value->IsObject())
            return;
        for (const auto& e : value->elements)
        {
            if (e.value && e.value->IsString())
                languages.insert(Lower(e.key));
        }
    }

    Node* Segments(Document& doc, bool create)
    {
        Node* root = doc.Root();
        if (!root || !root->IsObject())
            return nullptr;
        for (std::string_view key : { "segments", "sections" })
        {
            if (Node* n = root->Find(key); n && n->IsArray())
                return n;
        }
        return create ? doc.Set(*root, "segments", json::MakeArray()) : nullptr;
    }

    Node* NextLevels(Document& doc, bool create)
    {
        Node* root = doc.Root();
        if (!root || !root->IsObject())
            return nullptr;
        for (std::string_view key : { "nextLevels", "next-levels" })
        {
            if (Node* n = root->Find(key); n && n->IsArray())
                return n;
        }
        return create ? doc.Set(*root, "nextLevels", json::MakeArray()) : nullptr;
    }

    Node* Entries(Document& doc, Node& segment, bool create)
    {
        if (Node* n = segment.Find("entries"); n && n->IsArray())
            return n;
        return create ? doc.Set(segment, "entries", json::MakeArray()) : nullptr;
    }

    Node& QuestionHolder(Node& entry)
    {
        if (Node* q = entry.Find("question"); q && q->IsObject())
            return *q;
        return entry;
    }

    Node& TextHolder(Node& entry)
    {
        if (Node* q = entry.Find("question"); q && q->IsObject() && q->Find("text"))
            return *q;
        return entry;
    }

    Node* Responses(Document& doc, Node& entry, bool create)
    {
        Node& holder = QuestionHolder(entry);
        if (Node* n = holder.Find("responses"); n && n->IsArray())
            return n;
        return create ? doc.Set(holder, "responses", json::MakeArray()) : nullptr;
    }

    std::string ResponseId(const Node* response)
    {
        if (!response)
            return {};
        if (response->Find("id"))
            return GetString(response, "id");
        return GetString(response, "identifier");
    }

    void SetResponseId(Document& doc, Node& response, std::string_view id)
    {
        if (!response.Find("id") && response.Find("identifier"))
            doc.SetString(response, "identifier", id);
        else
            doc.SetString(response, "id", id);
    }

    std::set<std::string> Languages(Document& doc)
    {
        std::set<std::string> languages;
        if (Node* root = doc.Root())
            CollectLanguages(root->Find("title"), languages);
        ForEachEntry(doc, [&](Node& entry)
        {
            CollectLanguages(entry.Find("speaker"), languages);
            CollectLanguages(entry.Find("text"), languages);
            Node& holder = QuestionHolder(entry);
            if (&holder != &entry)
                CollectLanguages(holder.Find("text"), languages);
            if (Node* responses = Responses(doc, entry, false))
            {
                for (size_t i = 0; i < responses->Size(); ++i)
                    if (Node* r = responses->At(i); r && r->IsObject())
                        CollectLanguages(r->Find("text"), languages);
            }
        });
        return languages;
    }

    std::string UniqueSegmentId(Document& doc, std::string_view base)
    {
        std::set<std::string> used;
        if (Node* segments = Segments(doc, false))
        {
            for (size_t i = 0; i < segments->Size(); ++i)
                used.insert(GetString(segments->At(i), "id"));
        }
        for (int n = 1;; ++n)
        {
            std::string id = std::format("{}{}", base, n);
            if (!used.contains(id))
                return id;
        }
    }

    int RenameGotoTargets(Document& doc, std::string_view oldId, std::string_view newId)
    {
        if (oldId.empty() || oldId == newId)
            return 0;
        int changed = 0;
        ForEachEntry(doc, [&](Node& entry)
        {
            Node* responses = Responses(doc, entry, false);
            if (!responses)
                return;
            for (size_t i = 0; i < responses->Size(); ++i)
            {
                Node* r = responses->At(i);
                if (r && r->IsObject() && GetString(r, "goto") == oldId)
                {
                    doc.SetString(*r, "goto", newId);
                    ++changed;
                }
            }
        });
        return changed;
    }

    std::string NewDialogText()
    {
        return "{\n"
               "  \"typeSound\": \"sounds/type.wav\",\n"
               "  \"segments\": [\n"
               "    {\n"
               "      \"id\": \"start\",\n"
               "      \"entries\": []\n"
               "    }\n"
               "  ],\n"
               "  \"nextLevels\": []\n"
               "}\n";
    }

    bool NormalizeLanguageCode(std::string& code)
    {
        // Trim and lower-case.
        while (!code.empty() && code.front() == ' ')
            code.erase(code.begin());
        while (!code.empty() && code.back() == ' ')
            code.pop_back();
        code = Lower(code);
        std::replace(code.begin(), code.end(), '_', '-');
        if (code.empty() || code.size() > 35)
            return false;
        // language[-subtag]...: letters for the first part, letters/digits after.
        size_t start = 0;
        bool first = true;
        while (start <= code.size())
        {
            size_t end = code.find('-', start);
            if (end == std::string::npos)
                end = code.size();
            const std::string_view part(code.data() + start, end - start);
            if (part.empty() || part.size() > 8)
                return false;
            for (char c : part)
            {
                const bool letter = c >= 'a' && c <= 'z';
                const bool digit = c >= '0' && c <= '9';
                if (!letter && !(digit && !first))
                    return false;
            }
            if (first && (part.size() < 2 || part.size() > 3))
                return false;
            first = false;
            start = end + 1;
        }
        return true;
    }
}
