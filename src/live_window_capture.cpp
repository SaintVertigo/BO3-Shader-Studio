#include "live_window_capture.h"

#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS 1

#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <atomic>
#include <mutex>

using Microsoft::WRL::ComPtr;
using namespace winrt;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace bo3
{

void CaptureSourceLifecycle::start(const QString& title)
{
    diagnostics_ = {};
    diagnostics_.state = LiveCaptureState::Starting;
    diagnostics_.sourceTitle = title;
    diagnostics_.message = "Waiting for the first captured frame.";
}

void CaptureSourceLifecycle::acceptFrame(UINT width, UINT height)
{
    const bool resized = diagnostics_.frameAvailable &&
        (diagnostics_.width != width || diagnostics_.height != height);
    diagnostics_.state = resized ? LiveCaptureState::Resizing : LiveCaptureState::Capturing;
    diagnostics_.width = width;
    diagnostics_.height = height;
    diagnostics_.frameAvailable = width > 0 && height > 0;
    diagnostics_.message = resized ? "Capture resized; the GPU source texture was recreated."
                                   : "Live Game Capture — Display/LDR approximation";
}

void CaptureSourceLifecycle::sourceLost(const QString& reason)
{
    diagnostics_.state = LiveCaptureState::SourceLost;
    diagnostics_.message = reason;
    diagnostics_.frameAvailable = false;
    diagnostics_.width = 0;
    diagnostics_.height = 0;
}

void CaptureSourceLifecycle::stalled(const QString& reason)
{
    diagnostics_.state = LiveCaptureState::Stalled;
    diagnostics_.message = reason;
}

void CaptureSourceLifecycle::fail(const QString& reason)
{
    diagnostics_.state = LiveCaptureState::Error;
    diagnostics_.message = reason;
    diagnostics_.frameAvailable = false;
}

void CaptureSourceLifecycle::setPerformance(float captureFps, float approximateLatencyMs)
{
    diagnostics_.captureFps = captureFps;
    diagnostics_.approximateLatencyMs = approximateLatencyMs;
}

void CaptureSourceLifecycle::stop()
{
    diagnostics_ = {};
    diagnostics_.state = LiveCaptureState::Stopped;
    diagnostics_.message = "Capture stopped.";
}

struct LiveWindowCapture::Impl
{
    ComPtr<ID3D11Device> d3dDevice;
    wgd3d::IDirect3DDevice direct3dDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool framePool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    event_token frameToken{};
    event_token closedToken{};
    std::mutex frameMutex;
    wgc::Direct3D11CaptureFrame latestFrame{nullptr};
    ComPtr<ID3D11Texture2D> copyTexture;
    ComPtr<ID3D11ShaderResourceView> copySrv;
    CaptureSourceLifecycle lifecycle;
    std::atomic<bool> sourceClosed{false};
    std::atomic<bool> running{false};
    std::atomic<uint64_t> queuedFrames{0};
    std::chrono::steady_clock::time_point lastFrameTime{};
    std::chrono::steady_clock::time_point latestArrival{};
    std::chrono::steady_clock::time_point fpsStart{};
    uint64_t consumedFrames = 0;
    UINT poolWidth = 0;
    UINT poolHeight = 0;

    void closeLatestFrame()
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if(latestFrame) latestFrame.Close();
        latestFrame = nullptr;
    }

    void clearGpuCopy()
    {
        copySrv.Reset();
        copyTexture.Reset();
        poolWidth = 0;
        poolHeight = 0;
    }

    void releaseCaptureObjects()
    {
        running = false;
        try
        {
            if(framePool && frameToken.value) framePool.FrameArrived(frameToken);
            if(item && closedToken.value) item.Closed(closedToken);
            if(session) session.Close();
            if(framePool) framePool.Close();
        }
        catch(...) {}
        closeLatestFrame();
        session = nullptr;
        framePool = nullptr;
        item = nullptr;
        direct3dDevice = nullptr;
        d3dDevice.Reset();
        sourceClosed = false;
        frameToken = {};
        closedToken = {};
    }
};

LiveWindowCapture::LiveWindowCapture() : impl_(std::make_shared<Impl>()) {}
LiveWindowCapture::~LiveWindowCapture() { stop(); }

bool LiveWindowCapture::isSupported(QString* reason)
{
    try
    {
        const bool supported = wgc::GraphicsCaptureSession::IsSupported();
        if(!supported && reason)
            *reason = "Windows Graphics Capture is unavailable on this Windows version/session.";
        return supported;
    }
    catch(const hresult_error& error)
    {
        if(reason) *reason = QString::fromWCharArray(error.message().c_str());
        return false;
    }
}

QVector<CaptureWindowTarget> LiveWindowCapture::visibleWindows(HWND excludedWindow)
{
    struct Context
    {
        HWND excluded = nullptr;
        QVector<CaptureWindowTarget> windows;
    } context{excludedWindow, {}};

    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL
    {
        auto* context = reinterpret_cast<Context*>(value);
        if(hwnd == context->excluded || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        const int length = GetWindowTextLengthW(hwnd);
        if(length <= 0) return TRUE;
        std::wstring title(static_cast<size_t>(length + 1), L'\0');
        GetWindowTextW(hwnd, title.data(), length + 1);
        title.resize(wcslen(title.c_str()));
        const QString text = QString::fromStdWString(title).trimmed();
        if(text.isEmpty()) return TRUE;
        context->windows.push_back({hwnd, text});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));

    std::sort(context.windows.begin(), context.windows.end(),
              [](const CaptureWindowTarget& a, const CaptureWindowTarget& b)
              { return a.title.compare(b.title, Qt::CaseInsensitive) < 0; });
    return context.windows;
}

bool LiveWindowCapture::start(HWND target, ID3D11Device* device, QString& error)
{
    stop();
    error.clear();
    if(!target || !IsWindow(target) || !device)
    {
        error = "The selected window or D3D11 device is no longer available.";
        return false;
    }
    QString supportReason;
    if(!isSupported(&supportReason))
    {
        error = supportReason;
        return false;
    }

    try
    {
        impl_->d3dDevice = device;
        ComPtr<IDXGIDevice> dxgiDevice;
        check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())));
        com_ptr<IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
        impl_->direct3dDevice = inspectable.as<wgd3d::IDirect3DDevice>();

        const auto factory = get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        check_hresult(factory->CreateForWindow(
            target, guid_of<wgc::GraphicsCaptureItem>(), put_abi(impl_->item)));
        const auto size = impl_->item.Size();
        if(size.Width <= 0 || size.Height <= 0)
        {
            error = "The selected window has no capturable client area. Restore it and retry.";
            stop();
            return false;
        }

        impl_->poolWidth = static_cast<UINT>(size.Width);
        impl_->poolHeight = static_cast<UINT>(size.Height);
        impl_->framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            impl_->direct3dDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        impl_->session = impl_->framePool.CreateCaptureSession(impl_->item);
        impl_->session.IsCursorCaptureEnabled(true);

        wchar_t title[512]{};
        GetWindowTextW(target, title, static_cast<int>(std::size(title)));
        impl_->lifecycle.start(QString::fromWCharArray(title));
        impl_->sourceClosed = false;
        impl_->running = true;
        impl_->queuedFrames = 0;
        impl_->consumedFrames = 0;
        impl_->fpsStart = std::chrono::steady_clock::now();
        impl_->lastFrameTime = impl_->fpsStart;

        impl_->frameToken = impl_->framePool.FrameArrived([weakState = std::weak_ptr<Impl>(impl_)](
            const wgc::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&)
        {
            try
            {
                const auto state = weakState.lock();
                if(!state || !state->running) return;
                wgc::Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
                if(!frame) return;
                std::lock_guard<std::mutex> lock(state->frameMutex);
                if(!state->running)
                {
                    frame.Close();
                    return;
                }
                if(state->latestFrame) state->latestFrame.Close();
                state->latestFrame = frame;
                state->latestArrival = std::chrono::steady_clock::now();
                state->queuedFrames.fetch_add(1, std::memory_order_relaxed);
            }
            catch(...) {}
        });
        impl_->closedToken = impl_->item.Closed([weakState = std::weak_ptr<Impl>(impl_)](
            const wgc::GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&)
        {
            const auto state = weakState.lock();
            if(state && state->running) state->sourceClosed = true;
        });
        impl_->session.StartCapture();
        return true;
    }
    catch(const hresult_error& captureError)
    {
        error = QString("Windows Graphics Capture could not start: %1\n\nBorderless/windowed mode is generally more reliable than exclusive fullscreen.")
            .arg(QString::fromWCharArray(captureError.message().c_str()));
        stop();
        return false;
    }
}

void LiveWindowCapture::stop()
{
    if(!impl_) return;
    impl_->releaseCaptureObjects();
    impl_->clearGpuCopy();
    impl_->lifecycle.stop();
}

bool LiveWindowCapture::update(ID3D11DeviceContext* context, QString* notification)
{
    if(notification) notification->clear();
    if(!impl_->running || !context) return false;
    if(impl_->sourceClosed)
    {
        impl_->lifecycle.sourceLost("The captured window closed or revoked capture access.");
        impl_->releaseCaptureObjects();
        impl_->clearGpuCopy();
        if(notification) *notification = impl_->lifecycle.diagnostics().message;
        return false;
    }

    wgc::Direct3D11CaptureFrame frame{nullptr};
    std::chrono::steady_clock::time_point arrival{};
    {
        std::lock_guard<std::mutex> lock(impl_->frameMutex);
        frame = impl_->latestFrame;
        arrival = impl_->latestArrival;
        impl_->latestFrame = nullptr;
    }
    if(!frame)
    {
        const auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration<float>(now - impl_->lastFrameTime).count() >= 1.0f)
            impl_->lifecycle.stalled(
                "No new capture frame has arrived. The source window may be minimized, occluded by an exclusive-fullscreen transition, or temporarily unavailable.");
        return false;
    }

    try
    {
        const auto contentSize = frame.ContentSize();
        if(contentSize.Width <= 0 || contentSize.Height <= 0)
        {
            frame.Close();
            return false;
        }
        const auto access = frame.Surface().as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> frameTexture;
        check_hresult(access->GetInterface(IID_PPV_ARGS(frameTexture.GetAddressOf())));
        D3D11_TEXTURE2D_DESC frameDesc{};
        frameTexture->GetDesc(&frameDesc);
        const UINT width = static_cast<UINT>(contentSize.Width);
        const UINT height = static_cast<UINT>(contentSize.Height);

        bool recreated = false;
        if(!impl_->copyTexture || width != impl_->poolWidth || height != impl_->poolHeight)
        {
            impl_->copySrv.Reset();
            impl_->copyTexture.Reset();
            D3D11_TEXTURE2D_DESC copyDesc{};
            copyDesc.Width = width;
            copyDesc.Height = height;
            copyDesc.MipLevels = 1;
            copyDesc.ArraySize = 1;
            copyDesc.Format = frameDesc.Format;
            copyDesc.SampleDesc.Count = 1;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            check_hresult(impl_->d3dDevice->CreateTexture2D(
                &copyDesc, nullptr, impl_->copyTexture.GetAddressOf()));
            check_hresult(impl_->d3dDevice->CreateShaderResourceView(
                impl_->copyTexture.Get(), nullptr, impl_->copySrv.GetAddressOf()));
            impl_->poolWidth = width;
            impl_->poolHeight = height;
            recreated = true;
        }

        D3D11_BOX sourceBox{};
        sourceBox.right = std::min(width, frameDesc.Width);
        sourceBox.bottom = std::min(height, frameDesc.Height);
        sourceBox.back = 1;
        context->CopySubresourceRegion(impl_->copyTexture.Get(), 0, 0, 0, 0,
                                       frameTexture.Get(), 0, &sourceBox);
        frame.Close();

        if(recreated && impl_->framePool)
        {
            impl_->framePool.Recreate(
                impl_->direct3dDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2, {static_cast<int32_t>(width), static_cast<int32_t>(height)});
        }

        impl_->lifecycle.acceptFrame(width, height);
        ++impl_->consumedFrames;
        const auto now = std::chrono::steady_clock::now();
        impl_->lastFrameTime = now;
        const float seconds = std::chrono::duration<float>(now - impl_->fpsStart).count();
        float fps = impl_->lifecycle.diagnostics().captureFps;
        if(seconds >= 0.5f)
        {
            fps = static_cast<float>(impl_->consumedFrames) / seconds;
            impl_->consumedFrames = 0;
            impl_->fpsStart = now;
        }
        const float latencyMs = arrival.time_since_epoch().count() == 0 ? 0.0f :
            std::chrono::duration<float, std::milli>(now - arrival).count();
        impl_->lifecycle.setPerformance(fps, latencyMs);
        if(notification && recreated)
            *notification = QString("Live capture resized to %1 × %2.").arg(width).arg(height);
        return true;
    }
    catch(const hresult_error& captureError)
    {
        if(frame) frame.Close();
        const QString message = QString("Live capture frame failed: %1")
            .arg(QString::fromWCharArray(captureError.message().c_str()));
        impl_->lifecycle.fail(message);
        impl_->releaseCaptureObjects();
        impl_->clearGpuCopy();
        if(notification) *notification = message;
        return false;
    }
}

ComPtr<ID3D11ShaderResourceView> LiveWindowCapture::shaderResourceView() const
{
    return impl_->copySrv;
}

LiveCaptureDiagnostics LiveWindowCapture::diagnostics() const
{
    return impl_->lifecycle.diagnostics();
}

bool LiveWindowCapture::active() const
{
    return impl_->running;
}

QString toString(LiveCaptureState state)
{
    switch(state)
    {
        case LiveCaptureState::Stopped: return "STOPPED";
        case LiveCaptureState::Starting: return "STARTING";
        case LiveCaptureState::Capturing: return "LIVE";
        case LiveCaptureState::Resizing: return "RESIZING";
        case LiveCaptureState::Stalled: return "STALLED";
        case LiveCaptureState::SourceLost: return "SOURCE LOST";
        case LiveCaptureState::Error: return "ERROR";
    }
    return "STOPPED";
}

} // namespace bo3
