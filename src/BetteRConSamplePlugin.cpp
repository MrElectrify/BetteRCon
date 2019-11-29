#include <BetteRCon/Plugin.h>

#include <iostream>

// Sample Plugin Definition
BRBEGINPLUGIN(SamplePlugin)
public:
	// Constructor
	SamplePlugin()
	{
		RegisterHandler("player.onJoin", 
			[](const std::vector<std::string>& eventWords)
		{
			std::cout << "Player " << eventWords.at(1) << " joined\n";
		});
	}

	virtual std::string_view GetPluginAuthor() { return "MrElectrify"; }
	virtual std::string_view GetPluginName() { return "Sample Plugin"; }
	virtual std::string_view GetPluginVersion() { return "v1.0.0"; }

	virtual ~SamplePlugin() {}
BRENDPLUGIN(SamplePlugin)