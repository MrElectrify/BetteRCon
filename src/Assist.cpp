#include <BetteRCon/Plugin.h>

// STL
#include <fstream>
#include <list>
#include <streambuf>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
// Windows stuff
#include <Windows.h>
#endif

// Assist allows players to assist the losing team if they are unable to switch manually. ONLY FOR CONQUEST LARGE RIGHT NOW
class Assist : public BetteRCon::Plugin
{
public:
	// weighted player strength based on how long they were in the game and the strength of the enemy team
	struct PlayerStrengthEntry
	{
		float roundSamples;
		float relativeKDR;
		float relativeKPR;
		float relativeSPR;
		float winLossRatio;
		int assists;
	};

	using MoveQueue_t = std::list<std::string>;
	using PlayerInfo = BetteRCon::Server::PlayerInfo;
	using PlayerMap_t = BetteRCon::Server::PlayerMap_t;
	using ServerInfo = BetteRCon::Server::ServerInfo;
	using SquadMap_t = BetteRCon::Server::SquadMap_t;
	using Team_t = BetteRCon::Server::Team;
	using PlayerAssistMap_t = std::unordered_map<std::string, std::chrono::system_clock::time_point>;
	using PlayerStrengthMap_t = std::unordered_map<std::string, PlayerStrengthEntry>;

	Assist(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// read the player information flatfile database
		ReadPlayerDatabase();

		// we want to listen for chat messages
		RegisterHandler("player.onChat", std::bind(&Assist::HandlePlayerChat, this, std::placeholders::_1));

		// don't let them get away so easily
		RegisterHandler("player.onLeave", std::bind(&Assist::HandlePlayerLeave, this, std::placeholders::_1));

		// listen for team changes so that we can remove them from the move queue if they manually switch
		RegisterHandler("player.onTeamChange", std::bind(&Assist::HandleTeamChange, this, std::placeholders::_1));

		// store the current round time
		RegisterHandler("server.onLevelLoaded", std::bind(&Assist::HandleLevelLoaded, this, std::placeholders::_1));

		// listen for round end to store the winning team
		RegisterHandler("server.onRoundOver", std::bind(&Assist::HandleRoundOver, this, std::placeholders::_1));

		// also listen for round end players to store player information in our flatfile database
		RegisterHandler("server.onRoundOverPlayers", std::bind(&Assist::HandleRoundOverPlayers, this, std::placeholders::_1));

		// listen for serverInfo so we can calculate the ticket deficit
		RegisterHandler("bettercon.serverInfo", std::bind(&Assist::HandleServerInfo, this, std::placeholders::_1));

		// listen for playerInfo too so we can execute moves in the queue with updated team information
		RegisterHandler("bettercon.playerInfo", std::bind(&Assist::HandlePlayerInfo, this, std::placeholders::_1));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "Assist"; }
	virtual std::string_view GetPluginVersion() const { return "v1.2.2"; }

	virtual void Enable()
	{
		Plugin::Enable();

		// check serverInfo to see if we are conquest or in endscreen
		const ServerInfo& serverInfo = GetServerInfo();

		if (serverInfo.m_gameMode.find("Conquest") == std::string::npos)
		{
			BetteRCon::Internal::g_stdErrLog << "[Assist]: Failed to start: Mode must be ConquestSmall or ConquestLarge\n";
			Plugin::Disable();
			return;
		}

		m_inRound = serverInfo.m_scores.m_teamScores.size() > 0;
		for (const int32_t teamTickets : serverInfo.m_scores.m_teamScores)
		{
			if (teamTickets <= 0)
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

		const uint32_t mapSize = *reinterpret_cast<uint32_t*>(&dbData[0]);

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
			const uint8_t nameLen = *reinterpret_cast<uint8_t*>(&dbData[offset]);
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

		for (const PlayerStrengthMap_t::value_type& entry : m_playerStrengthDatabase)
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
		PlayerStrengthEntry& playerStrengthEntry, bool roundEnd = false, bool win = false)
	{
		const uint8_t enemyTeam = (pPlayer->teamId % 2) + 1;
		const uint8_t friendlyTeam = pPlayer->teamId;

		// teamIds start at 1, but our vector starts at 0. it is size 2
		const float enemyStrength = playerStrengths[enemyTeam - 1];
		const float friendlyStrength = playerStrengths[friendlyTeam - 1];

		// multipliers
		const int32_t minScore = *std::min_element(serverInfo.m_scores.m_teamScores.begin(), serverInfo.m_scores.m_teamScores.end());
		const int32_t maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		const std::chrono::system_clock::duration timeSinceFirstSeen = std::chrono::system_clock::now() - pPlayer->firstSeen;
		const std::chrono::system_clock::duration timeSinceLevelStart = std::chrono::system_clock::now() - m_levelStart;

		const float levelAttendance = (pPlayer->firstSeen > m_levelStart) ? static_cast<float>(timeSinceFirstSeen.count()) / timeSinceLevelStart.count() : 1.f;
		const float roundTime = levelAttendance * ((maxScore != 0) ? ((roundEnd == true) ? 1.f : (static_cast<float>(maxScore - minScore) / maxScore)) : 1.f);
		const float strengthMultiplier = std::min((friendlyStrength != 0.f) ? enemyStrength / friendlyStrength : 1.f, 2.f);

		const uint32_t friendlyTeamSize = GetTeam(friendlyTeam).playerCount;

		// friendly stats for comparison
		const float friendlyAvgKDR = (friendlyTeamSize > 0) ? playerKDTotals[pPlayer->teamId - 1] / friendlyTeamSize : 1.f;
		const float friendlyAvgKPR = (friendlyTeamSize > 0) ? playerKPRTotals[pPlayer->teamId - 1] / friendlyTeamSize : 1.f;
		const float friendlyAvgSPR = (friendlyTeamSize > 0) ? playerSPRTotals[pPlayer->teamId - 1] / friendlyTeamSize : 1.f;

		const float totalTime = (roundTime + playerStrengthEntry.roundSamples);

		const float weightedTotalRelativeKDR = playerStrengthEntry.relativeKDR * playerStrengthEntry.roundSamples;
		const float roundKD = (pPlayer->deaths != 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : pPlayer->kills;
		const float roundRelativeKDR = (friendlyAvgKDR != 0.f) ? roundKD / friendlyAvgKDR : 1.f;
		const float weightedRoundRelativeKDR = roundRelativeKDR * roundTime * strengthMultiplier;

		playerStrengthEntry.relativeKDR = (totalTime != 0.f) ? (weightedTotalRelativeKDR + weightedRoundRelativeKDR) / totalTime : 0.f;

		const float weightedTotalRelativeKPR = playerStrengthEntry.relativeKPR * playerStrengthEntry.roundSamples;
		const float roundRelativeKPR = (friendlyAvgKPR != 0.f) ? ((pPlayer->kills / roundTime) / friendlyAvgKPR) : 1.f;
		const float weightedRoundRelativeKPR = roundRelativeKPR * roundTime * strengthMultiplier;

		playerStrengthEntry.relativeKPR = (totalTime != 0.f) ? (weightedTotalRelativeKPR + weightedRoundRelativeKPR) / totalTime : 0.f;

		const float weightedTotalRelativeSPR = playerStrengthEntry.relativeSPR * playerStrengthEntry.roundSamples;
		const float roundRelativeSPR = (friendlyAvgSPR != 0.f) ? pPlayer->score / friendlyAvgSPR : 1.f;
		const float weightedRoundRelativeSPR = roundRelativeSPR * roundTime * strengthMultiplier;

		playerStrengthEntry.relativeSPR = (totalTime != 0.f) ? (weightedTotalRelativeSPR + weightedRoundRelativeSPR) / totalTime : 0.f;

		const float weightedTotalWL = playerStrengthEntry.winLossRatio * playerStrengthEntry.roundSamples;
		const float roundWinLossRatio = 1.f * win;
		const float weightedRoundWinLossRatio = roundWinLossRatio * roundTime;

		playerStrengthEntry.winLossRatio = (totalTime != 0.f) ? (weightedTotalWL + weightedRoundWinLossRatio) / totalTime : 0.f;

		playerStrengthEntry.roundSamples += roundTime;
	}

	void ProcessQueue()
	{
		const ServerInfo& serverInfo = GetServerInfo();
		const PlayerMap_t& players = GetPlayers();
		const std::vector<int32_t>& teamScores = serverInfo.m_scores.m_teamScores;

		const int32_t maxTeamSize = (teamScores.size() != 0) ? serverInfo.m_maxPlayerCount / teamScores.size() : 0;

		while (m_moveQueue.empty() == false)
		{
			const std::string& firstPlayer = m_moveQueue.front();

			// find the player in our player list
			const PlayerMap_t::const_iterator playerIt = players.find(firstPlayer);
			if (playerIt == players.end())
			{
				m_moveQueue.pop_front();
				continue;
			}

			const std::shared_ptr<PlayerInfo>& pPlayer = playerIt->second;

			const uint8_t newTeamId = (pPlayer->teamId % 2) + 1;

			// make sure the enemy team has space
			uint32_t teamSize = GetTeam(newTeamId).playerCount;

			if (teamSize >= maxTeamSize)
				// there is not enough space. wait until the next time around
				break;

			// let's play nice and find them a random squad
			uint8_t squadId = 0;

			// find their team
			const Team_t& newTeam = GetTeam(newTeamId);
			
			constexpr size_t SQUAD_MAX = 5;

			for (const SquadMap_t::value_type& squad : newTeam.squads)
			{
				if (squad.second.size() < SQUAD_MAX)
				{
					squadId = squad.first;
					break;
				}
			}

			// we are good to switch them. let's do it
			MovePlayer(newTeamId, squadId, pPlayer);

			SendChatMessage("[Assist] Thanks for assisting the losing team, "+ pPlayer->name + "!\n", pPlayer);

			m_moveQueue.pop_front();
		}
	}

	void HandlePlayerChat(const std::vector<std::string>& eventArgs)
	{
		const std::string& playerName = eventArgs[1];
		const std::string& chatMessage = eventArgs[2];

		// they sent more than just the assist request
		if (chatMessage.size() > 8)
			return;

		// they didn't send us an assist request
		if (chatMessage.find("!assist") == std::string::npos)
			return;

		const PlayerMap_t& players = GetPlayers();

		// see if they are a player
		const PlayerMap_t::const_iterator playerIt = players.find(playerName);
		if (playerIt == players.end())
			return;

		const std::shared_ptr<PlayerInfo>& pPlayer = playerIt->second;

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
		constexpr std::chrono::minutes timeBeforeAssist(3);

		std::chrono::seconds timeLeft = std::chrono::duration_cast<std::chrono::seconds>((m_levelStart + timeBeforeAssist) - std::chrono::system_clock::now());
		if (timeLeft.count() > 0)
		{
			SendChatMessage("[Assist] " + std::to_string(timeLeft.count()) + " seconds left before players are allowed to use assist (3 minutes after round start)!", pPlayer);
			return;
		}

		const uint8_t enemyTeam = (pPlayer->teamId % 2) + 1;
		const uint8_t friendlyTeam = pPlayer->teamId;

		// see if their team is winning
		const int32_t enemyScore = m_lastScores[enemyTeam - 1];
		const int32_t friendlyScore = m_lastScores[friendlyTeam - 1];

		const ServerInfo& serverInfo = GetServerInfo();
		const int32_t maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		// see if it is too close to the end of the round
		if (enemyScore < maxScore / 4 ||
			friendlyScore < maxScore / 4)
		{
			SendChatMessage("[Assist] Less than 25% of tickets are left, you cannot use assist!", pPlayer);
			return;
		}

		const int32_t enemyScoreDifference = m_lastScoreDiffs[enemyTeam - 1];
		const int32_t friendlyScoreDifference = m_lastScoreDiffs[friendlyTeam - 1];

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

		const size_t numTeams = GetTeams().size();

		std::vector<float> playerStrengths(numTeams);

		// first find the total strength for each team
		for (const PlayerMap_t::value_type& player : players)
		{
			const std::shared_ptr<PlayerInfo>& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			// see if they are already in the database
			const PlayerStrengthMap_t::const_iterator playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;

			const float playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		const uint32_t enemyTeamSize = GetTeam(enemyTeam).playerCount;
		const uint32_t friendlyTeamSize = GetTeam(friendlyTeam).playerCount;

		// see if the enemy team is at least 20% stronger than they are
		const float enemyStrength = (enemyTeamSize > 0) ? playerStrengths[enemyTeam - 1] / enemyTeamSize : 0.f;
		const float friendlyStrength = (friendlyTeamSize > 0) ? playerStrengths[friendlyTeam - 1] / friendlyTeamSize : 0.f;

		const float strengthRatio = (friendlyStrength != 0.f) ? enemyStrength / friendlyStrength : 1.f;

		if (strengthRatio > 1.5f)
		{
			const uint32_t strengthPctDiff = static_cast<uint32_t>((strengthRatio - 1.f) * 100);
			SendChatMessage("[Assist] The enemy team is " + std::to_string(strengthPctDiff) + "% stronger than your team!", pPlayer);
			return;
		}

		// see if they assisted within the last 5 minutes
		constexpr std::chrono::minutes assistTimeout(5);

		const PlayerAssistMap_t::iterator lastAssistIt = m_lastPlayerAssists.find(playerName);
		if (lastAssistIt != m_lastPlayerAssists.end())
		{
			if (std::chrono::system_clock::now() < (lastAssistIt->second + assistTimeout))
			{
				SendChatMessage("[Assist] You can only use assist once every 5 minutes!", pPlayer);
				return;
			}
		}

		// see if they would be too powerful
		const PlayerStrengthMap_t::iterator playerStrengthIt = m_playerStrengthDatabase.find(playerName);
		if (playerStrengthIt != m_playerStrengthDatabase.end())
		{
			PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;

			const float playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);
			const float adjustedEnemyStrength = enemyStrength + playerStrength;
			const float adjustedFriendlyStrength = friendlyStrength - playerStrength;

			const float adjustedStrengthRatio = (adjustedFriendlyStrength != 0.f) ? adjustedEnemyStrength / adjustedFriendlyStrength : 1.f;

			if (adjustedStrengthRatio > 1.75f)
			{
				const uint32_t strengthPctDiff = static_cast<uint32_t>((adjustedStrengthRatio - 1.f) * 100);
				SendChatMessage("[Assist] You would make the other team " + std::to_string(strengthPctDiff) + "% stronger than your team (>75%) (" + std::to_string(adjustedEnemyStrength) + ":" + std::to_string(adjustedFriendlyStrength) + ")!\n", pPlayer);
				return;
			}

			// add a successful assist
			++playerStrengthEntry.assists;
		}

		// they are good. add them to the move queue
		m_moveQueue.push_back(playerName);
		m_lastPlayerAssists.emplace(playerName, std::chrono::system_clock::now());
		SendChatMessage("[Assist] Your assist request has been accepted and you are number " + std::to_string(m_moveQueue.size()) + " in queue!", pPlayer);

		// try to process the queue now
		ProcessQueue();
	}

	void HandlePlayerLeave(const std::vector<std::string>& eventArgs)
	{
		// their stats are already handled somewhere else
		if (m_inRound == false)
			return;

		const std::string& playerName = eventArgs[1];

		const ServerInfo& serverInfo = GetServerInfo();
		const PlayerMap_t& players = GetPlayers();

		// the player was not found
		const PlayerMap_t::const_iterator playerIt = players.find(playerName);
		if (playerIt == players.end())
			return;

		// make sure they are on a playing team
		if (playerIt->second->teamId == 0)
			return;

		const size_t numTeams = serverInfo.m_scores.m_teamScores.size();

		// there are not loaded teams yet
		if (numTeams == 0)
			return;

		std::vector<float> playerStrengths(numTeams);
		std::vector<float> playerKDTotals(numTeams);
		std::vector<float> playerKPRTotals(numTeams);
		std::vector<float> playerSPRTotals(numTeams);

		const int32_t minScore = *std::min_element(serverInfo.m_scores.m_teamScores.begin(), serverInfo.m_scores.m_teamScores.end());
		const int32_t maxScore = ((serverInfo.m_gameMode == "ConquestLarge0") ? 800 : 400) * m_gameModeCounter;

		// first find the total strength for each team
		for (const PlayerMap_t::value_type& player : players)
		{
			const std::shared_ptr<PlayerInfo>& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			const std::chrono::system_clock::duration timeSinceFirstSeen = std::chrono::system_clock::now() - pPlayer->firstSeen;
			const std::chrono::system_clock::duration timeSinceLevelStart = std::chrono::system_clock::now() - m_levelStart;

			const float levelAttendance = (pPlayer->firstSeen > m_levelStart) ? static_cast<float>(timeSinceFirstSeen.count()) / timeSinceLevelStart.count() : 1.f;
			const float roundTime = (maxScore != 0) ? levelAttendance * ((static_cast<float>(maxScore) - minScore) / maxScore) : 1.f;

			// add team telemetry
			playerKDTotals[pPlayer->teamId - 1] += (pPlayer->deaths != 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : pPlayer->kills;
			playerKPRTotals[pPlayer->teamId - 1] += (roundTime != 0.f) ? pPlayer->kills / roundTime : 0.f;
			playerSPRTotals[pPlayer->teamId - 1] += (roundTime != 0.f) ? pPlayer->score / roundTime : 0.f;

			// see if they are already in the database
			const PlayerStrengthMap_t::const_iterator playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;

			const float playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		// see how they did and update their entry
		PlayerStrengthMap_t::iterator playerStrengthIt = m_playerStrengthDatabase.find(playerName);
		if (playerStrengthIt == m_playerStrengthDatabase.end())
			playerStrengthIt = m_playerStrengthDatabase.emplace(playerName, PlayerStrengthEntry{ 0 }).first;

		PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;
		const std::shared_ptr<PlayerInfo>& pPlayer = playerIt->second;

		CalculatePlayerStrength(serverInfo, numTeams, pPlayer, playerStrengths, playerKDTotals, playerKPRTotals, playerSPRTotals, playerStrengthEntry);

		// try to process the queue, maybe a spot just opened up on the other team
		ProcessQueue();
	}

	void HandleLevelLoaded(const std::vector<std::string>& eventArgs)
	{
		m_levelStart = std::chrono::system_clock::now();
		m_inRound = true;
	}

	void HandleRoundOver(const std::vector<std::string>& eventArgs)
	{
		m_lastWinningTeam = static_cast<uint8_t>(std::stoi(eventArgs[1]));
	}

	void HandleRoundOverPlayers(const std::vector<std::string>& eventArgs)
	{
		m_inRound = false;

		const ServerInfo& serverInfo = GetServerInfo();
		const PlayerMap_t& players = GetPlayers();

		const size_t numTeams = serverInfo.m_scores.m_teamScores.size();

		std::vector<float> playerStrengths(numTeams);
		std::vector<float> playerKDTotals(numTeams);
		std::vector<float> playerKPRTotals(numTeams);
		std::vector<float> playerSPRTotals(numTeams);

		// first find the total strength for each team
		for (const PlayerMap_t::value_type& player : players)
		{
			const std::shared_ptr<PlayerInfo>& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			const std::chrono::system_clock::duration timeSinceFirstSeen = std::chrono::system_clock::now() - pPlayer->firstSeen;
			const std::chrono::system_clock::duration timeSinceLevelStart = std::chrono::system_clock::now() - m_levelStart;

			const float levelAttendance = (pPlayer->firstSeen > m_levelStart) ? static_cast<float>(timeSinceFirstSeen.count()) / timeSinceLevelStart.count() : 1.f;

			// add team telemetry
			playerKDTotals[pPlayer->teamId - 1] += (pPlayer->deaths != 0) ? (static_cast<float>(pPlayer->kills) / pPlayer->deaths) : pPlayer->kills;
			playerKPRTotals[pPlayer->teamId - 1] += (levelAttendance != 0) ? pPlayer->kills / levelAttendance : 0.f;
			playerSPRTotals[pPlayer->teamId - 1] += (levelAttendance != 0) ? pPlayer->score / levelAttendance : 0.f;

			// see if they are already in the database
			const PlayerStrengthMap_t::const_iterator playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				continue;

			const PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;

			const float playerStrength = (playerStrengthEntry.relativeKDR / 2) + (playerStrengthEntry.relativeKPR / 2) + (playerStrengthEntry.relativeSPR * 2) + (playerStrengthEntry.winLossRatio * 4);

			// don't include the neutral team's info
			playerStrengths[pPlayer->teamId - 1] += playerStrength;
		}

		// now that we have the strengths, see how each player did
		for (const PlayerMap_t::value_type& player : players)
		{
			const std::shared_ptr<PlayerInfo>& pPlayer = player.second;

			// make sure they are on a playing team
			if (pPlayer->teamId == 0)
				continue;

			// see if they are already in the database
			PlayerStrengthMap_t::iterator playerStrengthIt = m_playerStrengthDatabase.find(pPlayer->name);
			if (playerStrengthIt == m_playerStrengthDatabase.end())
				playerStrengthIt = m_playerStrengthDatabase.emplace(pPlayer->name, PlayerStrengthEntry{}).first;

			PlayerStrengthEntry& playerStrengthEntry = playerStrengthIt->second;

			const bool playerWon = pPlayer->teamId == m_lastWinningTeam;

			CalculatePlayerStrength(serverInfo, numTeams, pPlayer, playerStrengths, playerKDTotals, playerKPRTotals, playerSPRTotals, playerStrengthEntry, true, playerWon);
		}

		// write the database
		WritePlayerDatabase();
	}

	void HandleServerInfo(const std::vector<std::string>& eventArgs)
	{
		if (m_inRound == false)
			return;

		// serverInfo doesn't actually give us the event information, we need to retrieve that from the server
		const ServerInfo& serverInfo = GetServerInfo();

		// fetch the latest scores
		const std::vector<int32_t>& scores = serverInfo.m_scores.m_teamScores;

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

		ProcessQueue();
	}

	void HandleTeamChange(const std::vector<std::string>& eventArgs)
	{
		// no need to search for the player if there is nobody in the queue
		if (m_moveQueue.empty() == true)
			return;

		const std::string& playerName = eventArgs[1];

		const MoveQueue_t::iterator playerQueueIt = std::find(m_moveQueue.begin(), m_moveQueue.end(), playerName);
		
		// they are not in the move queue, ignore them
		if (playerQueueIt == m_moveQueue.end())
			return;

		// they switched with a pending assist, cancel the assist
		m_moveQueue.erase(playerQueueIt);

		const PlayerMap_t& players = GetPlayers();

		// find their player so we can notify them that they were removed
		const PlayerMap_t::const_iterator playerIt = players.find(playerName);

		// they probably left and that's why we got the event
		if (playerIt == players.end())
			return;

		SendChatMessage("[Assist] You have been removed from the queue because you manually switched. Thanks for helping the losing team!", playerIt->second);
	}

	virtual ~Assist() {}
private:
	// scores
	std::vector<int32_t> m_lastScores;
	std::vector<int32_t> m_lastScoreDiffs;
	std::chrono::system_clock::time_point m_lastScoreCalculation;
	uint8_t m_lastWinningTeam;

	// round stuff
	bool m_isNotOfficial = true;
	bool m_inRound = true;
	float m_gameModeCounter = 1.f;
	std::chrono::system_clock::time_point m_levelStart;

	// assists
	PlayerAssistMap_t m_lastPlayerAssists;

	// strength
	PlayerStrengthMap_t m_playerStrengthDatabase;

	// move queue
	MoveQueue_t m_moveQueue;
};

PLUGIN_EXPORT Assist* CreatePlugin(BetteRCon::Server* pServer)
{
	return new Assist(pServer);
}

PLUGIN_EXPORT void DestroyPlugin(Assist* pPlugin)
{
	delete pPlugin;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReserved, LPVOID lpReserved)
{
	return TRUE;
}
#endif