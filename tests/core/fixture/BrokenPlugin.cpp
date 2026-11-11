// Libraries that look like plugins but are not usable, for the PluginManager error paths.
// Built twice: with MIKSERSTUDIO_FIXTURE_NO_ENTRY it does not export mikserstudio_plugin_v1(); with
// MIKSERSTUDIO_FIXTURE_WRONG_API it exports a table for a future API version.

#include <mikserstudio/plugin_api.h>

namespace {

int activate(const MikserStudioHostApiV1*, void**, const MikserStudioStringSink*)
{
    return 0;
}

void deactivate(void*) {}

constexpr MikserStudioPluginV1 kPlugin{sizeof(MikserStudioPluginV1), MIKSERSTUDIO_PLUGIN_API_VERSION + 98u, &activate, &deactivate};

}  // namespace

#if defined(MIKSERSTUDIO_FIXTURE_NO_ENTRY)
extern "C" MIKSERSTUDIO_PLUGIN_EXPORT const MikserStudioPluginV1* not_a_mikserstudio_plugin(void)
{
    return &kPlugin;
}
#elif defined(MIKSERSTUDIO_FIXTURE_WRONG_API)
extern "C" MIKSERSTUDIO_PLUGIN_EXPORT const MikserStudioPluginV1* mikserstudio_plugin_v1(void)
{
    return &kPlugin;
}
#else
#error "Define MIKSERSTUDIO_FIXTURE_NO_ENTRY or MIKSERSTUDIO_FIXTURE_WRONG_API"
#endif
