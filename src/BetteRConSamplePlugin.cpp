#include <BetteRCon/Plugin.h>

#include <iostream>

// Sample Plugin Definition
BEGINPLUGIN(SamplePlugin)
	CREATEPLUGIN(SamplePlugin)
	{
		// register the join handler that will be called every time player.onJoin is fired
		RegisterHandler("player.onJoin", std::bind(&SamplePlugin::HandleJoin, this, std::placeholders::_1));

		// schedule an action for 1000 ms in the future, that will print that 1000 milliseconds have passed
		ScheduleAction([] { std::cout << "[Sample Plugin]: It has been 1000 milliseconds\n"; }, 1000);

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

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("Sample Plugin")
	VERSIONPLUGIN("v1.0.0")

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
ENDPLUGIN(SamplePlugin)