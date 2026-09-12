#include "studio_frontend_bridge.h"

StudioFrontendBridge::StudioFrontendBridge(QObject* parent) : QObject(parent) {}

void StudioFrontendBridge::setNativeWindows(QWindow* hostWindow, QWindow* previewWindow, QWindow* advancedEditorWindow)
{
    if(hostWindow_ == hostWindow && previewWindow_ == previewWindow && advancedEditorWindow_ == advancedEditorWindow) return;
    hostWindow_ = hostWindow;
    previewWindow_ = previewWindow;
    advancedEditorWindow_ = advancedEditorWindow;
    emit windowsChanged();
}

void StudioFrontendBridge::setUiState(bool beginnerMode, bool animationsEnabled, const QString& displayVersion)
{
    const bool changed = beginnerMode_ != beginnerMode || animationsEnabled_ != animationsEnabled || displayVersion_ != displayVersion;
    beginnerMode_ = beginnerMode;
    animationsEnabled_ = animationsEnabled;
    displayVersion_ = displayVersion;
    if(changed) emit stateChanged();
}

void StudioFrontendBridge::setPaletteState(const QString& themeName, const QColor& windowColor,
                                           const QColor& panelColor, const QColor& baseColor,
                                           const QColor& buttonColor, const QColor& textColor,
                                           const QColor& mutedColor, const QColor& accentColor)
{
    themeName_ = themeName;
    windowColor_ = windowColor;
    panelColor_ = panelColor;
    baseColor_ = baseColor;
    buttonColor_ = buttonColor;
    textColor_ = textColor;
    mutedColor_ = mutedColor;
    accentColor_ = accentColor;
    emit paletteChanged();
}

void StudioFrontendBridge::setProjectState(int target, const QString& projectName, const QString& targetDescription,
                                           const QVariantList& presets, const QString& selectedPresetId,
                                           const QVariantMap& baseSettings, const QVariantList& effectCatalog,
                                           const QVariantList& activeEffects, int selectedEffectIndex,
                                           const QVariantMap& selectedEffect)
{
    target_ = target;
    projectName_ = projectName;
    targetDescription_ = targetDescription;
    presets_ = presets;
    selectedPresetId_ = selectedPresetId;
    baseSettings_ = baseSettings;
    effectCatalog_ = effectCatalog;
    activeEffects_ = activeEffects;
    selectedEffectIndex_ = selectedEffectIndex;
    selectedEffect_ = selectedEffect;
    emit projectChanged();
}

void StudioFrontendBridge::setStatusText(const QString& text)
{
    if(statusText_ == text) return;
    statusText_ = text;
    emit statusChanged();
}

void StudioFrontendBridge::requestMenu(const QString& menuName) { emit menuRequested(menuName); }
void StudioFrontendBridge::requestOpen() { emit openRequested(); }
void StudioFrontendBridge::requestSave() { emit saveRequested(); }
void StudioFrontendBridge::requestPreview() { emit previewRequested(); }
void StudioFrontendBridge::requestExport() { emit exportRequested(); }
void StudioFrontendBridge::requestMode(bool beginner) { emit modeRequested(beginner); }
void StudioFrontendBridge::requestTarget(int target) { emit targetRequested(target); }
void StudioFrontendBridge::requestProjectName(const QString& name) { emit projectNameRequested(name); }
void StudioFrontendBridge::requestApplyPreset(const QString& presetId) { emit applyPresetRequested(presetId); }
void StudioFrontendBridge::requestBaseColor(const QString& key, const QColor& color) { emit baseColorRequested(key, color); }
void StudioFrontendBridge::requestAddEffect(const QString& typeId) { emit addEffectRequested(typeId); }
void StudioFrontendBridge::requestSelectEffect(int index) { emit selectEffectRequested(index); }
void StudioFrontendBridge::requestRemoveEffect(int index) { emit removeEffectRequested(index); }
void StudioFrontendBridge::requestMoveEffect(int index, int delta) { emit moveEffectRequested(index, delta); }
void StudioFrontendBridge::requestEffectEnabled(int index, bool enabled) { emit effectEnabledRequested(index, enabled); }
void StudioFrontendBridge::requestParameterValue(const QString& key, const QVariant& value) { emit parameterValueRequested(key, value); }
void StudioFrontendBridge::requestParameterColor(const QString& key, const QColor& color) { emit parameterColorRequested(key, color); }
void StudioFrontendBridge::requestResetPreview() { emit resetPreviewRequested(); }
void StudioFrontendBridge::requestPreviewSettings() { emit previewSettingsRequested(); }
void StudioFrontendBridge::requestFullPreview() { emit fullPreviewRequested(); }
void StudioFrontendBridge::requestCamera3D(bool enabled) { emit camera3DRequested(enabled); }
void StudioFrontendBridge::requestMesh(int index) { emit meshRequested(index); }
void StudioFrontendBridge::requestBrowseEffects() { emit browseEffectsRequested(); }
void StudioFrontendBridge::requestTutorialComplete() { emit tutorialCompleteRequested(); }
void StudioFrontendBridge::showGettingStarted() { emit gettingStartedRequested(); }
void StudioFrontendBridge::showEffectBrowser() { emit effectBrowserOpenRequested(); }
