#include <QQuickWindow>
#include <QThread>
#include <rhi/qrhi.h>

// Optional Qt 6/macOS diagnostic, isolated in its own target because QRhi has
// limited compatibility guarantees. Never use this in normal note/tab cleanup.
bool wizNativeMemoryRhiControl(QQuickWindow* window, bool finish)
{
    if (!window || QThread::currentThread() != window->thread()) return false;
    auto* rhi = window->rhi();
    if (!rhi || rhi->backend() != QRhi::Metal || rhi->isRecordingFrame()) return false;
    if (finish) return rhi->finish() == QRhi::FrameOpSuccess;
    rhi->releaseCachedResources();
    return true;
}
