#include <BetteRCon/Plugin.h>

// STL
#include <fstream>
#include <streambuf>
#include <queue>
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

		// listen for playerInfo too so we can execute moves in the queue with updated team information
		RegisterHandler("bettercon.playerInfo", std::bind(&Assist::HandlePlayerInfo, this, std::placeholders::_1));
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

		// save the scores
		m_lastScores = serverInfo.m_scores.m_teamScores;
		m_lastScoreDiffs.resize(m_lastScores.size());

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

		// get server type
		SendCommand({ "vars.serverType" }, [this](const BetteRCon::Server::ErrorCode_t& ec, const std::vector<std::string>& response)
		{
			if (ec)
				return;

			if (response.size() != 2 ||
				response[0] != "OK")
			{
				BetteRCon::Internal::g_stdErrLog << "[Assist]: Failed to get ticket multiplier\n";
			}

			m_isNotOfficial = response[1] != "OFFICIAL";
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
		std::ifstream inFile("plugins/Assist.db", std::ios::binary);
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
			dbData.resize(dbData.size() + entry.first.size());
			memcpy(&dbData[dbData.size() - entry.first.size()], entry.first.c_str(), entry.first.size());

			// make space for their data
			dbData.resize(dbData.size() + sizeof(PlayerStrengthEntry));

			// copy their data
			memcpy(&dbData[dbData.size() - sizeof(PlayerStrengthEntry)], &entry.second, sizeof(PlayerStrengthEntry));
		}

		// try to open the output database
		std::ofstream outFile("plugins/Assist.db", std::ios::binary);

		// write the file
		outFile.write(dbData.data(), dbData.size());

		outFile.close();
	}

	void CalculatePlayerStrength(const BetteRCon::Server::ServerInfo& serverInfo, const size_t numTeams, const std::shared_ptr<BetteRCon::Server::PlayerInfo>& pPlayer,
		const std::vector<float>& playerStrengths, const std::vector<float>& playerKDTotals, const std::vector<float>& playerKPRTotals, const std::vector<float>& playerSPRTotals, 
		const std::vector<uint32_t>& teamSizes, PlayerStrengthEntry& playerStrengthEntry, bool roundEnd = false, bool win = false)
	{
		// teamIds start at 1, but our vector starts at 0. it is size 2
		const auto enemyStrength = playerStrengths[pPlayer->teamId % 2];
		const auto friendlyStrength = playerStrengths[pPlayer->teamId - 1];

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

		// they sent more than just the assist request
		if (chatMessage.size() > 8)
			return;

		// they didn't send us an assist request
		if (chatMessage.find("!assist") == std::string::npos)
			return;

		const auto& players = GetPlayers();

		// see if they are a player
		const auto playerIt = players.find(playerName);
		if (playerIt == players.end())
			return;

		const auto& pPlayer = playerIt->second;

		if (pPlayer->teamId == 0 ||
			pPlayer->type != 0)
		{
			SendChatMessage("[Assist] You are not a player!", pPlayer);
			return;
		}

		// see if the server is not official
		if (m_isNotOfficial == false)
		{
			SendChatMessage("[Assist] Assist is not possible on official!", pPlayer);
			return;
		}

		// see if the game is active
		if (m_inRound == false ||
			m_lastScores.size() != 2)
		{
			SendChatMessage("[Assist] The round must be in play in order to use assist!");
			return;
		}

		// see if it has been long enough
		constexpr std::chrono::minutes timeBeforeAssist(4);

		auto timeLeft = std::chrono::duration_cast<std::chrono::seconds>((m_levelStart + timeBeforeAssist) - std::chrono::system_clock::now());
		if (timeLeft.count() > 0)
		{
			SendChatMessage("[Assist] " + std::to_string(timeLeft.count()) + " seconds left before players are allowed to use assist!", pPlayer);
			return;
		}

		// see if their team is winning
		const auto enemyScore = m_lastScores[pPlayer->teamId % 2];
		const auto friendlyScore = m_lastScores[pPlayer->teamId - 1];

		const auto& serverInfo = GetServerInfo();
		const auto maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		// see if it is too close to the end of the round
		if (enemyScore < maxScore / 4 ||
			friendlyScore < maxScore / 4)
		{
			SendChatMessage("[Assist] Less than 25% of tickets are left, you cannot use assist!", pPlayer);
			return;
		}

		const auto enemyScoreDifference = m_lastScoreDiffs[pPlayer->teamId % 2];
		const auto friendlyScoreDifference = m_lastScoreDiffs[pPlayer->teamId - 1];

		if (enemyScore > friendlyScore)
		{
			// the enemy is winning. they have no reason to switch. see if they are coming back
			if (enemyScoreDifference > friendlyScoreDifference)
				SendChatMessage("[Assist] Your team is coming back, but the enemy is still winning!", pPlayer);
			else
				SendChatMessage("[Assist] The enemy team is winning and is still gaining!", pPlayer);
			return;
		}
		else if (friendlyScoreDifference > enemyScoreDifference)
		{
			// the enemy is coming back
			SendChatMessage("[Assist] The enemy is losing, but they are making a comeback!", pPlayer);
			return;
		}

		const auto numTeams = serverInfo.m_scores.m_teamScores.size();

		std::vector<float> playerStrengths(numTeams);
		std::vector<uint32_t> teamSizes(numTeams);

		// first find the total strength for each team
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			++teamSizes[pPlayer->teamId - 1];

			// see if they are already in the database
			const auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const auto& playerStrengthEntry = playerStrengthIt->second;

			const auto playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		// see if the enemy team is at least 20% stronger than they are
		const auto friendlyStrength = playerStrengths[pPlayer->teamId - 1] / teamSizes[pPlayer->teamId - 1];
		const auto enemyStrength = playerStrengths[pPlayer->teamId % 2] / teamSizes[pPlayer->teamId % 2];
		
		const auto strengthRatio = enemyStrength / friendlyStrength;

		if (strengthRatio > 1.5f)
		{
			const auto strengthPctDiff = static_cast<uint32_t>((strengthRatio - 1.f) * 100);
			SendChatMessage("[Assist] The enemy team is " + std::to_string(strengthPctDiff) + "% stronger than your team!", pPlayer);
			return;
		}

		// see if they assisted within the last 5 minutes
		constexpr std::chrono::minutes assistTimeout(5);
		
		const auto lastAssistIt = m_playerLastAssists.find(playerName);
		if (lastAssistIt != m_playerLastAssists.end())
		{
			if (std::chrono::system_clock::now() < (lastAssistIt->second + assistTimeout))
			{
				SendChatMessage("[Assist] You can only use assist once every 5 minutes!", pPlayer);
				return;
			}
		}
		
		// they are good. add them to the move queue
		m_moveQueue.push(playerName);
		m_playerLastAssists.emplace(playerName, std::chrono::system_clock::now());
		SendChatMessage("[Assist] Your assist request has been accepted and you are number " + std::to_string(m_moveQueue.size()) + " in queue!", pPlayer);
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

			const auto playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

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

			const auto playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

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
		if (m_inRound == false)
			return;

		// serverInfo doesn't actually give us the event information, we need to retrieve that from the server
		const auto& serverInfo = GetServerInfo();

		// fetch the latest scores
		const auto& scores = serverInfo.m_scores.m_teamScores;

		// calculate score differences
		if (m_lastScoreDiffs.size() != scores.size())
			m_lastScoreDiffs.resize(scores.size());

		if (m_lastScores.size() != scores.size())
			m_lastScores.resize(scores.size());

		for (size_t i = 0; i < scores.size(); ++i)
			m_lastScoreDiffs[i] = std::abs(scores[i] - m_lastScores[i]);

		// save the current scores as the last scores
		m_lastScores = scores;
	}

	void HandlePlayerInfo(const std::vector<std::string>& eventArgs)
	{
		if (m_inRound == false)
			return;

		const auto& serverInfo = GetServerInfo();
		const auto& players = GetPlayers();
		
		const auto maxTeamSize = serverInfo.m_maxPlayerCount / serverInfo.m_scores.m_teamScores.size();

		while (m_moveQueue.size() > 0)
		{
			const auto& firstPlayer = m_moveQueue.front();

			// find the player in our player list
			const auto playerIt = players.find(firstPlayer);
			if (playerIt == players.end())
			{
				m_moveQueue.pop();
				continue;
			}

			const auto& pPlayer = playerIt->second;

			// make sure their team has space
			uint32_t teamSize = 0;
			for (const auto& player : players)
				teamSize += (player.second->teamId == pPlayer->teamId);

			if (teamSize >= maxTeamSize)
				// there is not enough space. wait until the next time around
				break;

			// we are good to switch them. let's do it
			ForceMovePlayer((pPlayer->teamId % 2) + 1, 0, pPlayer);
			m_moveQueue.pop();
		}
	}

	// scores
	std::vector<int32_t> m_lastScores;
	std::vector<int32_t> m_lastScoreDiffs;
	std::chrono::system_clock::time_point m_lastScoreCalculation;

	// round stuff
	bool m_isNotOfficial = true;
	bool m_inRound = true;
	float m_gameModeCounter = 1.f;
	std::chrono::system_clock::time_point m_levelStart;

	// assists
	std::unordered_map<std::string, std::chrono::system_clock::time_point> m_playerLastAssists;
	
	// strength
	std::unordered_map<std::string, PlayerStrengthEntry> m_playerStrengthDatabase;

	// move queue
	std::queue<std::string> m_moveQueue;
ENDPLUGIN(Assist)