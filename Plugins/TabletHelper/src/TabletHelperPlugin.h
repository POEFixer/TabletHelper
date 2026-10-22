#pragma once

// Tablet Helper: highlights Precursor Tablets in every open item window
// (inventory, stash tabs, the Tablet stash, guild stash, merchant), coloured per
// type and filtered by bonuses, roll values and uses left.
//
// Threads: the render thread owns the settings, the settings tab and drawing;
// TabletWorker reads the game and publishes immutable frames.

#include "HighlightRenderer.h"
#include "Settings.h"
#include "TabletRanges.h"
#include "TabletWorker.h"
#include "../../../POEFixer/plugin_sdk/PluginSDK.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

class TabletHelperPlugin : public PluginSDK::Plugin {
public:
    TabletHelperPlugin() = default;
    ~TabletHelperPlugin() override;

    const char* GetName() const override { return "Tablet Helper"; }
    bool WantsOverlay() const override { return true; }
    void OnEnable(bool isGameAttached) override;
    void OnDisable() override;
    void DrawUI() override;
    void DrawSettings() override;
    void SaveSettings() override;

private:
    using Clock = std::chrono::steady_clock;

    // State changes from the settings tab: re-send the rules to the worker and
    // save about a second after the last edit.
    void MarkChanged();
    void PushFilter();
    void SaveIfDue(bool force);

    // Settings tab, one function per card (src/ui/SettingsUi.cpp).
    void DrawSectionStatus();
    void DrawSectionHighlight();
    void DrawSectionLabels();
    void DrawSectionDetection();
    void DrawSectionProfiles();
    void DrawSectionTypes();
    void DrawTypeEditor(TabletHelperConfig::TypeConfig& t);
    void DrawBonusTable(TabletHelperConfig::TypeConfig& t);
    bool DrawValueField(const char* id, int* v, int lo, int hi);
    void DrawSectionDiagnostics();

    TabletHelperConfig::Settings m_settings;
    TabletHelper::TabletRanges m_ranges;
    std::unique_ptr<TabletHelper::TabletWorker> m_worker;
    TabletHelper::HighlightRenderer m_renderer;
    bool m_running = false;
    bool m_loaded = false;
    bool m_filterDirty = false;
    bool m_saveDirty = false;
    Clock::time_point m_saveDue{};

    // Settings-tab only state (not persisted).
    std::unordered_map<std::string, std::string> m_bonusSearch;  // per type key
    std::unordered_map<std::string, bool> m_onlySelected;        // per type key
    std::string m_openType;                                      // the type whose editor is open
    std::string m_diagStatus;
    int m_dumpCount = 0;
};
