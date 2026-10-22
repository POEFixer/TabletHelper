// Tablet Helper - POEFixer plugin (SDK v6).
// Originally created by Omer Faruk ARPA (GPL-3.0); maintained by POEFixer.

#include "TabletHelperPlugin.h"

#include "../../../POEFixer/imgui/imgui.h"

// Plugin info: the host checks it before CreatePlugin; the publish tool reads it.
POEFIXER_PLUGIN_INFO(
    "TabletHelper",
    "Tablet Helper",
    "1.4.3",
    "Omer Faruk ARPA, POEFixer",
    "Highlights Precursor Tablets in your inventory, stash, guild stash and merchant windows, coloured by type and filtered by bonuses, roll values and uses left.")

TabletHelperPlugin::~TabletHelperPlugin() {
    // The host normally calls OnDisable first; this covers an unload without it.
    if (m_worker) m_worker->Stop();
}

void TabletHelperPlugin::OnEnable(bool /*isGameAttached*/) {
    if (!HostCompatible()) {
        ctx()->Log.Error("Tablet Helper: this POEFixer version is not compatible with the plugin; it stays inactive.");
        return;
    }
    if (ctx()->ImGuiContext) ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

    if (!m_settings.Load(DirectoryPath()))
        ctx()->Log.Warn("Tablet Helper: config/settings.json could not be read; defaults are used (a copy was kept as settings.json.corrupt).");
    m_loaded = true;
    ctx()->Log.Info(m_ranges.Load(DirectoryPath()).c_str());

    if (!m_worker) m_worker = std::make_unique<TabletHelper::TabletWorker>(ctx());
    PushFilter();
    m_worker->Start();
    m_renderer.Reset();
    m_running = true;
}

void TabletHelperPlugin::OnDisable() {
    m_running = false;
    if (m_worker) m_worker->Stop();
    m_renderer.Reset();
    SaveIfDue(true);
}

void TabletHelperPlugin::SaveSettings() { SaveIfDue(true); }

void TabletHelperPlugin::MarkChanged() {
    m_filterDirty = true;
    m_saveDirty = true;
    m_saveDue = Clock::now() + std::chrono::seconds(1);
}

void TabletHelperPlugin::PushFilter() {
    m_filterDirty = false;
    if (!m_worker) return;
    auto f = std::make_shared<TabletHelper::FilterConfig>();
    f->readMods = m_settings.readMods;
    f->types = m_settings.ActiveTypes();
    m_worker->SetFilter(std::move(f), m_settings.scanIntervalMs);
}

void TabletHelperPlugin::SaveIfDue(bool force) {
    if (!m_loaded || !m_saveDirty) return;
    if (!force && Clock::now() < m_saveDue) return;
    m_saveDirty = false;
    if (!m_settings.Save(DirectoryPath()))
        ctx()->Log.Warn("Tablet Helper: could not write config/settings.json.");
}

void TabletHelperPlugin::DrawUI() {
    if (!m_running || !m_worker || !ctx()->ImGuiContext) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    if (m_filterDirty) PushFilter();
    SaveIfDue(false);

    // Cheap host scalars only; the worker does the reading.
    const bool active = ctx()->Game.IsInGame() && ctx()->Game.IsForeground();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const uint64_t area = ctx()->Game.GetAreaChangeCounter();
    m_worker->Heartbeat(active, display.x, display.y, area);
    if (!active) return;

    const auto frame = m_worker->Latest();
    if (!frame || frame->areaCounter != area || frame->items.empty()) return;
    // Over the game normally; behind the POEFixer window while it is open, so the
    // highlights never cover the menu.
    ImDrawList* dl = ctx()->Game.IsMenuVisible() ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
    m_renderer.Draw(dl, frame, m_settings);
}

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() { return new TabletHelperPlugin(); }
extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* p) { delete p; }
