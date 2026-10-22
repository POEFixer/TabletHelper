// Tablet Helper settings tab (drawn under the host's Enabled header).
// One card per topic, one measured label column per card, widths from the
// available region, wrapped tooltips, dimmed (not hidden) dependent controls,
// stable ### ids. Every edit goes through MarkChanged(): the worker gets the new
// rules at once and the file is saved about a second after the last edit.

#include "../TabletHelperPlugin.h"
#include "../TabletBonusCatalog.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "../../../../POEFixer/imgui/imgui.h"
#include "../../../../POEFixer/imgui/imgui_internal.h"  // ImTextInitClassifiers
#include "../../../../POEFixer/plugin_sdk/PluginUi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Ui = PluginUi;
using TabletHelperConfig::TypeConfig;

namespace {

bool Contains(const std::vector<std::string>& v, const std::string& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}
void RemoveFrom(std::vector<std::string>& v, const std::string& id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

std::string PathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// One line summing up a type's rule for the types table.
std::string RuleSummary(const TypeConfig& t, bool readMods) {
    if (!t.enabled) return "Off";
    const int sel = static_cast<int>(t.selectedBonusIds.size());
    const int req = static_cast<int>(t.requiredBonusIds.size());
    std::string s;
    if (sel == 0) {
        s = "Every tablet";
    } else if (req > 0) {
        s = std::to_string(req) + " required";
        if (sel - req > 0) s += " + " + std::to_string(sel - req) + " optional";
        s += sel == 1 ? " bonus" : " bonuses";
    } else {
        s = std::to_string(sel) + (sel == 1 ? " bonus" : " bonuses");
    }
    if (t.minUsesLeft > 0) s += ", " + std::to_string(t.minUsesLeft) + "+ uses";
    if (!readMods && (sel > 0 || t.minUsesLeft > 0)) s += " (filters paused)";
    return s;
}

// The text, or its longest prefix plus "..." that fits maxW (UTF-8 safe).
std::string FitWithEllipsis(const std::string& text, float maxW) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxW) return text;
    std::string cut = text;
    while (!cut.empty()) {
        size_t n = cut.size() - 1;
        while (n > 0 && (static_cast<unsigned char>(cut[n]) & 0xC0) == 0x80) --n;
        cut.resize(n);
        while (!cut.empty() && cut.back() == ' ') cut.pop_back();
        const std::string candidate = cut + "...";
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxW) return candidate;
    }
    return "...";
}

// Width of the types table and the filter editor: the whole card when narrow,
// at most ~42 em so names, rules and buttons stay close together on wide panes.
float ListWidth() {
    return (std::min)(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 42.0f);
}

// A wrapped line in the kit's warning colour (the kit's Hint only has the
// secondary colour).
void WarningLine(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Warning);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}  // namespace

void TabletHelperPlugin::DrawSettings() {
    if (!ctx()->ImGuiContext) return;  // incompatible host: no ImGui context to draw into
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    // ImGui 1.93 keeps its word-wrap character classes in a per-DLL static that
    // only the copy which builds the font atlas initialises (the host's). This
    // DLL's copy wraps our text, so set it up here (idempotent, one check).
    ImTextInitClassifiers();
    if (!m_loaded) {
        Ui::Hint("Tablet Helper is not active: this POEFixer version is not compatible with the plugin.");
        return;
    }

    ImGui::PushID("TabletHelper");
    ImGui::Spacing();
    DrawSectionStatus();

    const bool twoColumns = Ui::WideEnoughForTwoColumns() && ImGui::BeginTable("##layout", 2, ImGuiTableFlags_SizingStretchSame);
    if (twoColumns) {
        ImGui::TableNextColumn();
        DrawSectionHighlight();
        DrawSectionDetection();
        ImGui::TableNextColumn();
        DrawSectionLabels();
        ImGui::EndTable();
    } else {
        DrawSectionHighlight();
        DrawSectionLabels();
        DrawSectionDetection();
    }
    DrawSectionProfiles();
    DrawSectionTypes();
    DrawSectionDiagnostics();
    ImGui::PopID();

    if (m_filterDirty) PushFilter();
    SaveIfDue(false);
}

// ---------------------------------------------------------------------------
// Status: what the plugin does and what it sees right now
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionStatus() {
    Ui::BeginCard("##status");
    Ui::Heading(ICON_FA_GEM, "Precursor Tablets");
    Ui::HelpMarker("Open your inventory, a stash tab (the Tablet stash too), the guild stash or a merchant: "
                   "matching tablets are outlined in their type's colour.\n\n"
                   "Each tablet type has its own rule below. A rule without bonuses highlights every tablet "
                   "of that type; pick bonuses to highlight only tablets that carry them. "
                   "\"All types\" is an extra rule that applies to every type.\n\n"
                   "Filter profiles keep several sets of rules you can switch between.");
    Ui::Hint("Highlights Precursor Tablets in every open item window, coloured by type and filtered by "
             "their bonuses and uses left.");
    ImGui::Spacing();

    const auto st = m_worker ? m_worker->Stats() : TabletHelper::WorkerStats{};
    const Ui::Palette pal = Ui::Colors();
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(pal.Accent);
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(pal.TextSecondary);
    const ImU32 warning = ImGui::ColorConvertFloat4ToU32(pal.Warning);
    char buf[64];
    if (!ctx()->Game.IsInGame()) {
        Ui::StatusPill("Not in game", muted);
    } else if (!st.itemWindowOpen) {
        Ui::StatusPill("No item window open", muted);
    } else {
        std::snprintf(buf, sizeof(buf), "%d on screen", st.visibleTablets);
        Ui::StatusPill(buf, muted);
        ImGui::SameLine();
        std::snprintf(buf, sizeof(buf), "%d highlighted", st.highlighted);
        Ui::StatusPill(buf, accent);
        if (st.pendingReads > 0) {
            ImGui::SameLine();
            std::snprintf(buf, sizeof(buf), "%d reading", st.pendingReads);
            Ui::StatusPill(buf, warning);
            Ui::Tip("Tablets whose modifiers are still being read. Tablets matched by bonuses or uses "
                    "left appear as soon as their read completes (a full Tablet stash takes about a second).");
        }
    }
    Ui::EndCard();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Highlight style
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionHighlight() {
    auto& s = m_settings;
    Ui::BeginCard("##highlight");
    Ui::Heading(ICON_FA_HIGHLIGHTER, "Highlight");
    const Ui::LabelColumn col({"Style", "Line thickness"});

    col.Row("Style");
    int mode = static_cast<int>(s.highlightMode);
    if (Ui::Combo("###style", mode, {"Border", "Cross", "Star"})) {
        s.highlightMode = static_cast<TabletHelperConfig::HighlightMode>(mode);
        MarkChanged();
    }
    Ui::Tip("How a matching tablet is marked:\n"
            "Border - a frame around the tablet's cell.\n"
            "Cross - two diagonal lines over the tablet.\n"
            "Star - a small star in the cell's lower-left corner.");

    col.Row("Line thickness");
    if (ImGui::SliderInt("###thickness", &s.borderThickness, TabletHelperConfig::kBorderThicknessMin,
                         TabletHelperConfig::kBorderThicknessMax, "%d px"))
        MarkChanged();
    Ui::Tip("Width of the frame, cross or star lines in pixels.");
    Ui::EndCard();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Labels drawn on the tablets
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionLabels() {
    auto& s = m_settings;
    Ui::BeginCard("##labels");
    Ui::Heading(ICON_FA_TAG, "Labels");

    if (Ui::Check("Type name", "showLabel", &s.showLabel,
                  "Writes the tablet's type (Breach, Ritual, ...) in the top of its cell. A name too long "
                  "for the cell is drawn smaller, then shortened to three letters (Exp, Ovs, ...)."))
        MarkChanged();
    ImGui::Indent();
    ImGui::BeginDisabled(!s.showLabel);
    if (Ui::Check("Matched bonuses", "showBonuses", &s.showMatchedBonuses,
                  "Lists the selected bonuses the tablet carries under its type name, required ones first. "
                  "Names that do not fit the cell are shortened and \"+N\" counts the ones left out.\n"
                  "A cell too small for names (most stash cells) shows \xEF\x80\x8C" "N instead: how many of "
                  "the selected bonuses the tablet carries."))
        MarkChanged();
    ImGui::EndDisabled();
    ImGui::Unindent();
    if (Ui::Check("Uses left", "showUses", &s.showUsesLeft,
                  "Shows the tablet's remaining uses as \xC3\x97N in the cell's lower-right corner.\n"
                  "Needs \"Read tablet modifiers\" (Detection)."))
        MarkChanged();

    ImGui::Spacing();
    const bool anyText = s.showLabel || s.showUsesLeft;
    ImGui::BeginDisabled(!anyText);
    const Ui::LabelColumn col({"Text colour", "Text size", "Background"});
    col.Row("Text colour");
    int tmode = static_cast<int>(s.labelTextMode);
    if (Ui::Combo("###textColour", tmode, {"White", "Tablet colour"})) {
        s.labelTextMode = static_cast<TabletHelperConfig::LabelTextMode>(tmode);
        MarkChanged();
    }
    Ui::Tip("Colour of the type name: white, or the colour of the tablet's rule.\n"
            "Bonus names and the uses badge are always white.");

    col.Row("Text size");
    int pct = static_cast<int>(s.labelScale * 100.0f + 0.5f);
    if (ImGui::SliderInt("###textSize", &pct, static_cast<int>(TabletHelperConfig::kLabelScaleMin * 100.0f),
                         static_cast<int>(TabletHelperConfig::kLabelScaleMax * 100.0f), "%d%%")) {
        s.labelScale = std::clamp(static_cast<float>(pct) / 100.0f, TabletHelperConfig::kLabelScaleMin,
                                  TabletHelperConfig::kLabelScaleMax);
        MarkChanged();
    }
    Ui::Tip("Label size relative to the overlay font. Labels never leave their tablet's cell: "
            "text that does not fit is drawn smaller or shortened.");

    col.Row("Background");
    int alphaPct = static_cast<int>(static_cast<float>(s.labelBackgroundAlpha) * 100.0f / 255.0f + 0.5f);
    ImGui::BeginDisabled(!s.labelBackground);
    if (ImGui::SliderInt("###bgAlpha", &alphaPct, 0, 100, "%d%% opaque")) {
        s.labelBackgroundAlpha = std::clamp(static_cast<int>(static_cast<float>(alphaPct) * 255.0f / 100.0f + 0.5f),
                                            TabletHelperConfig::kLabelBgAlphaMin, TabletHelperConfig::kLabelBgAlphaMax);
        MarkChanged();
    }
    Ui::Tip("Opacity of the dark box behind each label line.");
    ImGui::EndDisabled();

    if (Ui::Check("Dark box behind labels", "labelBg", &s.labelBackground,
                  "Draws a dark rounded box behind each label line so it reads on bright item art."))
        MarkChanged();
    if (Ui::Check("Text outline", "labelOutline", &s.labelOutline,
                  "Draws a thin black outline around label text."))
        MarkChanged();
    ImGui::EndDisabled();
    Ui::EndCard();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionDetection() {
    auto& s = m_settings;
    Ui::BeginCard("##detection");
    Ui::Heading(ICON_FA_MAGNIFYING_GLASS, "Detection");

    if (Ui::Check("Read tablet modifiers", "readMods", &s.readMods,
                  "Reads each tablet's modifiers once, when it first appears on screen. Needed for the "
                  "uses-left filter and badge and for every bonus filter.\n\n"
                  "Off: every enabled type is highlighted by type only and the filters are ignored."))
        MarkChanged();
    if (!s.readMods) WarningLine("Bonus and uses-left filters are paused while this is off.");

    ImGui::Spacing();
    const Ui::LabelColumn col({"Refresh every"});
    col.Row("Refresh every");
    if (ImGui::SliderInt("###interval", &s.scanIntervalMs, TabletHelperConfig::kScanIntervalMinMs,
                         TabletHelperConfig::kScanIntervalMaxMs, "%d ms"))
        MarkChanged();
    Ui::Tip("How often the open item windows are checked for tablets. Lower follows tab switches and "
            "moved items sooner; higher asks the game for less.\n"
            "With no item window open the plugin checks at most every 300 ms. Default 150 ms.");
    Ui::EndCard();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Filter profiles
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionProfiles() {
    auto& s = m_settings;
    auto& profiles = s.profiles;
    if (profiles.empty()) profiles.push_back(TabletHelperConfig::Profile{});
    if (s.activeProfile < 0 || s.activeProfile >= static_cast<int>(profiles.size())) s.activeProfile = 0;

    Ui::BeginCard("##profiles");
    Ui::Heading(ICON_FA_LAYER_GROUP, "Filter profiles");
    Ui::HelpMarker("A profile is a complete set of tablet rules: colours, bonuses and uses left for every "
                   "type. Keep one per strategy and switch here. Highlight, label and detection options "
                   "are shared by all profiles.");

    const Ui::LabelColumn col({"Active profile", "Name"});
    col.Row("Active profile");
    if (ImGui::BeginCombo("###profile", profiles[static_cast<size_t>(s.activeProfile)].name.c_str())) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            ImGui::PushID(i);
            const bool sel = i == s.activeProfile;
            if (ImGui::Selectable(profiles[static_cast<size_t>(i)].name.c_str(), sel) && !sel) {
                s.activeProfile = i;
                MarkChanged();
            }
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    Ui::Tip("The rules in use. Switching re-filters the open windows at once.");

    col.Row("Name");
    auto& name = profiles[static_cast<size_t>(s.activeProfile)].name;
    if (Ui::InputString("###profileName", "profile name", name)) {
        name = TabletHelperConfig::TruncateUtf8(name, TabletHelperConfig::kMaxProfileNameLen);
        MarkChanged();
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && name.empty()) {
        name = s.UniqueProfileName("Profile");
        MarkChanged();
    }
    Ui::Tip("Rename the active profile.");

    ImGui::Spacing();
    const int n = static_cast<int>(profiles.size());
    const bool atCap = n >= TabletHelperConfig::kMaxProfiles;
    ImGui::BeginDisabled(atCap);
    if (ImGui::Button(ICON_FA_PLUS "  New###newProfile")) {
        TabletHelperConfig::Profile p;
        p.name = s.UniqueProfileName("Profile");
        profiles.push_back(std::move(p));
        s.activeProfile = static_cast<int>(profiles.size()) - 1;
        MarkChanged();
    }
    Ui::Tip("Adds a profile with the default rules (every tablet by type) and switches to it.");
    ImGui::SameLine();
    if (Ui::SubtleButton(ICON_FA_COPY "  Duplicate###dupProfile")) {
        TabletHelperConfig::Profile p = profiles[static_cast<size_t>(s.activeProfile)];
        p.name = s.UniqueProfileName(p.name + " copy");
        profiles.push_back(std::move(p));
        s.activeProfile = static_cast<int>(profiles.size()) - 1;
        MarkChanged();
    }
    Ui::Tip("Copies the active profile and switches to the copy.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(n <= 1);
    if (Ui::SubtleButton(ICON_FA_TRASH "  Delete###delProfile")) ImGui::OpenPopup("##confirmDelete");
    Ui::Tip("Deletes the active profile (asks first). The last profile cannot be deleted.");
    ImGui::EndDisabled();
    if (Ui::ConfirmPopup("##confirmDelete", "Delete this profile and its rules?", "Delete", "Keep") &&
        profiles.size() > 1) {
        profiles.erase(profiles.begin() + s.activeProfile);
        s.activeProfile = std::clamp(s.activeProfile, 0, static_cast<int>(profiles.size()) - 1);
        MarkChanged();
    }
    if (atCap) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Profile limit reached (%d).", TabletHelperConfig::kMaxProfiles);
        Ui::Hint(buf);
    }
    Ui::EndCard();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Tablet types: one row per type, one editor open at a time
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionTypes() {
    auto& types = m_settings.ActiveTypes();
    Ui::BeginCard("##types");
    Ui::Heading(ICON_FA_TABLE_CELLS, "Tablet types");
    Ui::HelpMarker("Tick a type to highlight it and pick its colour. \"Edit\" opens the type's filter: "
                   "bonuses, roll values and uses left.\n\n"
                   "\"All types\" is an extra rule tested on every tablet. A tablet its own type's rule "
                   "does not highlight is still highlighted, in the \"All types\" colour, when that rule "
                   "accepts it.");

    const ImGuiStyle& st = ImGui::GetStyle();
    const float editW = Ui::ButtonWidth(ICON_FA_PEN "  Close");
    if (ImGui::BeginTable("##typeTable", 4,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_BordersInnerH,
                          ImVec2(ListWidth(), 0.0f))) {
        float nameW = 0.0f;
        for (const auto& t : types) nameW = (std::max)(nameW, ImGui::CalcTextSize(t.displayName.c_str()).x);
        ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFrameHeight() * 2.0f + st.ItemInnerSpacing.x);
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed, nameW + st.ItemSpacing.x);
        ImGui::TableSetupColumn("##rule", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##edit", ImGuiTableColumnFlags_WidthFixed, editW);

        for (auto& t : types) {
            ImGui::PushID(t.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##enabled", &t.enabled)) MarkChanged();
            Ui::Tip("Highlight tablets of this type.");
            ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
            if (Ui::Swatch("##colour", t.color)) MarkChanged();
            Ui::Tip("Highlight colour of this type (the alpha slider sets its opacity).");

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::BeginDisabled(!t.enabled);
            ImGui::TextUnformatted(t.displayName.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string summary = RuleSummary(t, m_settings.readMods);
            ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().TextSecondary);
            ImGui::TextUnformatted(FitWithEllipsis(summary, ImGui::GetContentRegionAvail().x).c_str());
            ImGui::PopStyleColor();
            Ui::Tip(summary.c_str());
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            const bool open = m_openType == t.key;
            if (Ui::SubtleButton(open ? ICON_FA_PEN "  Close###edit" : ICON_FA_PEN "  Edit###edit",
                                 ImVec2(editW, 0.0f)))
                m_openType = open ? std::string() : t.key;
            Ui::Tip(open ? "Close this type's filter." : "Open this type's filter: bonuses, roll values, uses left.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    for (auto& t : types) {
        if (t.key != m_openType) continue;
        ImGui::Spacing();
        DrawTypeEditor(t);
    }
    Ui::EndCard();
    ImGui::Spacing();
}

void TabletHelperPlugin::DrawTypeEditor(TypeConfig& t) {
    ImGui::PushID(t.key.c_str());
    ImGui::PushID("editor");
    const std::string title = t.displayName + " filter";
    Ui::Heading(ICON_FA_FILTER, title.c_str());

    const bool readMods = m_settings.readMods;
    if (!readMods) WarningLine("\"Read tablet modifiers\" is off: this filter is paused and every enabled "
                                   "tablet of the type is highlighted.");

    const int selCount = static_cast<int>(t.selectedBonusIds.size());
    const int reqCount = static_cast<int>(t.requiredBonusIds.size());
    const int optCount = selCount - reqCount;

    const Ui::LabelColumn col({"Minimum uses left", "Required bonuses", "Optional bonuses"});
    ImGui::BeginDisabled(!readMods);
    col.Row("Minimum uses left");
    if (ImGui::SliderInt("###minUses", &t.minUsesLeft, 0, TabletHelperConfig::kMinUsesMax,
                         t.minUsesLeft == 0 ? "Any" : "%d"))
        MarkChanged();
    Ui::Tip("Only highlight tablets with at least this many uses left. \"Any\" = no limit.");

    auto poolSlider = [&](const char* label, const char* id, int* stored, int pool, const char* tip) {
        col.Row(label);
        if (pool <= 0) {
            // Nothing to count yet: an empty, dimmed slider (a 1..1 slider would
            // draw its grab across the whole frame; 0..100 keeps it minimal).
            ImGui::BeginDisabled();
            int zero = 0;
            ImGui::SliderInt(id, &zero, 0, 100, "none selected");
            ImGui::EndDisabled();
            Ui::Tip(tip);
            return;
        }
        const int cap = std::clamp(pool, 1, TabletHelperConfig::kMinMatchedMax);
        int v = std::clamp(*stored, 1, cap);
        char fmt[48];
        std::snprintf(fmt, sizeof(fmt), "%%d of %d", pool);
        if (ImGui::SliderInt(id, &v, 1, cap, fmt)) {
            *stored = v;
            MarkChanged();
        }
        Ui::Tip(tip);
    };
    poolSlider("Required bonuses", "###minReq", &t.minRequiredBonuses, reqCount,
               "How many of the bonuses marked \"Required\" a tablet must carry. Set it to the number "
               "of required bonuses to demand all of them.");
    poolSlider("Optional bonuses", "###minOpt", &t.minMatchedBonuses, optCount,
               "How many of the other selected bonuses a tablet must carry.");
    ImGui::EndDisabled();

    if (selCount == 0) Ui::Hint("No bonus selected: every tablet of this type is highlighted.");
    ImGui::Spacing();
    DrawBonusTable(t);
    ImGui::PopID();
    ImGui::PopID();
}

bool TabletHelperPlugin::DrawValueField(const char* id, int* v, int lo, int hi) {
    // A small integer field where clearing it means 0 ("no limit").
    char buf[12];
    if (*v == 0) buf[0] = '\0';
    else std::snprintf(buf, sizeof(buf), "%d", *v);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("-0000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    bool changed = false;
    if (ImGui::InputTextWithHint(id, "-", buf, sizeof(buf),
                                 ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll)) {
        int nv = (buf[0] == '\0' || (buf[0] == '-' && buf[1] == '\0')) ? 0 : std::atoi(buf);
        nv = std::clamp(nv, lo, hi);
        if (nv != *v) {
            *v = nv;
            changed = true;
        }
    }
    return changed;
}

void TabletHelperPlugin::DrawBonusTable(TypeConfig& t) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const auto& bonuses = TabletHelper::GetBonusesFor(t.key);
    std::string& search = m_bonusSearch[t.key];
    bool& onlySelected = m_onlySelected[t.key];

    // Search row: field, "Selected only", "Clear"; the field gets its own line
    // when the row is too narrow for a useful field beside the other two.
    const float listW = ListWidth();
    const char* clearLabel = ICON_FA_ERASER "  Clear###clear";
    const float clearW = Ui::ButtonWidth(clearLabel);
    const float onlyW = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize("Selected only").x;
    const float fieldW = listW - clearW - onlyW - st.ItemSpacing.x * 2.0f;
    const bool stacked = fieldW < ImGui::GetFontSize() * 12.0f;
    ImGui::SetNextItemWidth(stacked ? listW : fieldW);
    Ui::InputString("###search", "search bonuses (name or id)", search);
    Ui::Tip("Type part of a bonus name or mod id, e.g. \"rarity\", \"surpassing\" or \"waystone\".");
    if (!stacked) ImGui::SameLine();
    Ui::Check("Selected only", "onlySel", &onlySelected, "Show only the bonuses ticked for this type.");
    ImGui::SameLine();
    ImGui::BeginDisabled(t.selectedBonusIds.empty() && t.valueFilters.empty());
    if (Ui::SubtleButton(clearLabel)) {
        t.selectedBonusIds.clear();
        t.requiredBonusIds.clear();
        t.valueFilters.clear();
        MarkChanged();
    }
    Ui::Tip("Unticks every bonus of this type and clears their value limits.");
    ImGui::EndDisabled();

    const std::string needle = TabletHelper::ToLowerCopy(search);
    const float rowH = ImGui::GetFrameHeight() + st.CellPadding.y * 2.0f;
    const float tableH = rowH * 12.0f + ImGui::GetTextLineHeightWithSpacing();
    const float reqW = (std::max)(ImGui::CalcTextSize(listW < ImGui::GetFontSize() * 30.0f ? "Req." : "Required").x,
                                  ImGui::GetFrameHeight());
    const float valW = (std::max)(ImGui::CalcTextSize("Max").x,
                                  ImGui::CalcTextSize("-0000").x + st.FramePadding.x * 2.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##bonuses", 4, flags, ImVec2(listW, tableH))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Bonus", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Required", ImGuiTableColumnFlags_WidthFixed, reqW);
    ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, valW);
    ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, valW);
    // Header row by hand so each header explains its column.
    static const char* const kHeaderTips[4] = {
        "Tick the bonuses to look for. Hover a name for its mod id.",
        "A required bonus counts in the \"Required bonuses\" pool instead of the optional one.",
        "Lowest roll to accept for a ticked bonus. Empty = no limit.",
        "Highest roll to accept for a ticked bonus. Empty = no limit."};
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < 4; ++c) {
        ImGui::TableSetColumnIndex(c);
        // "Required" is the widest fixed header: abbreviated on narrow panes.
        ImGui::TableHeader(c == 1 && listW < ImGui::GetFontSize() * 30.0f ? "Req.###Required"
                                                                          : ImGui::TableGetColumnName(c));
        Ui::Tip(kHeaderTips[c]);
    }

    const bool readMods = m_settings.readMods;
    const char* lastCategory = nullptr;
    int shown = 0;
    for (const auto& b : bonuses) {
        const bool sel = Contains(t.selectedBonusIds, b.Id);
        if (onlySelected && !sel) continue;
        if (!needle.empty() && !TabletHelper::ContainsCI(b.Label, needle) && !TabletHelper::ContainsCI(b.Id, needle))
            continue;
        if (!lastCategory || b.Category != lastCategory) {
            lastCategory = b.Category.c_str();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Accent);
            ImGui::TextUnformatted(b.Category.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PushID(b.Id.c_str());
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        bool on = sel;
        // The name is shortened with "..." to the column (the tooltip has it whole).
        const float nameW = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - st.ItemInnerSpacing.x;
        const std::string checkLabel = FitWithEllipsis(b.Label, nameW) + "###on";
        if (ImGui::Checkbox(checkLabel.c_str(), &on)) {
            if (on) {
                if (!Contains(t.selectedBonusIds, b.Id)) t.selectedBonusIds.push_back(b.Id);
            } else {
                RemoveFrom(t.selectedBonusIds, b.Id);
                RemoveFrom(t.requiredBonusIds, b.Id);
            }
            MarkChanged();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(b.Label.c_str());
            ImGui::TextDisabled("Mod id: %s", b.Id.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImGui::BeginDisabled(!on || !readMods);
        ImGui::TableNextColumn();
        bool req = Contains(t.requiredBonusIds, b.Id);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (reqW - ImGui::GetFrameHeight()) * 0.5f);
        if (ImGui::Checkbox("##req", &req)) {
            if (req) {
                if (!Contains(t.requiredBonusIds, b.Id)) t.requiredBonusIds.push_back(b.Id);
            } else {
                RemoveFrom(t.requiredBonusIds, b.Id);
            }
            MarkChanged();
        }
        Ui::Tip("Required: counted in the \"Required bonuses\" pool instead of the optional one.\n"
                "Use it for must-have bonuses, e.g. one required plus any two of five others.");

        const TabletHelper::ModRange* rg = m_ranges.For(b.NormId);
        if (!rg && !b.NormIdStripped.empty()) rg = m_ranges.For(b.NormIdStripped);
        if (rg && rg->min != rg->max) {
            auto& vr = t.valueFilters[b.Id];
            const int lo = rg->min < 0 ? rg->min : 0;
            const int hi = (std::max)(rg->max, 1) * 4;  // generous: a patch may raise the rolls
            const bool pct = rg->unit == "percent";
            const bool secs = rg->unit == "seconds";
            const char* unit = pct ? "%" : (secs ? " s" : "");
            char tipMin[200], tipMax[200];
            std::snprintf(tipMin, sizeof(tipMin), "Lowest value to accept. Known rolls: %d to %d%s.\nEmpty = no limit.",
                          rg->min, rg->max, unit);
            std::snprintf(tipMax, sizeof(tipMax), "Highest value to accept. Known rolls: %d to %d%s.\nEmpty = no limit.",
                          rg->min, rg->max, unit);
            ImGui::TableNextColumn();
            if (DrawValueField("##min", &vr.min, lo, hi)) MarkChanged();
            Ui::Tip(tipMin);
            ImGui::TableNextColumn();
            if (DrawValueField("##max", &vr.max, lo, hi)) MarkChanged();
            Ui::Tip(tipMax);
            if (vr.min == 0 && vr.max == 0) t.valueFilters.erase(b.Id);
        } else {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("-");
            Ui::Tip("This bonus has a single fixed value: there is nothing to limit.");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("-");
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        ++shown;
    }
    if (shown == 0) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled(onlySelected ? "No bonus selected." : "No bonus matches the search.");
    }
    ImGui::EndTable();
}

// ---------------------------------------------------------------------------
// Diagnostics (collapsed)
// ---------------------------------------------------------------------------
void TabletHelperPlugin::DrawSectionDiagnostics() {
    Ui::BeginCard("##diagnostics");
    ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Accent);
    const bool open = ImGui::TreeNodeEx(ICON_FA_STETHOSCOPE "  Diagnostics###diag", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    if (open) {
        const auto st = m_worker ? m_worker->Stats() : TabletHelper::WorkerStats{};
        char buf[160];
        const Ui::LabelColumn col({"Last read", "Modifier reads", "Remembered tablets", "Mod ranges"});
        ImGui::PushTextWrapPos(0.0f);  // values wrap inside the card on narrow panes
        col.RowNoWidth("Last read");
        std::snprintf(buf, sizeof(buf), "%.2f ms (peak %.2f ms, %llu reads, worker thread)", st.lastPassMs,
                      st.peakPassMs, static_cast<unsigned long long>(st.passes));
        ImGui::TextUnformatted(buf);
        col.RowNoWidth("Modifier reads");
        std::snprintf(buf, sizeof(buf), "%llu (%llu failed, retried after 2 s)",
                      static_cast<unsigned long long>(st.modReads), static_cast<unsigned long long>(st.modReadFailures));
        ImGui::TextUnformatted(buf);
        col.RowNoWidth("Remembered tablets");
        std::snprintf(buf, sizeof(buf), "%d", st.cachedTablets);
        ImGui::TextUnformatted(buf);
        if (st.faults > 0) {
            ImGui::SameLine();
            std::snprintf(buf, sizeof(buf), "(%llu worker faults recovered)", static_cast<unsigned long long>(st.faults));
            ImGui::TextDisabled("%s", buf);
        }
        col.RowNoWidth("Mod ranges");
        ImGui::TextUnformatted(m_ranges.Summary().c_str());
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        Ui::Hint("The modifier dump writes the raw modifiers (mod ids, stat keys, values) of every tablet "
                 "the game currently lists to config/tablet_debug_dump.txt, appending to the file. Use it to "
                 "check the bonus list and roll ranges after a game patch.");
        const auto dir = DirectoryPath() / "config";
        const auto path = dir / "tablet_debug_dump.txt";
        if (Ui::SubtleButton(ICON_FA_BUG "  Write modifier dump###dump")) {
            const std::string dump = TabletHelper::TabletWorker::BuildModDump(ctx(), 60);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::ofstream out(path, std::ios::binary | std::ios::app);
            if (out.is_open()) {
                out << "\n===== DUMP #" << ++m_dumpCount << " =====\n" << dump;
                m_diagStatus = "Appended dump #" + std::to_string(m_dumpCount) + " to " + PathToUtf8(path);
            } else {
                m_diagStatus = "Could not write " + PathToUtf8(path);
            }
        }
        Ui::Tip("Up to 60 tablets per press. Open each stash tab with tablets and press once per tab.");
        ImGui::SameLine();
        if (Ui::SubtleButton(ICON_FA_TRASH "  Clear dump file###clearDump")) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            m_dumpCount = 0;
            m_diagStatus = "Removed " + PathToUtf8(path);
        }
        if (!m_diagStatus.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", m_diagStatus.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TreePop();
    }
    Ui::EndCard();
    ImGui::Spacing();
}
