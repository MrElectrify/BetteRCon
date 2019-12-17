#include <BetteRCon/Plugin.h>

// STL
#include <iostream>

#ifdef _WIN32
// Windows stuff
#include <Windows.h>
#endif

// Sample Plugin Definition
class SamplePlugin : public BetteRCon::Plugin
{
public:
	SamplePlugin(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// register the join handler that will be called every time player.onJoin is fired
		RegisterHandler("player.onJoin", std::bind(&SamplePlugin::HandleJoin, this, std::placeholders::_1));

		// schedule an action for 1000 ms in the future, that will print that 1000 milliseconds have passed
		ScheduleAction([] { BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: It has been 1000 milliseconds since creation\n"; }, 1000);

		// retrieve the server's name
		SendCommand({ "vars.serverName" },
			[](const BetteRCon::Server::ErrorCode_t& ec, const std::vector<std::string>& words)
		{
			if (ec)
			{
				BetteRCon::Internal::g_stdErrLog << "[Sample Plugin]: Failed to get server name: " << ec.message() << '\n';
				return;
			}

			if (words.front() != "OK")
			{
				BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: Bad response: " << words.front() << '\n';
				return;
			}

			BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: Server name: " << words.at(1) << '\n';
		});
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "Sample Plugin"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.1"; }

	virtual void Enable()
	{
		Plugin::Enable();
		BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void HandleJoin(const std::vector<std::string>& eventWords)
	{
		BetteRCon::Internal::g_stdOutLog << "[Sample Plugin]: Player " << eventWords.at(1) << " joined\n";
	}

	virtual ~SamplePlugin() {}
};

PLUGIN_EXPORT SamplePlugin* CreatePlugin(BetteRCon::Server* pServer)
{
	return new SamplePlugin(pServer);
}

PLUGIN_EXPORT void DestroyPlugin(SamplePlugin* pPlugin)
{
	delete pPlugin;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReserved, LPVOID lpReserved)
{
	return TRUE;
}
#endif