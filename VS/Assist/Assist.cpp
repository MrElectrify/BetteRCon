#include <BetteRCon/Plugin.h>

// Assist allows players to assist the losing team if they are unable to switch manually.
BEGINPLUGIN(Assist)
	CREATEPLUGIN(Assist)
	{
		// we want to listen for chat messages
		RegisterHandler("player.onChat", std::bind(&Assist::HandleChat, this, std::placeholders::_1));

		// also listen for round end messages to store player information in
		RegisterHandler("server.onRoundOver", std::bind(&Assist::HandleRoundOver, this, std::placeholders::_1));
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("Assist")
	VERSIONPLUGIN("v1.0.0")

	virtual void Enable()
	{
		Plugin::Enable();
		BetteRCon::Internal::g_stdOutLog << "[Assist]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		BetteRCon::Internal::g_stdOutLog << "[FastRoundStart]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void HandleChat(const std::vector<std::string>& eventArgs)
	{

	}

	void HandleRoundOver(const std::vector<std::string>& eventArgs)
	{

	}
ENDPLUGIN(Assist)