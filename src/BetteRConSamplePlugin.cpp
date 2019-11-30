#include <BetteRCon/Plugin.h>

#include <iostream>

// Sample Plugin Definition
BEGINPLUGIN(SamplePlugin)
	CREATEPLUGIN(SamplePlugin)
	{
		RegisterHandler("player.onJoin", std::bind(&SamplePlugin::HandleJoin, this, std::placeholders::_1));
		pServer->ScheduleAction([] { std::cout << "It has been 1000 milliseconds\n"; }, 1000);
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("Sample Plugin")
	VERSIONPLUGIN("v1.0.0")

	virtual void Enable()
	{
		Plugin::Enable();
		std::cout << "Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		std::cout << "Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void HandleJoin(const std::vector<std::string>& eventWords)
	{
		std::cout << "Player " << eventWords.at(1) << " joined\n";
	}
ENDPLUGIN(SamplePlugin)