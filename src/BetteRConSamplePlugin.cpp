#include <BetteRCon/Plugin.h>

#include <iostream>

// Sample Plugin Definition
BEGINPLUGIN(SamplePlugin)
public:
	CREATEPLUGIN
	(SamplePlugin,
		RegisterHandler("player.onJoin", 
			[](const std::vector<std::string>& eventWords)
		{
			std::cout << "Player " << eventWords.at(1) << " joined\n";
		});
	);

	AUTHORPLUGIN("MrElectrify");
	NAMEPLUGIN("Sample Plugin");
	VERSIONPLUGIN("v1.0.0");

	ENABLEPLUGIN(
		std::cout << "Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	)

	DISABLEPLUGIN(
		std::cout << "Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	)
ENDPLUGIN(SamplePlugin)