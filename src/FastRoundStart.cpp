#include <BetteRCon/Plugin.h>

#include <iostream>

// Fast Round Start start the next round 30 seconds after the end of the last round
BEGINPLUGIN(FastRoundStart)
	CREATEPLUGIN(FastRoundStart)
	{
		// every time server.onRoudOver fires, start a timer for 30 seconds later that will start the next round
		RegisterHandler("server.onRoundOver", std::bind(&FastRoundStart::HandleRoundOver, this, std::placeholders::_1));
		
		// if the next level starts for some other reason (like an admin starts it) then cancel the current request
		RegisterHandler("server.onLevelLoaded", std::bind(&FastRoundStart::HandleRoundOver, this, std::placeholders::_1));
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("FastRoundStart")
	VERSIONPLUGIN("v1.0.0")

	virtual void Enable()
	{
		Plugin::Enable();
		std::cout << "[FastRoundStart]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		std::cout << "[FastRoundStart]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void HandleRoundOver(const std::vector<std::string>& eventWords)
	{
		// unused
		(void)eventWords;

		m_cancelNextLevel = false;

		// schedule the command for 30 seconds from now
		m_pServer->ScheduleAction(
			[this] 
		{
			// the next round was started! abort!
			if (m_cancelNextLevel == true)
				return;

			m_pServer->SendCommand({ "mapList.runNextRound" }, 
				[](const BetteRCon::Server::ErrorCode_t& ec, const std::vector<std::string>& responseWords)
			{
				if (ec)
				{
					std::cerr << "[FastRoundStart]: Error running next round: " << ec.message() << '\n';
					return 1;
				}

				if (responseWords.front() != "OK")
				{
					std::cerr << "[FastRoundStart]: Bad response while running next round: " << responseWords.front() << '\n';
				}

				std::cout << "[FastRoundStart]: Ran next round successfully\n";
			});
		}, 30000);
	}

	void HandleLevelLoaded(const std::vector<std::string>& eventWords)
	{
		// unused
		(void)eventWords;

		// cancel any in-progress timer
		m_cancelNextLevel = true;
	}

	bool m_cancelNextLevel;
ENDPLUGIN(FastRoundStart)