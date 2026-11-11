#include "MltLog.h"

#include "Logging.h"

#include <framework/mlt.h>  // Declares the C API with C linkage.

#include <QString>

#include <array>
#include <cstdarg>
#include <cstdio>

namespace mikserstudio::engine {

namespace {

QString serviceName(void* service)
{
    if (!service)
        return {};
    const char* name = mlt_properties_get(MLT_SERVICE_PROPERTIES(static_cast<mlt_service>(service)), "mlt_service");
    return name ? QString::fromUtf8(name) : QString();
}

void logCallback(void* service, int level, const char* format, va_list args)
{
    if (level > mlt_log_get_level())
        return;
    std::array<char, 1024> buffer{};
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    QString message = QString::fromUtf8(buffer.data()).trimmed();
    if (message.isEmpty())
        return;
    if (const QString name = serviceName(service); !name.isEmpty())
        message = QStringLiteral("[%1] %2").arg(name, message);

    if (level <= MLT_LOG_ERROR)
        qCWarning(lcMlt).noquote() << message;
    else if (level <= MLT_LOG_WARNING)
        qCInfo(lcMlt).noquote() << message;
    else
        qCDebug(lcMlt).noquote() << message;
}

}  // namespace

void installMltLogHandler()
{
    mlt_log_set_level(lcMlt().isDebugEnabled() ? MLT_LOG_VERBOSE : MLT_LOG_WARNING);
    mlt_log_set_callback(logCallback);
}

}  // namespace mikserstudio::engine
