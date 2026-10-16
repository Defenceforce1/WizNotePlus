// Read-only version query. These are documented exported free-function
// signatures from qtwebenginecoreglobal.h, not reconstructed class layouts.
#include <QtCore/qglobal.h>
#include <cstdio>

extern const char* qWebEngineVersion() noexcept;
extern const char* qWebEngineChromiumVersion() noexcept;
extern const char* qWebEngineChromiumSecurityPatchVersion() noexcept;

int main()
{
    std::printf("Qt Core runtime: %s\nQt WebEngine runtime: %s\nChromium baseline: %s\nChromium security patch baseline: %s\n",
        qVersion(), qWebEngineVersion(), qWebEngineChromiumVersion(), qWebEngineChromiumSecurityPatchVersion());
}
