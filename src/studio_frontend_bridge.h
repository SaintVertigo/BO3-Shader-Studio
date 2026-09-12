#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

class StudioFrontendBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow* hostWindow READ hostWindow NOTIFY windowsChanged)
    Q_PROPERTY(QWindow* previewWindow READ previewWindow NOTIFY windowsChanged)
    Q_PROPERTY(QWindow* advancedEditorWindow READ advancedEditorWindow NOTIFY windowsChanged)
    Q_PROPERTY(bool beginnerMode READ beginnerMode NOTIFY stateChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString displayVersion READ displayVersion NOTIFY stateChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowColor READ windowColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor baseColor READ baseColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonColor READ buttonColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor mutedColor READ mutedColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY paletteChanged)
    Q_PROPERTY(int target READ target NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString targetDescription READ targetDescription NOTIFY projectChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY projectChanged)
    Q_PROPERTY(QString selectedPresetId READ selectedPresetId NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap baseSettings READ baseSettings NOTIFY projectChanged)
    Q_PROPERTY(QVariantList effectCatalog READ effectCatalog NOTIFY projectChanged)
    Q_PROPERTY(QVariantList activeEffects READ activeEffects NOTIFY projectChanged)
    Q_PROPERTY(int selectedEffectIndex READ selectedEffectIndex NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap selectedEffect READ selectedEffect NOTIFY projectChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    explicit StudioFrontendBridge(QObject* parent = nullptr);

    QWindow* hostWindow() const { return hostWindow_; }
    QWindow* previewWindow() const { return previewWindow_; }
    QWindow* advancedEditorWindow() const { return advancedEditorWindow_; }
    bool beginnerMode() const { return beginnerMode_; }
    bool animationsEnabled() const { return animationsEnabled_; }
    const QString& displayVersion() const { return displayVersion_; }
    const QString& themeName() const { return themeName_; }
    QColor windowColor() const { return windowColor_; }
    QColor panelColor() const { return panelColor_; }
    QColor baseColor() const { return baseColor_; }
    QColor buttonColor() const { return buttonColor_; }
    QColor textColor() const { return textColor_; }
    QColor mutedColor() const { return mutedColor_; }
    QColor accentColor() const { return accentColor_; }
    int target() const { return target_; }
    const QString& projectName() const { return projectName_; }
    const QString& targetDescription() const { return targetDescription_; }
    const QVariantList& presets() const { return presets_; }
    const QString& selectedPresetId() const { return selectedPresetId_; }
    const QVariantMap& baseSettings() const { return baseSettings_; }
    const QVariantList& effectCatalog() const { return effectCatalog_; }
    const QVariantList& activeEffects() const { return activeEffects_; }
    int selectedEffectIndex() const { return selectedEffectIndex_; }
    const QVariantMap& selectedEffect() const { return selectedEffect_; }
    const QString& statusText() const { return statusText_; }

    void setNativeWindows(QWindow* hostWindow, QWindow* previewWindow, QWindow* advancedEditorWindow);
    void setUiState(bool beginnerMode, bool animationsEnabled, const QString& displayVersion);
    void setPaletteState(const QString& themeName, const QColor& windowColor,
                         const QColor& panelColor, const QColor& baseColor,
                         const QColor& buttonColor, const QColor& textColor,
                         const QColor& mutedColor, const QColor& accentColor);
    void setProjectState(int target, const QString& projectName, const QString& targetDescription,
                         const QVariantList& presets, const QString& selectedPresetId,
                         const QVariantMap& baseSettings, const QVariantList& effectCatalog,
                         const QVariantList& activeEffects, int selectedEffectIndex,
                         const QVariantMap& selectedEffect);
    void setStatusText(const QString& text);

    Q_INVOKABLE void requestMenu(const QString& menuName);
    Q_INVOKABLE void requestOpen();
    Q_INVOKABLE void requestSave();
    Q_INVOKABLE void requestPreview();
    Q_INVOKABLE void requestExport();
    Q_INVOKABLE void requestMode(bool beginner);
    Q_INVOKABLE void requestTarget(int target);
    Q_INVOKABLE void requestProjectName(const QString& name);
    Q_INVOKABLE void requestApplyPreset(const QString& presetId);
    Q_INVOKABLE void requestBaseColor(const QString& key, const QColor& color);
    Q_INVOKABLE void requestAddEffect(const QString& typeId);
    Q_INVOKABLE void requestSelectEffect(int index);
    Q_INVOKABLE void requestRemoveEffect(int index);
    Q_INVOKABLE void requestMoveEffect(int index, int delta);
    Q_INVOKABLE void requestEffectEnabled(int index, bool enabled);
    Q_INVOKABLE void requestParameterValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void requestParameterColor(const QString& key, const QColor& color);
    Q_INVOKABLE void requestResetPreview();
    Q_INVOKABLE void requestPreviewSettings();
    Q_INVOKABLE void requestFullPreview();
    Q_INVOKABLE void requestCamera3D(bool enabled);
    Q_INVOKABLE void requestMesh(int index);
    Q_INVOKABLE void requestBrowseEffects();
    Q_INVOKABLE void requestTutorialComplete();

    void showGettingStarted();
    void showEffectBrowser();

signals:
    void windowsChanged();
    void stateChanged();
    void paletteChanged();
    void projectChanged();
    void statusChanged();

    void menuRequested(const QString& menuName);
    void openRequested();
    void saveRequested();
    void previewRequested();
    void exportRequested();
    void modeRequested(bool beginner);
    void targetRequested(int target);
    void projectNameRequested(const QString& name);
    void applyPresetRequested(const QString& presetId);
    void baseColorRequested(const QString& key, const QColor& color);
    void addEffectRequested(const QString& typeId);
    void selectEffectRequested(int index);
    void removeEffectRequested(int index);
    void moveEffectRequested(int index, int delta);
    void effectEnabledRequested(int index, bool enabled);
    void parameterValueRequested(const QString& key, const QVariant& value);
    void parameterColorRequested(const QString& key, const QColor& color);
    void resetPreviewRequested();
    void previewSettingsRequested();
    void fullPreviewRequested();
    void camera3DRequested(bool enabled);
    void meshRequested(int index);
    void browseEffectsRequested();
    void tutorialCompleteRequested();
    void gettingStartedRequested();
    void effectBrowserOpenRequested();

private:
    QWindow* hostWindow_ = nullptr;
    QWindow* previewWindow_ = nullptr;
    QWindow* advancedEditorWindow_ = nullptr;
    bool beginnerMode_ = true;
    bool animationsEnabled_ = true;
    QString displayVersion_ = QStringLiteral("0.3");
    QString themeName_ = QStringLiteral("BO3 Dark");
    QColor windowColor_ = QColor(QStringLiteral("#12151A"));
    QColor panelColor_ = QColor(QStringLiteral("#171B21"));
    QColor baseColor_ = QColor(QStringLiteral("#0D1014"));
    QColor buttonColor_ = QColor(QStringLiteral("#242A32"));
    QColor textColor_ = QColor(QStringLiteral("#E3E8EF"));
    QColor mutedColor_ = QColor(QStringLiteral("#9DA8B5"));
    QColor accentColor_ = QColor(QStringLiteral("#4D8FCC"));
    int target_ = 0;
    QString projectName_ = QStringLiteral("New Screen Effect");
    QString targetDescription_;
    QVariantList presets_;
    QString selectedPresetId_ = QStringLiteral("blank");
    QVariantMap baseSettings_;
    QVariantList effectCatalog_;
    QVariantList activeEffects_;
    int selectedEffectIndex_ = -1;
    QVariantMap selectedEffect_;
    QString statusText_;
};
