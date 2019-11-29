#include <BetteRCon/Plugin.h>

// Sample Plugin Definition
class SamplePlugin : public BetteRCon::Plugin
{
public:
	virtual std::string_view GetPluginAuthor() { return "MrElectrify"; }
	virtual std::string_view GetPluginName() { return "Sample Plugin"; }
	virtual std::string_view GetPluginVersion() { return "v1.0.0"; }

	virtual ~SamplePlugin() {}
};

BPLUGIN_EXPORT SamplePlugin* CreatePlugin()
{
	return new SamplePlugin;
}

BPLUGIN_EXPORT void DestroyPlugin(SamplePlugin* pPlugin)
{
	delete pPlugin;
}

#ifdef _WIN32
#include <Windows.h>
// Windows-specific DllMain
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	return TRUE;
}
#endif