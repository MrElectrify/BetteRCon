#include <BetteRCon/Plugin.h>

// Fast Round Start start the next round 30 seconds after the end of the last round
BEGINPLUGIN(FastRoundStart)
	CREATEPLUGIN(FastRoundStart)
	{
		// every time server.onRoudOver fires, start a timer for 30 seconds later that will start the next round
		RegisterHandler("server.onRoundOver", std::bind(&FastRoundStart::HandleRoundOver, this, std::placeholders::_1));
		
		// if the next level starts for some other reason (like an admin starts it) then cancel the current request
		RegisterHandler("server.onLevelLoaded", std::bind(&FastRoundStart::HandleLevelLoaded, this, std::placeholders::_1));
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("FastRoundStart")
	VERSIONPLUGIN("v1.0.0")

	virtual void Enable()
	{
		Plugin::Enable();
		BetteRCon::Internal::g_stdOutLog << "[FastRoundStart]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		BetteRCon::Internal::g_stdOutLog << "[FastRoundStart]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void HandleRoundOver(const std::vector<std::string>& eventWords)
	{
		// unused
		(void)eventWords;

		m_cancelNextLevel = false;

		BetteRCon::Internal::g_stdOutLog << "[FastRoundStart]: Round is over. Scheduling command for 30 seconds from now\n";

		// schedule the command for 30 seconds from now
		ScheduleAction(
			[this] 
		{
			// the next round was started! abort!
			if (m_cancelNextLevel == true)
				return;

			SendCommand({ "mapList.runNextRound" }, 
				[](const BetteRCon::Server::ErrorCode_t& ec, const std::vector<std::string>& responseWords)
			{
				if (ec)
				{
					BetteRCon::Internal::g_stdErrLog << "[FastRoundStart]: Error running next round: " << ec.message() << '\n';
					return;
				}

				if (responseWords.front() != "OK")
				{
					BetteRCon::Internal::g_stdErrLog << "[FastRoundStart]: Bad response while running next round: " << responseWords.front() << '\n';
					return;
				}

				BetteRCon::Internal::g_stdOutLog << "[FastRoundStart]: Ran next round successfully\n";
			});
		}, 31000);
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