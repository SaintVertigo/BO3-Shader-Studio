#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QString>
#include <QVector>

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <memory>

namespace bo3
{

enum class LiveCaptureState
{
    Stopped,
    Starting,
    Capturing,
    Resizing,
    Stalled,
    SourceLost,
    Error
};

struct CaptureWindowTarget
{
    HWND hwnd = nullptr;
    QString title;
};

struct LiveCaptureDiagnostics
{
    LiveCaptureState state = LiveCaptureState::Stopped;
    QString sourceTitle;
    QString message;
    UINT width = 0;
    UINT height = 0;
    float captureFps = 0.0f;
    float approximateLatencyMs = 0.0f;
    bool frameAvailable = false;
    bool ldrDisplayApproximation = true;
};

class CaptureSourceLifecycle
{
public:
    void start(const QString& title);
    void acceptFrame(UINT width, UINT height);
    void stalled(const QString& reason);
    void sourceLost(const QString& reason);
    void fail(const QString& reason);
    void setPerformance(float captureFps, float approximateLatencyMs);
    void stop();

    LiveCaptureDiagnostics diagnostics() const { return diagnostics_; }

private:
    LiveCaptureDiagnostics diagnostics_;
};

class LiveWindowCapture
{
public:
    LiveWindowCapture();
    ~LiveWindowCapture();
    LiveWindowCapture(const LiveWindowCapture&) = delete;
    LiveWindowCapture& operator=(const LiveWindowCapture&) = delete;

    static bool isSupported(QString* reason = nullptr);
    static QVector<CaptureWindowTarget> visibleWindows(HWND excludedWindow = nullptr);

    bool start(HWND target, ID3D11Device* device, QString& error);
    void stop();
    bool update(ID3D11DeviceContext* context, QString* notification = nullptr);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView() const;
    LiveCaptureDiagnostics diagnostics() const;
    bool active() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

QString toString(LiveCaptureState state);

} // namespace bo3
