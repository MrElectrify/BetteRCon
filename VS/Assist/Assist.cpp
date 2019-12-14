#include <BetteRCon/Plugin.h>

// STL
#include <fstream>
#include <streambuf>
#include <unordered_map>
#include <vector>

// Assist allows players to assist the losing team if they are unable to switch manually.
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
	}

	AUTHORPLUGIN("MrElectrify")
	NAMEPLUGIN("Assist")
	VERSIONPLUGIN("v1.0.0")

	virtual void Enable()
	{
		Plugin::Enable();

		// in case we are starting mid-round
		m_levelStart = std::chrono::system_clock::now();

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
			const auto nameLen = *reinterpret_cast<uint8_t*>(&dbData[offset += sizeof(uint8_t)]);

			// read the player's name
			std::string playerName(&dbData[offset += nameLen], nameLen);

			// add their entry and copy the data in
			memcpy(&m_playerStrengthDatabase.emplace(std::move(playerName), PlayerStrengthEntry{}).first->second, &dbData[offset += sizeof(PlayerStrengthEntry)], sizeof(PlayerStrengthEntry));
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

			// copy their data
			for (size_t i = 0; i < sizeof(decltype(entry.second)); ++i)
				dbData.push_back('\0');

			// copy their data
			memcpy(&dbData[dbData.size() - 1 - sizeof(decltype(entry.second))], &entry.second, sizeof(decltype(entry.second)));
		}

		// try to open the output database
		std::ofstream outFile("plugins/Assist.db");

		// write the file
		outFile.write(dbData.data(), dbData.size());

		outFile.close();
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

		std::vector<float> playerStrengths(numTeams);
		std::vector<float> playerKDTotals(numTeams);
		std::vector<float> playerScoreTotals(numTeams);
		std::vector<uint32_t> teamSizes(numTeams);

		// first find the total strength for each team
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			// see if they are already in the database
			const auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const auto& playerStrengthEntry = playerStrengthIt->second;

			const auto playerStrength = playerStrengthEntry.relativeKDR + playerStrengthEntry.relativeScore + (2 * playerStrengthEntry.winLossRatio);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
			playerKDTotals[pPlayer->teamId - 1] += (pPlayer->deaths > 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : 1.f;
			playerScoreTotals[pPlayer->teamId - 1] += static_cast<float>(pPlayer->score);
			++teamSizes[pPlayer->teamId];
		}

		// see how they did and update their entry
		auto playerStrengthIt = m_playerStrengthDatabase.find(playerName);
		if (playerStrengthIt == m_playerStrengthDatabase.end())
			playerStrengthIt = m_playerStrengthDatabase.emplace(playerName, PlayerStrengthEntry{}).first;

		auto& playerStrengthEntry = playerStrengthIt->second;
		const auto& pPlayer = playerIt->second;

		float enemyStrength = 0.f;
		for (size_t i = 0; i < numTeams; ++i)
		{
			if (i == pPlayer->teamId - 1)
				continue;

			enemyStrength += playerStrengths.at(i);
		}
		const float friendlyStrength = playerStrengths[pPlayer->teamId - 1];

		// multipliers
		const auto roundTime = (pPlayer->firstSeen > m_levelStart) ? (std::chrono::system_clock::now() - pPlayer->firstSeen) / (std::chrono::system_clock::now() - m_levelStart) : 1.f;
		const auto strengthMultiplier = (friendlyStrength != 0.f) ? enemyStrength / friendlyStrength : 1.f;

		// friendly stats for comparison
		const auto friendlyAvgKDR = playerKDTotals[pPlayer->teamId] / teamSizes[pPlayer->teamId];
		const auto friendlyAvgScore = playerScoreTotals[pPlayer->teamId] / teamSizes[pPlayer->teamId];

		const auto totalTime = (roundTime + playerStrengthEntry.roundSamples);

		const auto weightedTotalRelativeKDR = playerStrengthEntry.relativeKDR * playerStrengthEntry.roundSamples * strengthMultiplier;
		const auto roundRelativeKDR = (friendlyAvgKDR != 0.f) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) / friendlyAvgKDR : 1.f;
		const auto weightedRoundRelativeKDR = roundRelativeKDR * totalTime;

		playerStrengthEntry.relativeKDR = (weightedTotalRelativeKDR + weightedRoundRelativeKDR) / totalTime;

		const auto weightedTotalRelativeScore = playerStrengthEntry.relativeScore * playerStrengthEntry.roundSamples;
		const auto roundRelativeScore = (friendlyAvgScore != 0.f) ? pPlayer->score / friendlyAvgScore : 1.f;
		const auto weightedRoundRelativeScore = roundRelativeScore * totalTime;

		playerStrengthEntry.relativeScore = (weightedTotalRelativeScore + weightedRoundRelativeScore) / totalTime;

		const auto weightedTotalWL = playerStrengthEntry.winLossRatio * playerStrengthEntry.roundSamples * strengthMultiplier;
		const auto roundWinLossRatio = 0.f;

		playerStrengthEntry.winLossRatio = (weightedTotalWL + roundWinLossRatio) / totalTime;

		playerStrengthEntry.roundSamples += roundTime;
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
		std::vector<float> playerScoreTotals(numTeams);
		std::vector<uint32_t> teamSizes(numTeams);

		// first find the total strength for each team
		for (const auto& player : players)
		{
			const auto& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			// see if they are already in the database
			const auto playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const auto& playerStrengthEntry = playerStrengthIt->second;

			const auto playerStrength = playerStrengthEntry.relativeKDR + playerStrengthEntry.relativeScore + (2 * playerStrengthEntry.winLossRatio);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
			playerKDTotals[pPlayer->teamId - 1] += (static_cast<float>(pPlayer->kills) / pPlayer->deaths);
			playerScoreTotals[pPlayer->teamId - 1] += static_cast<float>(pPlayer->score);
			++teamSizes[pPlayer->teamId];
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

			float enemyStrength = 0.f;
			for (size_t i = 0; i < numTeams; ++i)
			{
				if (i == pPlayer->teamId - 1)
					continue;

				enemyStrength += playerStrengths.at(i);
			}
			const float friendlyStrength = playerStrengths[pPlayer->teamId - 1];

			// multipliers
			const auto roundTime = (pPlayer->firstSeen > m_levelStart) ? (std::chrono::system_clock::now() - pPlayer->firstSeen) / (std::chrono::system_clock::now() - m_levelStart) : 1.f;
			const auto strengthMultiplier = (friendlyStrength != 0.f) ? enemyStrength / friendlyStrength : 1.f;

			// friendly stats for comparison
			const auto friendlyAvgKDR = playerKDTotals[pPlayer->teamId] / teamSizes[pPlayer->teamId];
			const auto friendlyAvgScore = playerScoreTotals[pPlayer->teamId] / teamSizes[pPlayer->teamId];

			const auto totalTime = (roundTime + playerStrengthEntry.roundSamples);

			const auto weightedTotalRelativeKDR = playerStrengthEntry.relativeKDR * playerStrengthEntry.roundSamples * strengthMultiplier;
			const auto roundRelativeKDR = (friendlyAvgKDR != 0.f) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) / friendlyAvgKDR : 1.f;
			const auto weightedRoundRelativeKDR = roundRelativeKDR * totalTime;

			playerStrengthEntry.relativeKDR = (weightedTotalRelativeKDR + weightedRoundRelativeKDR) / totalTime;

			const auto weightedTotalRelativeScore = playerStrengthEntry.relativeScore * playerStrengthEntry.roundSamples;
			const auto roundRelativeScore = (friendlyAvgScore != 0.f) ? pPlayer->score / friendlyAvgScore : 1.f;
			const auto weightedRoundRelativeScore = roundRelativeScore * totalTime;

			playerStrengthEntry.relativeScore = (weightedTotalRelativeScore + weightedRoundRelativeScore) / totalTime;

			const auto weightedTotalWL = playerStrengthEntry.winLossRatio * playerStrengthEntry.roundSamples * strengthMultiplier;
			const auto roundWinLossRatio = 0.f;

			playerStrengthEntry.winLossRatio = (weightedTotalWL + roundWinLossRatio) / totalTime;

			playerStrengthEntry.roundSamples += roundTime;
		}

		// write the database
		WritePlayerDatabase();
	}

	// weighted player strength based on how long they were in the game and the strength of the enemy team
	struct PlayerStrengthEntry
	{
		float roundSamples;
		float relativeKDR;
		float relativeScore;
		float winLossRatio;
	};

	bool m_inRound;
	std::chrono::system_clock::time_point m_levelStart;
	std::unordered_map<std::string, PlayerStrengthEntry> m_playerStrengthDatabase;
ENDPLUGIN(Assist)