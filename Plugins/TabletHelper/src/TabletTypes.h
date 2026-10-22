#pragma once

// Tablet type keys, detection and classification. Pure standard C++ (no host or
// ImGui dependency), shared by the scan worker, the matcher and the tests.

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace TabletHelper {

// Stable ASCII type keys: the JSON keys of settings.json and the catalog keys.
// Never localised.
namespace TypeKeys {
inline constexpr const char* Unknown    = "Unknown";
inline constexpr const char* Irradiated = "Irradiated";
inline constexpr const char* Breach     = "Breach";
inline constexpr const char* Delirium   = "Delirium";
inline constexpr const char* Expedition = "Expedition";
inline constexpr const char* Abyss      = "Abyss";
inline constexpr const char* Ritual     = "Ritual";
inline constexpr const char* Overseer   = "Overseer";
inline constexpr const char* Temple     = "Temple";
inline constexpr const char* Global     = "Global";  // the cross-type "All types" scope
}  // namespace TypeKeys

// The eight real tablet types in canonical order (Global is handled separately).
inline constexpr std::array<const char*, 8> kRealTypeKeys = {
    TypeKeys::Irradiated, TypeKeys::Breach, TypeKeys::Delirium, TypeKeys::Expedition,
    TypeKeys::Abyss,      TypeKeys::Ritual, TypeKeys::Overseer, TypeKeys::Temple};

inline std::string ToLowerCopy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

// Allocation-free case-insensitive substring test. `needleLower` must already be
// lowercase.
inline bool ContainsCI(std::string_view hay, std::string_view needleLower) {
    if (needleLower.empty()) return true;
    if (needleLower.size() > hay.size()) return false;
    for (size_t i = 0; i + needleLower.size() <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < needleLower.size(); ++j) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(hay[i + j])));
            if (c != needleLower[j]) break;
        }
        if (j == needleLower.size()) return true;
    }
    return false;
}

// Lowercase alphanumerics only: "TowerBreachBossChance1" -> "towerbreachbosschance1".
inline std::string NormalizeIdentifier(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value)
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

// Drops a trailing run of digits (tier suffix): "towerbreachbosschance1" ->
// "towerbreachbosschance".
inline std::string StripTrailingDigits(const std::string& v) {
    size_t end = v.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(v[end - 1]))) --end;
    return v.substr(0, end);
}

// A Precursor Tablet has a metadata path under Metadata/Items/TowerAugment/
// (BreachAugment, GenericAugment, MapBossAugment, ...). The English base-type
// fallback only matters when a host leaves the path empty.
inline bool LooksLikeTablet(std::string_view path, std::string_view baseType) {
    if (ContainsCI(path, "toweraugment")) return true;
    if (ContainsCI(baseType, "precursor tablet")) return true;
    return false;
}

// Type from the metadata path (plus the base type as a safety net). Order
// matters: the first match wins. Unknown when nothing matched; the scanner then
// tries the implicit mods.
inline const char* ClassifyFromText(std::string_view path, std::string_view baseType) {
    auto has = [&](const char* kw) { return ContainsCI(path, kw) || ContainsCI(baseType, kw); };
    if (has("abyss")) return TypeKeys::Abyss;
    if (has("breach")) return TypeKeys::Breach;
    if (has("delirium")) return TypeKeys::Delirium;
    if (has("expedition")) return TypeKeys::Expedition;
    if (has("ritual")) return TypeKeys::Ritual;
    if (has("boss") || has("overseer")) return TypeKeys::Overseer;
    if (has("incursion") || has("temple") || has("vaal")) return TypeKeys::Temple;
    if (has("generic") || has("irradiated") || has("toweraugment")) return TypeKeys::Irradiated;
    return TypeKeys::Unknown;
}

// Fallback from a lowercased blob of the implicit mods' ids and stat keys.
inline const char* ClassifyFromModText(std::string_view lowerModText) {
    if (lowerModText.empty()) return TypeKeys::Unknown;
    auto has = [&](const char* kw) { return lowerModText.find(kw) != std::string_view::npos; };
    if (has("abyss")) return TypeKeys::Abyss;
    if (has("breach")) return TypeKeys::Breach;
    if (has("delirium")) return TypeKeys::Delirium;
    if (has("expedition")) return TypeKeys::Expedition;
    if (has("ritual")) return TypeKeys::Ritual;
    if (has("boss")) return TypeKeys::Overseer;
    if (has("incursion") || has("beacon") || has("vaal")) return TypeKeys::Temple;
    if (has("irradiated")) return TypeKeys::Irradiated;
    return TypeKeys::Unknown;
}

inline bool IsRealType(std::string_view key) {
    for (const char* k : kRealTypeKeys)
        if (key == k) return true;
    return false;
}

// Name in the settings tab.
inline const char* DisplayName(std::string_view key) {
    if (key == TypeKeys::Irradiated) return "Irradiated Tablet";
    if (key == TypeKeys::Breach)     return "Breach Tablet";
    if (key == TypeKeys::Delirium)   return "Delirium Tablet";
    if (key == TypeKeys::Expedition) return "Expedition Tablet";
    if (key == TypeKeys::Abyss)      return "Abyss Tablet";
    if (key == TypeKeys::Ritual)     return "Ritual Tablet";
    if (key == TypeKeys::Overseer)   return "Overseer Tablet";
    if (key == TypeKeys::Temple)     return "Temple Tablet";
    if (key == TypeKeys::Global)     return "All types";
    return "Unknown Tablet";
}

// Label drawn on the tablet in game.
inline const char* ShortName(std::string_view key) {
    if (key == TypeKeys::Irradiated) return "Irradiated";
    if (key == TypeKeys::Breach)     return "Breach";
    if (key == TypeKeys::Delirium)   return "Delirium";
    if (key == TypeKeys::Expedition) return "Expedition";
    if (key == TypeKeys::Abyss)      return "Abyss";
    if (key == TypeKeys::Ritual)     return "Ritual";
    if (key == TypeKeys::Overseer)   return "Overseer";
    if (key == TypeKeys::Temple)     return "Temple";
    if (key == TypeKeys::Global)     return "All types";
    return "Unknown";
}

// Three-letter label used when the short name does not fit the cell.
inline const char* Abbreviation(std::string_view key) {
    if (key == TypeKeys::Irradiated) return "Irr";
    if (key == TypeKeys::Breach)     return "Bre";
    if (key == TypeKeys::Delirium)   return "Del";
    if (key == TypeKeys::Expedition) return "Exp";
    if (key == TypeKeys::Abyss)      return "Aby";
    if (key == TypeKeys::Ritual)     return "Rit";
    if (key == TypeKeys::Overseer)   return "Ovs";
    if (key == TypeKeys::Temple)     return "Tmp";
    return "?";
}

}  // namespace TabletHelper
