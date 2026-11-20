// DialogModel.h - typed access to Pluto Fall dialog files on top of the lossless JSON tree.
//
// The editor reads and writes the JSON tree directly instead of converting it to structs, so
// anything it does not understand (unknown keys, comments, formatting) is left untouched.
// The functions here understand the same variations the game's loader accepts:
//   "segments" or "sections"; "nextLevels" or "next-levels"; response "id" or "identifier";
//   questions written inline on the entry or in a nested "question" object.
//
// No Windows dependencies.
#pragma once
#include "LosslessJson.h"
#include <set>
#include <string>
#include <string_view>

namespace de::model
{
    using json::Document;
    using json::Node;

    // ---- plain string members

    std::string GetString(const Node* object, std::string_view key);
    // Sets a string member. When `value` is empty and removeIfEmpty is set, the member is removed.
    void SetString(Document& doc, Node& object, std::string_view key, std::string_view value, bool removeIfEmpty);

    // ---- multi-language strings
    //
    // A multi-language value is either a plain string (shared by every language) or an object
    // keyed by language code. Reading returns the exact language's text, or the plain string.
    // Writing to a plain string in "en" updates it in place; writing another language converts
    // it to an object, keeping the original text under "en".

    std::string GetLoc(const Node* object, std::string_view key, std::string_view language);
    void SetLoc(Document& doc, Node& object, std::string_view key, std::string_view language, std::string_view value);
    void CollectLanguages(const Node* value, std::set<std::string>& languages);

    // Text for list displays: the language if present, otherwise English, otherwise the first entry.
    std::string DisplayLoc(const Node* object, std::string_view key, std::string_view language);

    // ---- dialog structure

    Node* Segments(Document& doc, bool create);
    Node* NextLevels(Document& doc, bool create);
    Node* Entries(Document& doc, Node& segment, bool create);

    // The object holding an entry's question ("key" and "responses"): the nested
    // "question" object when there is one, otherwise the entry itself.
    Node& QuestionHolder(Node& entry);
    // The object holding an entry's text (a nested question's "text" overrides the entry's).
    Node& TextHolder(Node& entry);
    Node* Responses(Document& doc, Node& entry, bool create);

    std::string ResponseId(const Node* response);
    void SetResponseId(Document& doc, Node& response, std::string_view id);

    // Every language code used by the dialog's multi-language fields.
    std::set<std::string> Languages(Document& doc);

    // A segment id not used by any segment ("segment1", "segment2", ...).
    std::string UniqueSegmentId(Document& doc, std::string_view base = "segment");

    // Renames goto targets in every response from oldId to newId. Returns how many changed.
    int RenameGotoTargets(Document& doc, std::string_view oldId, std::string_view newId);

    // Template used for "New dialogue".
    std::string NewDialogText();

    // Lower-cases a language code and checks it looks like one ("en", "pt-br", "zh-hant").
    bool NormalizeLanguageCode(std::string& code);
}
