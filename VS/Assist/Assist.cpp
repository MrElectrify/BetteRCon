#include <BetteRCon/Plugin.h>

// STL
#include <fstream>
#include <streambuf>
#include <unordered_map>
#include <vector>

// Assist allows players to assist the losing team if they are unable to switch manually. ONLY FOR CONQUEST LARGE RIGHT NOW
BEGINPLUGIN(Assist)
	CREATEPLUGIN(Assist)
	{
		// read the player information flatfile database
		ReadPlayerDatabase();

		// we want to listen for chat messages
		RegisterHandler("player.onChat", std::bind(&Assist::HandlePlayerChat, this, std::placeholders::_1));

		// don't let them get away so easily
		RegisterHandler("player.onLeave", std::bind(&Assist::HandlePlayerLeave, this, std::placeholders::_1));

		// store the current round time
		RegisterHandler("server.onLevelLoaded", std::bind(&Assist::HandleLevelLoaded, this, std::placeholders::_1));

		// also listen for round end to store player information in our flatfile database
		RegisterHandler("server.onRoundOverPlayers", std::bind(&Assist::HandleRoundOver, this, std::placeholders::_1));

		// listen for serverInfo so we can calculate the ticket deficit
		RegisterHandler("bettercon.serverInfo", std::bind(&Assist::HandleServerInfo, this, std::placeholders::_1));
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("Assist")
	VERSIONPLUGIN("v1.0.0")

	// weighted player strength based on how long they were in the game and the strength of the enemy team
	struct PlayerStrengthEntry
	{
		float roundSamples;
		float relativeKDR;
		float relativeKPR;
		float relativeSPR;
		float winLossRatio;
	};

	virtual void Enable()
	{
		Plugin::Enable();

		// check serverInfo to see if we are conquest or in endscreen
		const auto& serverInfo = GetServerInfo();

		if (serverInfo.m_gameMode.find("Conquest") == std::string::npos)
		{
			BetteRCon::Internal::g_stdErrLog << "[Assist]: Failed to start: Mode must be ConquestSmall or ConquestLarge\n";
			Plugin::Disable();
			return;
		}

		m_inRound = serverInfo.m_scores.m_teamScores.size() > 0;
		for (const auto teamTickets : serverInfo.m_scores.m_teamScores)
		{
			if (teamTickets <= 0.f)
			{
				m_inRound = false;
				continue;
			}
		}

		// in case we are starting mid-round
		m_levelStart = std::chrono::system_clock::now();

		// get ticket multiplier
		SendCommand({ "vars.gameModeCounter" }, [this](const BetteRCon::Server::ErrorCode_t& ec, const std::vector<std::string>& response)
		{
			if (ec)
				return;

			if (response.size() != 2 ||
				response[0] != "OK")
			{
				BetteRCon::Internal::g_stdErrLog << "[Assist]: Failed to get ticket multiplier\n";
			}

			m_gameModeCounter = std::stoi(response[1]) / 100.f;
		});

		BetteRCon::Internal::g_stdOutLog << "[Assist]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		BetteRCon::Internal::g_stdOutLog << "[Assist]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	void ReadPlayerDatabase()
	{
		// try to open the database
		std::ifstream inFile("plugins/Assist.db");
		if (inFile.good() == false)
			return;

		// read the entire file
		std::vector<char> dbData((std::istreambuf_iterator<char>(inFile)),
			std::istreambuf_iterator<char>());

		inFile.close();

		const auto mapSize = *reinterpret_cast<uint32_t*>(&dbData[0]);

		size_t offset = sizeof(uint32_t);
		for (size_t i = 0; i < mapSize; ++i)
		{
			// some naive bounds checking
			if (offset >= dbData.size())
			{
				BetteRCon::Internal::g_stdErrLog << "[Assist]: Invalid DB\n";
				return;
			}

			// this is a new entry. read the size of the name
			const auto nameLen = *reinterpret_cast<uint8_t*>(&dbData[offset]);
			offset += sizeof(uint8_t);

			// read the player's name
			std::string playerName(&dbData[offset], nameLen);
			offset += nameLen;

			// add their entry and copy the data in
			memcpy(&m_playerStrengthDatabase.emplace(std::move(playerName), PlayerStrengthEntry{}).first->second, &dbData[offset], sizeof(PlayerStrengthEntry));
			offset += sizeof(PlayerStrengthEntry);
		}
	}

	void WritePlayerDatabase()
	{
		std::vector<char> dbData(sizeof(uint32_t));

		// insert the map size
		*reinterpret_cast<uint32_t*>(&dbData[0]) = m_playerStrengthDatabase.size();

		for (const auto& entry : m_playerStrengthDatabase)
		{
			// write the size of the player. player names will be max 64 players, so one char is necessary
			dbData.push_back(entry.first.size() & 0xff);
			
			// copy their name
			for (const auto c : entry.first)
				dbData.push_back(c);

			// make space for their data
			for (size_t i = 0; i < sizeof(PlayerStrengthEntry); ++i)
				dbData.push_back('\0');

			// copy their data
			memcpy(&dbData[dbData.size() - sizeof(PlayerStrengthEntry)], &entry.second, sizeof(PlayerStrengthEntry));
		}

		// try to open the output database
		std::ofstream outFile("plugins/Assist.db");

		// write the file
		outFile.write(dbData.data(), dbData.size());

		outFile.close();
	}

	void CalculatePlayerStrength(const BetteRCon::Server::ServerInfo& serverInfo, const size_t numTeams, const std::shared_ptr<BetteRCon::Server::PlayerInfo>& pPlayer,
		const std::vector<float>& playerStrengths, const std::vector<float>& playerKDTotals, const std::vector<float>& playerKPRTotals, const std::vector<float>& playerSPRTotals, 
		const std::vector<uint32_t>& teamSizes, PlayerStrengthEntry& playerStrengthEntry, bool roundEnd = false, bool win = false)
	{
		float enemyStrength = 0.f;
		for (size_t i = 0; i < numTeams; ++i)
		{
			if (i == pPlayer->teamId - 1)
				continue;

			enemyStrength += playerStrengths.at(i);
		}
		const float friendlyStrength = playerStrengths[pPlayer->teamId - 1];

		// multipliers
		const auto minScore = *std::min_element(serverInfo.m_scores.m_teamScores.begin(), serverInfo.m_scores.m_teamScores.end());
		const auto maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		const auto levelAttendance = (pPlayer->firstSeen > m_levelStart) ? ((std::chrono::system_clock::now() - pPlayer->firstSeen) / (std::chrono::system_clock::now() - m_levelStart)) : 1.f;
		const auto roundTime = levelAttendance * ((roundEnd == true) ? 1.f : ((maxScore - minScore) / maxScore));
		const auto strengthMultiplier = (friendlyStrength != 0.f) ? enemyStrength / friendlyStrength : 1.f;

		// friendly stats for comparison
		const auto friendlyAvgKDR = playerKDTotals[pPlayer->teamId - 1] / teamSizes[pPlayer->teamId - 1];
		const auto friendlyAvgKPR = playerKPRTotals[pPlayer->teamId - 1] / teamSizes[pPlayer->teamId - 1];
		const auto friendlyAvgSPR = playerSPRTotals[pPlayer->teamId - 1] / teamSizes[pPlayer->teamId - 1];

		const auto totalTime = (roundTime + playerStrengthEntry.roundSamples);

		const auto weightedTotalRelativeKDR = playerStrengthEntry.relativeKDR * playerStrengthEntry.roundSamples;
		const auto roundRelativeKDR = (friendlyAvgKDR != 0.f) ? ((pPlayer->deaths != 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : 0.f) / friendlyAvgKDR : 1.f;
		const auto weightedRoundRelativeKDR = roundRelativeKDR * totalTime * strengthMultiplier;

		playerStrengthEntry.relativeKDR = (totalTime != 0.f) ? (weightedTotalRelativeKDR + weightedRoundRelativeKDR) / totalTime : 0.f;

		const auto weightedTotalRelativeKPR = playerStrengthEntry.relativeKPR * playerStrengthEntry.roundSamples;
		const auto roundRelativeKPR = (friendlyAvgKPR != 0.f) ? ((pPlayer->kills / roundTime) / friendlyAvgKPR) : 1.f;
		const auto weightedRoundRelativeKPR = roundRelativeKPR * totalTime * strengthMultiplier;

		playerStrengthEntry.relativeKPR = (totalTime != 0.f) ? (weightedTotalRelativeKPR + weightedRoundRelativeKPR) / totalTime : 0.f;

		const auto weightedTotalRelativeSPR = playerStrengthEntry.relativeSPR * playerStrengthEntry.roundSamples;
		const auto roundRelativeSPR = (friendlyAvgSPR != 0.f) ? pPlayer->score / friendlyAvgSPR : 1.f;
		const auto weightedRoundRelativeSPR = roundRelativeSPR * totalTime * strengthMultiplier;

		playerStrengthEntry.relativeSPR = (totalTime != 0.f) ? (weightedTotalRelativeSPR + weightedRoundRelativeSPR) / totalTime : 0.f;

		const auto weightedTotalWL = playerStrengthEntry.winLossRatio * playerStrengthEntry.roundSamples;
		const auto roundWinLossRatio = 0.f;
		const auto weightedRoundWinLossRatio = roundWinLossRatio * strengthMultiplier;

		playerStrengthEntry.winLossRatio = (totalTime != 0.f) ? (weightedTotalWL + weightedRoundWinLossRatio) / totalTime : 0.f;

		playerStrengthEntry.roundSamples += roundTime;
	}

	void HandlePlayerChat(const std::vector<std::string>& eventArgs)
	{
		const auto& playerName = eventArgs.at(1);
		const auto& chatMessage = eventArgs.at(2);

		// they didn't send us an assist request
		if (chatMessage.find("!assist") == std::string::npos)
			return;

		// they sent more than just the assist request
		if (chatMessage.size() > 8)
			return;

		/// TODO: logic!
	}

	void HandlePlayerLeave(const std::vector<std::string>& eventArgs)
	{
		// their stats are already handled somewhere else
		if (m_inRound == false)
			return;

		const auto& playerName = eventArgs.at(1);

		const auto& serverInfo = GetServerInfo();
		const auto& players = GetPlayers();

		// the player was not found
		const auto playerIt = players.find(playerName);
		if (playerIt == players.end())
			return;

		// make sure they are on a playing team
		if (playerIt->second->teamId == 0)
			return;

		const auto numTeams = serverInfo.m_scores.m_teamScores.size();

		// there are not loaded teams yet
		if (numTeams == 0)
			return;

		std::vector<float> playerStrengths(numTeams);
		std::vector<float> playerKDTotals(numTeams);
		std::vector<float> playerKPRTotals(numTeams);
		std::vector<float> playerSPRTotals(numTeams);
		std::vector<uint32_t> teamSizes(numTeams);

		const auto minScore = *std::min_element(serverInfo.m_scores.m_teamScores.begin(), serverInfo.m_scores.m_teamScores.end());
		const auto maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		// first find the total strength for each team
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			const auto levelAttendance = (pPlayer->firstSeen > m_levelStart) ? ((std::chrono::system_clock::now() - pPlayer->firstSeen) / (std::chrono::system_clock::now() - m_levelStart)) : 1.f;
			const auto roundTime = levelAttendance * ((maxScore - minScore) / maxScore);

			// add team telemetry
			++teamSizes[pPlayer->teamId - 1];
			playerKDTotals[pPlayer->teamId - 1] += (pPlayer->deaths > 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : 0.f;
			playerKPRTotals[pPlayer->teamId - 1] += (roundTime != 0.f) ? pPlayer->kills / roundTime : 0.f;
			playerSPRTotals[pPlayer->teamId - 1] += (roundTime != 0.f) ? pPlayer->score / roundTime : 0.f;

			// see if they are already in the database
			const auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const auto& playerStrengthEntry = playerStrengthIt->second;

			const auto playerStrength = playerStrengthEntry.relativeKDR + playerStrengthEntry.relativeKPR + playerStrengthEntry.relativeSPR + (2 * playerStrengthEntry.winLossRatio);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		// see how they did and update their entry
		auto playerStrengthIt = m_playerStrengthDatabase.find(playerName);
		if (playerStrengthIt == m_playerStrengthDatabase.end())
			playerStrengthIt = m_playerStrengthDatabase.emplace(playerName, PlayerStrengthEntry{}).first;

		auto& playerStrengthEntry = playerStrengthIt->second;
		const auto& pPlayer = playerIt->second;

		CalculatePlayerStrength(serverInfo, numTeams, pPlayer, playerStrengths, playerKDTotals, playerKPRTotals, playerSPRTotals, teamSizes, playerStrengthEntry);
	}

	void HandleLevelLoaded(const std::vector<std::string>& eventArgs)
	{
		m_levelStart = std::chrono::system_clock::now();
		m_inRound = true;
	}

	void HandleRoundOver(const std::vector<std::string>& eventArgs)
	{
		m_inRound = false;

		const auto& serverInfo = GetServerInfo();
		const auto& players = GetPlayers();

		const auto numTeams = serverInfo.m_scores.m_teamScores.size();

		std::vector<float> playerStrengths(numTeams);
		std::vector<float> playerKDTotals(numTeams);
		std::vector<float> playerKPRTotals(numTeams);
		std::vector<float> playerSPRTotals(numTeams);
		std::vector<uint32_t> teamSizes(numTeams);

		// first find the total strength for each team
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			const auto levelAttendance = (pPlayer->firstSeen > m_levelStart) ? ((std::chrono::system_clock::now() - pPlayer->firstSeen) / (std::chrono::system_clock::now() - m_levelStart)) : 1.f;

			// add team telemetry
			++teamSizes[pPlayer->teamId - 1];
			playerKDTotals[pPlayer->teamId - 1] += (pPlayer->deaths > 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : 0.f;
			playerKPRTotals[pPlayer->teamId - 1] += (levelAttendance != 0) ? pPlayer->kills / levelAttendance : 0.f;
			playerSPRTotals[pPlayer->teamId - 1] += (levelAttendance != 0) ? pPlayer->score / levelAttendance : 0.f;

			// see if they are already in the database
			const auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const auto& playerStrengthEntry = playerStrengthIt->second;

			const auto playerStrength = playerStrengthEntry.relativeKDR + playerStrengthEntry.relativeKPR + playerStrengthEntry.relativeSPR + (2 * playerStrengthEntry.winLossRatio);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		// now that we have the strengths, see how each player did
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			// see if they are already in the database
			auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				playerStrengthIt = m_playerStrengthDatabase.emplace(pPlayer->name, PlayerStrengthEntry{}).first;

			auto& playerStrengthEntry = playerStrengthIt->second;

			CalculatePlayerStrength(serverInfo, numTeams, pPlayer, playerStrengths, playerKDTotals, playerKPRTotals, playerSPRTotals, teamSizes, playerStrengthEntry, true, pPlayer->teamId);
		}

		// write the database
		WritePlayerDatabase();
	}

	void HandleServerInfo(const std::vector<std::string>& eventArgs)
	{
		// serverInfo doesn't actually give us the event information, we need to retrieve that from the server

	}

	bool m_inRound = true;
	float m_gameModeCounter = 1.f;
	std::chrono::system_clock::time_point m_levelStart;
	std::unordered_map<std::string, PlayerStrengthEntry> m_playerStrengthDatabase;
ENDPLUGIN(Assist)