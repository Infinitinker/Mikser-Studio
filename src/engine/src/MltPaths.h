// Finds the folders of MLT's modules and data: an environment override, next to the executable, the build-time install.
#pragma once

#include <QString>

#include <expected>

namespace mikserstudio::engine {

// A folder MLT should use and how it was found ("MIKSERSTUDIO_MLT_REPOSITORY", "next to the
// executable", "build-time install", "MLT default"). An empty path means MLT's own default.
struct MltLocation {
    QString path;
    QString source;
};

// See MltRuntime.h for the lookup order. Fails only when an environment override points to a
// folder without MLT modules/data, since that is a setup mistake the user must fix.
std::expected<MltLocation, QString> findMltRepository();
std::expected<MltLocation, QString> findMltData();

}  // namespace mikserstudio::engine
