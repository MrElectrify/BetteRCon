#include <BetteRCon/Plugin.h>

// STL
#include <algorithm>
#include <chrono>
#include <functional>
#include <fstream>
#include <list>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
// Windows stuff
#include <Windows.h>
#endif

// A set of in-game commands including admin commands.
class InGameAdmin : public BetteRCon::Plugin
{
public:
	struct Admin
	{
		std::string name;
		std::string guid;
	};
	using AdminMap_t = std::unordered_map<std::string, std::shared_ptr<Admin>>;
	struct BannedPlayer
	{
		std::vector<std::string> names;
		std::vector<std::string> guids;
		std::vector<std::string> ips;
		std::string reason;
		bool perm;
		std::chrono::system_clock::time_point expiry;
	};
	using BanMap_t = std::unordered_map<std::string, std::shared_ptr<BannedPlayer>>;
	using BanSet_t = std::unordered_set<std::shared_ptr<BannedPlayer>>;
	using ChatHandlerMap_t = std::unordered_map<std::string, std::function<void(const std::string& playerName, const std::vector<std::string>& args)>>;
	using MoveQueue_t = std::list<std::pair<std::string, std::string>>;
	using PlayerInfo_t = BetteRCon::Server::PlayerInfo;
	using FuzzyMatchMap_t = std::unordered_map<std::string, std::pair<const std::vector<std::string>, const char>>;
	using PlayerMap_t = BetteRCon::Server::PlayerMap_t;
	using ServerInfo_t = BetteRCon::Server::ServerInfo;
	using SquadMap_t = BetteRCon::Server::SquadMap_t;
	using Team_t = BetteRCon::Server::Team;

	using Hours_t = std::chrono::hours;
	using Days_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<24>, Hours_t::period>>;
	using Weeks_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<7>, Days_t::period>>;
	using Months_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<30>, Days_t::period>>;

	InGameAdmin(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// read the player information flatfile database
		ReadAdminDatabase();

		// register commands
		RegisterCommand("ban", std::bind(&InGameAdmin::HandleBan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("find", std::bind(&InGameAdmin::HandleFind, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("fmove", std::bind(&InGameAdmin::HandleForceMove, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("kick", std::bind(&InGameAdmin::HandleKick, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("kill", std::bind(&InGameAdmin::HandleKill, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("move", std::bind(&InGameAdmin::HandleMove, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("no", std::bind(&InGameAdmin::HandleNo, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("tban", std::bind(&InGameAdmin::HandleTBan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("unban", std::bind(&InGameAdmin::HandleUnban, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		RegisterCommand("yes", std::bind(&InGameAdmin::HandleYes, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

		// register handlers
		RegisterHandler("bettercon.endOfPBPlayerList", std::bind(&InGameAdmin::HandleOnEndOfPBPlayerList, this, std::placeholders::_1));
		RegisterHandler("bettercon.playerPBConnected", std::bind(&InGameAdmin::HandleOnPlayerPBConnected, this, std::placeholders::_1));
		RegisterHandler("player.onKill", std::bind(&InGameAdmin::HandleOnKill, this, std::placeholders::_1));
		RegisterHandler("player.onLeave", std::bind(&InGameAdmin::HandleOnKill, this, std::placeholders::_1));
		RegisterHandler("player.onTeamChange", std::bind(&InGameAdmin::HandleOnTeamSwitch, this, std::placeholders::_1));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "InGameAdmin"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.1"; }

	virtual void Enable() { Plugin::Enable(); ReadAdminDatabase(); ReadBanDatabase(); }

	virtual void Disable() { Plugin::Disable(); WriteAdminDatabase(); WriteBanDatabase(); }

	virtual ~InGameAdmin() {}
private:
	AdminMap_t m_adminNames;
	AdminMap_t m_adminGUIDs;

	BanMap_t m_banNames;
	BanMap_t m_banGUIDs;
	BanMap_t m_banIPs;
	BanSet_t m_bans;

	MoveQueue_t m_forceMoveQueue;
	MoveQueue_t m_moveQueue;

	FuzzyMatchMap_t m_lastFuzzyMatchMap;

	bool m_checkedInGame = false;
	
	void ReadAdminDatabase()
	{
		// try to open the database
		std::ifstream inFile("plugins/Admins.cfg");
		if (inFile.good() == false)
			return;

		// read each admin
		std::string adminLine;
		size_t line = 0;
		while (std::getline(inFile, adminLine))
		{
			const size_t comma = adminLine.find(',');
			if (comma == std::string::npos)
			{
				BetteRCon::Internal::g_stdErrLog << "Failed to find comma for admin on line " << line << '\n';
				return;
			}

			// split the name and guid up, and add it to our admin maps
			std::string adminName = adminLine.substr(0, comma);
			std::string adminGUID = adminLine.substr(comma + 1);

			const std::shared_ptr<Admin> pAdmin = std::make_shared<Admin>(Admin{ std::move(adminName), std::move(adminGUID) });

			m_adminNames.emplace(pAdmin->name, pAdmin);
			m_adminGUIDs.emplace(pAdmin->guid, std::move(pAdmin));

			++line;
		}

		inFile.close();
	}
	void WriteAdminDatabase()
	{
		// open the output file
		std::ofstream outFile("plugins/Admins.cfg");
		if (outFile.good() == false)
			return;

		// write each admin
		for (const AdminMap_t::value_type& adminName : m_adminNames)
			outFile << adminName.first << ',' << adminName.second->guid << '\n';

		outFile.close();
	}
	
	void ReadBanDatabase() 
	{
		// try to open the file
		std::ifstream inFile("plugins/Bans.db", std::ios::binary);
		if (inFile.good() == false)
			return;

		// read the file
		std::vector<char> dbFile(std::istreambuf_iterator<char>(inFile), {});

		// read the size
		const uint32_t banCount = *reinterpret_cast<uint32_t*>(&dbFile[0]);
		
		size_t offset = sizeof(uint32_t);

		const auto readString = [&dbFile, &offset]() -> std::string
		{
			// read the string size
			const uint8_t strLen = static_cast<uint8_t>(dbFile[offset]);
			++offset;

			// read the string
			std::string res(&dbFile[offset], strLen);
			offset += strLen;

			return res;
		};
		
		const auto readStrVec = [&dbFile, &offset, &readString]() -> std::vector<std::string>
		{
			// read the vector size
			const uint32_t vecLen = *reinterpret_cast<uint32_t*>(&dbFile[offset]);
			offset += sizeof(uint32_t);

			// read each string
			std::vector<std::string> res;
			for (uint32_t i = 0; i < vecLen; ++i)
				res.push_back(readString());

			return res;
		};
		
		for (uint32_t i = 0; i < banCount; ++i)
		{
			// naive bounds check
			if (offset >= dbFile.size())
			{
				BetteRCon::Internal::g_stdErrLog << "[InGameAdmin] Invalid DB\n";
				return;
			}

			const std::vector<std::string> names = readStrVec();
			const std::vector<std::string> guids = readStrVec();
			const std::vector<std::string> ips = readStrVec();
			const std::string reason = readString();

			const bool perm = static_cast<bool>(dbFile[offset]);
			++offset;

			const std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(*reinterpret_cast<time_t*>(&dbFile[offset]));
			offset += sizeof(time_t);

			// see if the ban already expired
			if (perm == false &&
				std::chrono::system_clock::now() >= tp)
				continue;

			// add the ban to the databases
			std::shared_ptr<BannedPlayer> pBannedPlayer = std::make_shared<BannedPlayer>(BannedPlayer{ std::move(names), std::move(guids), std::move(ips), std::move(reason), perm, tp });
			AddBan(std::move(pBannedPlayer));
		}

		inFile.close();
		// write the database to reflect removed bans
		WriteBanDatabase();
	}
	void WriteBanDatabase() 
	{
		// try to open the ban database
		std::ofstream outFile("plugins/Bans.db", std::ios::binary);
		if (outFile.good() == false)
			return;

		// write the ban database size
		std::vector<char> dbData(sizeof(uint32_t));
		*reinterpret_cast<uint32_t*>(&dbData[0]) = m_bans.size();

		const auto writeString = [&dbData](const std::string& str)
		{
			// write the size
			dbData.push_back(str.size() & 0xff);
			// write the string
			dbData.insert(dbData.end(), str.begin(), str.end());
		};

		const auto writeStrVec = [&dbData, &writeString](const std::vector<std::string>& vec)
		{
			// write the size
			dbData.resize(dbData.size() + sizeof(uint32_t));
			*reinterpret_cast<uint32_t*>(&dbData[dbData.size() - sizeof(uint32_t)]) = vec.size();

			// write each string
			for (const std::string& str : vec)
				writeString(str);
		};

		// write each ban
		for (const BanSet_t::value_type& pBannedPlayer : m_bans)
		{
			writeStrVec(pBannedPlayer->names);
			writeStrVec(pBannedPlayer->guids);
			writeStrVec(pBannedPlayer->ips);
			writeString(pBannedPlayer->reason);

			dbData.push_back(pBannedPlayer->perm);

			dbData.resize(dbData.size() + sizeof(time_t));
			*reinterpret_cast<time_t*>(&dbData[dbData.size() - sizeof(time_t)]) = std::chrono::system_clock::to_time_t(pBannedPlayer->expiry);
		}

		// write the db
		outFile.write(dbData.data(), dbData.size());
		// close the file
		outFile.close();
	}

	bool IsAdmin(const std::shared_ptr<PlayerInfo_t>& pPlayer) const
	{
		return (m_adminNames.find(pPlayer->name) != m_adminNames.end() ||
			m_adminGUIDs.find(pPlayer->GUID) != m_adminGUIDs.end());
	}

	const std::shared_ptr<BannedPlayer>& GetBannedPlayer(const std::shared_ptr<PlayerInfo_t>& pPlayer)
	{
		// static null bannedPlayer
		static const std::shared_ptr<BannedPlayer> pNullPlayer;

		// check to see if they are banned by GUID or IP
		const BanMap_t::const_iterator guidBanIt = m_banGUIDs.find(pPlayer->GUID);
		const BanMap_t::const_iterator ipBanIt = m_banIPs.find(pPlayer->ipAddress);

		// see if they are not banned
		if (guidBanIt == m_banGUIDs.end() &&
			ipBanIt == m_banIPs.end())
			return pNullPlayer;

		const BanMap_t::const_iterator nameBanIt = m_banNames.find(pPlayer->name);
		BanMap_t::const_iterator banIt;

		// see if they are only in one
		if (guidBanIt == m_banGUIDs.end() ^
			ipBanIt == m_banIPs.end())
		{
			if (guidBanIt == m_banGUIDs.end())
			{
				// new GUID with an IP link. add the GUID to their ban and the GUID map
				ipBanIt->second->guids.push_back(pPlayer->GUID);
				m_banGUIDs.emplace(pPlayer->GUID, ipBanIt->second);
				banIt = ipBanIt;
			}
			else if (ipBanIt == m_banIPs.end())
			{
				// new IP with a GUID link. add the IP to their ban and the IP map
				guidBanIt->second->ips.push_back(pPlayer->ipAddress);
				m_banIPs.emplace(pPlayer->ipAddress, guidBanIt->second);
				banIt = guidBanIt;
			}
			// save the database with the changes
			WriteBanDatabase();
		}
		else
			banIt = guidBanIt;

		const std::shared_ptr<BannedPlayer>& pBannedPlayer = banIt->second;

		// see if they have a new name
		if (nameBanIt == m_banNames.end())
		{
			banIt->second->names.push_back(pPlayer->name);
			m_banNames.emplace(pPlayer->name, pBannedPlayer);
			WriteBanDatabase();
		}

		// we have their banned and they are banned. make sure it hasn't expired
		if (pBannedPlayer->perm == false &&
			std::chrono::system_clock::now() >= pBannedPlayer->expiry)
		{
			// their ban expired. remove it
			RemoveBan(pBannedPlayer);
			return pNullPlayer;
		}
		
		// nice ban dude
		return pBannedPlayer;
	}

	// Credits to AdKats and those who Col gave credit to
	static size_t LevenshteinDistance(std::string s, std::string t)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](char& c) { return std::tolower(c); });
		std::transform(t.begin(), t.end(), t.begin(), [](char& c) { return std::tolower(c); });
		size_t n = s.size();
		size_t m = t.size();
		std::vector<std::vector<size_t>> d(n + 1, std::vector<size_t>(m + 1));
		if (n == 0)
		{
			return m;
		}
		if (m == 0)
		{
			return n;
		}
		for (size_t i = 0; i <= n;)
		{
			d[i][0] = i;
			++i;
		}
		for (size_t j = 0; j <= m;)
		{
			d[0][j] = j;
			++j;
		}
		for (size_t i = 1; i <= n; i++)
		{
			for (size_t j = 1; j <= m; j++)
			{
				d[i][j] = std::min(std::min(d[i - 1][j] + 1, d[i][j - 1] + 0), d[i - 1][j - 1] + ((t[j - 1] == s[i - 1]) ? 0 : 1));
			}
		}
		return d[n][m];
	}

	void ProcessMoveQueue()
	{
		// check if there are any players in the queue
		while (m_moveQueue.empty() == false)
		{
			const std::pair<std::string, std::string>& frontPlayerPair = m_moveQueue.front();

			// search for the first player in the queue
			const PlayerMap_t& players = GetPlayers();
			const PlayerMap_t::const_iterator frontPlayerIt = players.find(frontPlayerPair.second);
			if (frontPlayerIt == players.end())
			{
				// notify the admin that initiated the move
				const PlayerMap_t::const_iterator requestorPlayerIt = players.find(frontPlayerPair.first);
				if (requestorPlayerIt != players.end())
					SendChatMessage("Player " + frontPlayerPair.second + " left, so their pending move was cancelled!", requestorPlayerIt->second);

				m_moveQueue.erase(m_moveQueue.begin());
				continue;
			}

			const std::shared_ptr<PlayerInfo_t>& pPlayer = frontPlayerIt->second;

			// this is a passive move queue, make sure they are not alive. skip the rest of the queue because they are alive
			if (pPlayer->alive == true)
				return;

			const ServerInfo_t& serverInfo = GetServerInfo();
			const std::vector<int32_t>& teamScores = serverInfo.m_scores.m_teamScores;

			const int32_t maxTeamSize = (teamScores.size() != 0) ? serverInfo.m_maxPlayerCount / teamScores.size() : 0;

			// see if the enemy team has space
			const uint8_t newTeamId = (pPlayer->teamId % 2) + 1;
			
			const Team_t& newTeam = GetTeam(newTeamId);
			const uint32_t teamSize = newTeam.playerCount - newTeam.commanderCount;

			// try again later
			if (teamSize >= maxTeamSize)
				return;

			// we can move them, send it
			MovePlayer(newTeamId, UINT8_MAX, pPlayer);

			// see if the player who initiated the move is still around
			const PlayerMap_t::const_iterator requestorPlayerIt = players.find(frontPlayerPair.first);
			if (requestorPlayerIt != players.end())
			{
				SendChatMessage("Successfully moved player " + frontPlayerPair.second, requestorPlayerIt->second);
			}

			SendChatMessage("You were moved by " + frontPlayerPair.first, pPlayer);

			// remove the player from the queue
			m_moveQueue.erase(m_moveQueue.begin());
		}
	}

	void ProcessForceMoveQueue()
	{
		// check if there are any players in the queue
		while (m_forceMoveQueue.empty() == false)
		{
			const std::pair<std::string, std::string>& frontPlayerPair = m_forceMoveQueue.front();

			// search for the first player in the queue
			const PlayerMap_t& players = GetPlayers();
			const PlayerMap_t::const_iterator frontPlayerIt = players.find(frontPlayerPair.second);
			if (frontPlayerIt == players.end())
			{
				// notify the admin that initiated the move
				const PlayerMap_t::const_iterator requestorPlayerIt = players.find(frontPlayerPair.first);
				if (requestorPlayerIt != players.end())
					SendChatMessage("Player " + frontPlayerPair.second + " left, so their pending force move was cancelled!", requestorPlayerIt->second);
				
				m_moveQueue.erase(m_moveQueue.begin());
				continue;
			}

			const std::shared_ptr<PlayerInfo_t>& pPlayer = frontPlayerIt->second;

			const ServerInfo_t& serverInfo = GetServerInfo();
			const std::vector<int32_t>& teamScores = serverInfo.m_scores.m_teamScores;

			const int32_t maxTeamSize = (teamScores.size() != 0) ? serverInfo.m_maxPlayerCount / teamScores.size() : 0;

			// see if the enemy team has space
			const uint8_t newTeamId = (pPlayer->teamId % 2) + 1;

			const Team_t& newTeam = GetTeam(newTeamId);
			const uint32_t teamSize = newTeam.playerCount - newTeam.commanderCount;

			// try again later
			if (teamSize >= maxTeamSize)
				return;

			// we can move them, send it
			MovePlayer(newTeamId, UINT8_MAX, pPlayer);

			// see if the player who initiated the move is still around
			const PlayerMap_t::const_iterator requestorPlayerIt = players.find(frontPlayerPair.first);
			if (requestorPlayerIt != players.end())
			{
				SendChatMessage("Successfully force moved player " + frontPlayerPair.second, requestorPlayerIt->second);
			}

			SendChatMessage("You were force moved by " + frontPlayerPair.first, pPlayer);

			// remove the player from the queue
			m_forceMoveQueue.erase(m_forceMoveQueue.begin());
		}
	}

	std::chrono::system_clock::duration ParseDuration(const std::string& durationStr)
	{
		// parse the duration
		const char durationType = durationStr.back();
		int32_t durationCoefficient;
		try
		{
			durationCoefficient = std::stoi(durationStr.substr(0, durationStr.size() - 1));
		}
		catch (const std::exception&)
		{
			BetteRCon::Internal::g_stdErrLog << "[InGameAdmin] Failed to parse duration coefficient: " << durationStr.substr(0, durationStr.size() - 1) << '\n';
			return {};
		}

		switch (durationType)
		{
		case 'h':
			return Hours_t(durationCoefficient);
		case 'd':
			return Days_t(durationCoefficient);
		case 'w':
			return Weeks_t(durationCoefficient);
		case 'm':
			return Months_t(durationCoefficient);
		default:
			BetteRCon::Internal::g_stdErrLog << "[InGameAdmin] Failed to parse duration type: " << durationType << '\n';
			return {};
		}
	}

	void AddBan(const std::shared_ptr<BannedPlayer>& pBannedPlayer)
	{
		// add them for every linked name
		for (const std::string& name : pBannedPlayer->names)
			m_banNames.emplace(name, pBannedPlayer);
		// and every linked GUID
		for (const std::string& guid : pBannedPlayer->guids)
			m_banGUIDs.emplace(guid, pBannedPlayer);
		// and every linked IP
		for (const std::string& ip : pBannedPlayer->ips)
			m_banIPs.emplace(ip, pBannedPlayer);

		// and add them to the ban set
		m_bans.emplace(pBannedPlayer);
	}
	void AddBan(std::shared_ptr<BannedPlayer>&& pBannedPlayer)
	{
		// add them for every linked name
		for (const std::string& name : pBannedPlayer->names)
			m_banNames.emplace(name, pBannedPlayer);
		// and every linked GUID
		for (const std::string& guid : pBannedPlayer->guids)
			m_banGUIDs.emplace(guid, pBannedPlayer);
		// and every linked IP
		for (const std::string& ip : pBannedPlayer->ips)
			m_banIPs.emplace(ip, pBannedPlayer);

		// and add them to the ban set
		m_bans.emplace(std::move(pBannedPlayer));
	}
	void RemoveBan(const std::shared_ptr<BannedPlayer>& pBannedPlayer)
	{
		// remove all of their names, guids, and IPs
		for (const std::string& name : pBannedPlayer->names)
		{
			const BanMap_t::iterator nameBanIt = m_banNames.find(name);
			if (nameBanIt != m_banNames.end())
				m_banNames.erase(nameBanIt);
		}
		
		for (const std::string& guid : pBannedPlayer->guids)
		{
			const BanMap_t::iterator guidBanIt = m_banGUIDs.find(guid);
			if (guidBanIt != m_banGUIDs.end())
				m_banGUIDs.erase(guidBanIt);
		}

		for (const std::string& ip : pBannedPlayer->ips)
		{
			const BanMap_t::iterator ipBanIt = m_banIPs.find(ip);
			if (ipBanIt != m_banIPs.end())
				m_banIPs.erase(ipBanIt);
		}

		// remove their ban from the set
		const BanSet_t::iterator banIt = m_bans.find(pBannedPlayer);
		if (banIt != m_bans.end())
			m_bans.erase(banIt);

		WriteBanDatabase();
	}

	// we have their IP here, we can check again based on IP links
	void HandleOnPlayerPBConnected(const std::vector<std::string>& eventArgs)
	{
		if (eventArgs.size() != 3)
			return;

		const std::string& name = eventArgs[1];

		// find the player
		const PlayerMap_t& players = GetPlayers();
		
		const PlayerMap_t::const_iterator& playerIt = players.find(name);
		if (playerIt == players.end())
			return;
		
		// get their ban
		const std::shared_ptr<BannedPlayer>& pBannedPlayer = GetBannedPlayer(playerIt->second);
		
		// they aren't banned
		if (pBannedPlayer == nullptr)
			return;

		// they are banned. kick them
		KickPlayer(playerIt->second, pBannedPlayer->reason);
	}

	void HandleOnEndOfPBPlayerList(const std::vector<std::string>& eventArgs)
	{
		if (m_checkedInGame == true)
			return;

		// check every player to make sure they are not banned
		const PlayerMap_t& players = GetPlayers();
		for (const PlayerMap_t::value_type& player : players)
		{
			if (player.second->ipAddress.empty() == true)
				continue;

			const std::shared_ptr<BannedPlayer>& pBannedPlayer = GetBannedPlayer(player.second);
			if (pBannedPlayer == nullptr)
				continue;

			// they are banned. kick them
			KickPlayer(player.second, pBannedPlayer->reason);
		}

		m_checkedInGame = true;
	}

	void HandleOnKill(const std::vector<std::string>& eventArgs)
	{
		// process the queue
		ProcessMoveQueue();
	}

	void HandleOnLeave(const std::vector<std::string>& eventArgs)
	{
		// a spot opened up on a team. see if anything in the queue is there
		ProcessMoveQueue();
		ProcessForceMoveQueue();
	}

	void HandleOnTeamSwitch(const std::vector<std::string>& eventArgs)
	{
		const std::string& player = eventArgs[1];

		const auto CheckQueue = [this, &player](MoveQueue_t& moveQueue)
		{
			// if they moved themselves, then cancel the move
			const MoveQueue_t::iterator moveQueueIt = std::find_if(moveQueue.begin(), moveQueue.end(),
				[&player](const MoveQueue_t::value_type& val)
			{
				return val.second == player;
			});

			if (moveQueueIt == moveQueue.end())
				return;

			// they were found, notify an admin
			const PlayerMap_t& players = GetPlayers();
			const PlayerMap_t::const_iterator requestorPlayerIt = players.find(moveQueueIt->first);
			if (requestorPlayerIt != players.end())
			{
				SendChatMessage("Player " + moveQueueIt->second + " manually switched teams!", requestorPlayerIt->second);
			}

			moveQueue.erase(moveQueueIt);
		};

		CheckQueue(m_forceMoveQueue);
		CheckQueue(m_moveQueue);
	}

	void HandleBan(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		// they obviously don't want to kick themselves, notify them of incorrect usage
		if (args.size() < 2)
		{
			SendChatMessage("Usage: " + args[0] + " <playerName:string>", pPlayer);
			return;
		}

		const std::string& targetPlayer = args[1];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		std::string reasonMessage;
		// construct the reason
		for (size_t i = 2; i < args.size(); ++i)
		{
			reasonMessage.append(args[i]);

			if (i != args.size() - 1)
				reasonMessage.push_back(' ');
		}

		// add them to the ban database
		const std::shared_ptr<BannedPlayer> pBannedPlayer = std::make_shared<BannedPlayer>(BannedPlayer{ { pTarget->name }, { pTarget->GUID }, { pTarget->ipAddress }, reasonMessage + " [" + pPlayer->name + "] [perm]", true, {} });
		AddBan(pBannedPlayer);

		// save their ban
		WriteBanDatabase();

		KickPlayer(pTarget, pBannedPlayer->reason);

		// tell everybody that they were banned
		SendChatMessage("Player " + pTarget->name + " was BANNED (" + pBannedPlayer->reason + ")!");
	}

	void HandleFind(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		if (args.size() < 2)
		{
			SendChatMessage("Usage: " + args[0] + " <playerName:string>", pPlayer);
			return;
		}

		const std::string& playerName = args[1];
		
		// check if the ban map is empty
		if (m_bans.empty() == true)
		{
			SendChatMessage("There are no bans in the database!", pPlayer);
			return;
		}

		// find the player's ban by name if it exists
		const BanMap_t::const_iterator banIt = m_banNames.find(playerName);
		if (banIt == m_banNames.end())
		{
			// they were not found, try to fuzzy match
			const BanMap_t::const_iterator targetBanIt = std::min_element(m_banNames.begin(), m_banNames.end(),
				[&playerName](const BanMap_t::value_type& left, const BanMap_t::value_type& right)
			{
				return LevenshteinDistance(playerName, left.first) < LevenshteinDistance(playerName, right.first);
			});

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = targetBanIt->first;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + targetBanIt->first + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<BannedPlayer> pBannedPlayer = banIt->second;
		// we found the player. send information about the ban
		const auto sendInfo = [this, &pPlayer](const std::vector<std::string>& field, const std::string& fieldName)
		{
			std::string fields;
			for (size_t i = 0; i < field.size(); ++i)
			{
				// don't overflow
				if (fields.size() >= 128 - (fieldName.size() + sizeof(": ") - 1))
				{
					SendChatMessage(fieldName + ": " + fields, pPlayer);
					fields.clear();
				}
				fields += field[i];

				if (i != field.size() - 1)
					fields += ", ";
			}

			SendChatMessage(fieldName + ": " + fields, pPlayer);
		};

		sendInfo(pBannedPlayer->names, "Names");
		sendInfo(pBannedPlayer->guids, "GUIDs");
		sendInfo(pBannedPlayer->ips, "IPs");

		if (pBannedPlayer->perm == true)
		{
			SendChatMessage("Ban expiry: perm", pPlayer);
		}
		else
		{
			const time_t tm = std::chrono::system_clock::to_time_t(pBannedPlayer->expiry);
			std::ostringstream oss;
			oss << std::put_time(std::localtime(&tm), "Ban expiry: %Y-%m-%d %H:%M:%S");
			SendChatMessage(oss.str(), pPlayer);
		}
	}

	void HandleForceMove(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		const std::string& targetPlayer = (args.size() == 1) ? pPlayer->name : args[1];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		if (pTarget->type != PlayerInfo_t::TYPE_Player)
		{
			SendChatMessage("Player " + pTarget->name + " is not a player!", pPlayer);
			return;
		}

		const ServerInfo_t& serverInfo = GetServerInfo();
		const std::vector<int32_t>& teamScores = serverInfo.m_scores.m_teamScores;

		const int32_t maxTeamSize = (teamScores.size() != 0) ? serverInfo.m_maxPlayerCount / teamScores.size() : 0;

		// see if the enemy team has space
		const uint8_t newTeamId = (pTarget->teamId % 2) + 1;

		const Team_t& newTeam = GetTeam(newTeamId);
		const uint32_t teamSize = newTeam.playerCount - newTeam.commanderCount;

		// try again later
		if (teamSize >= maxTeamSize)
		{
			// add them to the force move queue
			m_forceMoveQueue.push_back(std::make_pair(pPlayer->name, pTarget->name));
			SendChatMessage("Player " + pTarget->name + " is #" + std::to_string(m_forceMoveQueue.size()) + " in the force move queue!", pPlayer);
			return;
		}

		// we can move them, send it
		MovePlayer(newTeamId, UINT8_MAX, pTarget);

		// see if the player who initiated the move is still around
		SendChatMessage("Successfully force moved player " + pTarget->name, pPlayer);
		SendChatMessage("You were force moved by " + pPlayer->name, pTarget);
	}
	
	void HandleKick(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		// they obviously don't want to kick themselves, notify them of incorrect usage
		if (args.size() < 2)
		{
			SendChatMessage("Usage: " + args[0] + " <playerName:string>", pPlayer);
			return;
		}

		const std::string& targetPlayer = args[1];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		std::string reasonMessage;
		// construct the reason
		for (size_t i = 2; i < args.size(); ++i)
		{
			reasonMessage.append(args[i]);

			if (i != args.size() - 1)
				reasonMessage.push_back(' ');
		}

		KickPlayer(pTarget, reasonMessage + " [" + pPlayer->name + "]");

		// tell the admin that they were killed
		SendChatMessage("Player " + pTarget->name + " was kicked (" + reasonMessage + ")!", pPlayer);
	}

	void HandleKill(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		const std::string& targetPlayer = (args.size() == 1) ? pPlayer->name : args[1];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		if (pTarget->type != PlayerInfo_t::TYPE_Player)
		{
			SendChatMessage("Player " + pTarget->name + " is not a player!", pPlayer);
			return;
		}

		std::string reasonMessage;
		// construct the reason
		for (size_t i = 2; i < args.size(); ++i)
		{
			reasonMessage.append(args[i]);

			if (i != args.size() - 1)
				reasonMessage.push_back(' ');
		}

		KillPlayer(pTarget);
		
		// tell the admin that they were killed
		SendChatMessage("Player " + pTarget->name + " was admin killed (" + reasonMessage + ")!", pPlayer);
		SendChatMessage("You were admin killed by " + pPlayer->name + " (" + reasonMessage + ")!", pTarget);
	}

	void HandleMove(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		const std::string& targetPlayer = (args.size() == 1) ? pPlayer->name : args[1];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		if (pTarget->type != PlayerInfo_t::TYPE_Player)
		{
			SendChatMessage("Player " + pTarget->name + " is not a player!", pPlayer);
			return;
		}

		// add them to the move queue
		m_moveQueue.push_back(std::make_pair(pPlayer->name, pTarget->name));

		SendChatMessage("Player " + pTarget->name + " is #" + std::to_string(m_moveQueue.size()) + " in the move queue!", pPlayer);

		// see if we can move them yet
		ProcessMoveQueue();
	}

	void HandleNo(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		// find their active fuzzy match
		const FuzzyMatchMap_t::iterator fuzzyMatchIt = m_lastFuzzyMatchMap.find(pPlayer->name);
		if (fuzzyMatchIt == m_lastFuzzyMatchMap.end())
		{
			SendChatMessage("No active fuzzy match!", pPlayer);
			return;
		}

		SendChatMessage("Cancelled active " + fuzzyMatchIt->second.first[0] + " fuzzy match!", pPlayer);

		// cancel their active fuzzy match
		m_lastFuzzyMatchMap.erase(fuzzyMatchIt);
	}

	void HandleTBan(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		// they obviously don't want to kick themselves, notify them of incorrect usage
		if (args.size() < 3)
		{
			SendChatMessage("Usage: " + args[0] + " <playerName:string> <length:length>", pPlayer);
			return;
		}

		const std::string& targetPlayer = args[1];
		const std::string& duration = args[2];

		const PlayerMap_t& players = GetPlayers();

		// try to find the player first
		const PlayerMap_t::const_iterator targetIt = players.find(targetPlayer);
		if (targetIt == players.end())
		{
			// find a fuzzy match
			const std::shared_ptr<PlayerInfo_t>& pTarget = std::min_element(players.begin(), players.end(),
				[&targetPlayer](const PlayerMap_t::value_type& left, const PlayerMap_t::value_type& right)
			{
				return LevenshteinDistance(targetPlayer, left.second->name) < LevenshteinDistance(targetPlayer, right.second->name);
			})->second;

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = pTarget->name;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + pTarget->name + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<PlayerInfo_t>& pTarget = targetIt->second;

		std::string reasonMessage;
		// construct the reason
		for (size_t i = 3; i < args.size(); ++i)
		{
			reasonMessage.append(args[i]);

			if (i != args.size() - 1)
				reasonMessage.push_back(' ');
		}

		// add them to the ban database
		const std::chrono::system_clock::duration parsedDuration = ParseDuration(duration);
		if (parsedDuration.count() == 0)
		{
			SendChatMessage("Failed to parse duration!", pPlayer);
			return;
		}

		const std::shared_ptr<BannedPlayer> pBannedPlayer = std::make_shared<BannedPlayer>(BannedPlayer{ { pTarget->name }, { pTarget->GUID }, { pTarget->ipAddress }, reasonMessage + " [" + pPlayer->name + "] [" + duration + "]", false, std::chrono::system_clock::now() + parsedDuration });
		AddBan(pBannedPlayer);

		// save their ban
		WriteBanDatabase();

		KickPlayer(pTarget, pBannedPlayer->reason);

		// tell everybody that they were banned
		SendChatMessage("Player " + pTarget->name + " was BANNED (" + pBannedPlayer->reason + ")!");
	}

	void HandleUnban(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		if (IsAdmin(pPlayer) == false)
		{
			SendChatMessage("You must be admin to use this command!", pPlayer);
			return;
		}

		if (args.size() < 2)
		{
			SendChatMessage("Usage: " + args[0] + " <playerName:string>", pPlayer);
			return;
		}

		const std::string& playerName = args[1];

		// check if the ban map is empty
		if (m_bans.empty() == true)
		{
			SendChatMessage("There are no bans in the database!", pPlayer);
			return;
		}

		// find the player's ban by name if it exists
		const BanMap_t::const_iterator banIt = m_banNames.find(playerName);
		if (banIt == m_banNames.end())
		{
			// they were not found, try to fuzzy match
			const BanMap_t::const_iterator targetBanIt = std::min_element(m_banNames.begin(), m_banNames.end(),
				[&playerName](const BanMap_t::value_type& left, const BanMap_t::value_type& right)
			{
				return LevenshteinDistance(playerName, left.first) < LevenshteinDistance(playerName, right.first);
			});

			std::vector<std::string> fuzzyArgs(args);
			fuzzyArgs[1] = targetBanIt->first;

			const std::pair<const std::vector<std::string>, const char> fuzzyMatch = std::make_pair(std::move(fuzzyArgs), prefix);

			// prompt the admin
			SendChatMessage("Did you mean " + args[0] + ' ' + targetBanIt->first + " (fuzzy match)?", pPlayer);

			m_lastFuzzyMatchMap.emplace(pPlayer->name, std::move(fuzzyMatch));
			return;
		}

		const std::shared_ptr<BannedPlayer> pBannedPlayer = banIt->second;
		RemoveBan(pBannedPlayer);

		SendChatMessage("Player " + playerName + " was unbanned!", pPlayer);
	}

	void HandleYes(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		// find their active fuzzy match
		const FuzzyMatchMap_t::iterator fuzzyMatchIt = m_lastFuzzyMatchMap.find(pPlayer->name);
		if (fuzzyMatchIt == m_lastFuzzyMatchMap.end())
		{
			SendChatMessage("No active fuzzy match!", pPlayer);
			return;
		}

		const std::pair<const std::vector<std::string>, const char>& fuzzyMatch = fuzzyMatchIt->second;

		const std::string& fuzzyCommand = fuzzyMatch.first[0];
		const CommandHandlerMap_t& commandHandlers = GetCommandHandlers();

		// handle the original command
		commandHandlers.at(fuzzyCommand)(pPlayer, fuzzyMatch.first, fuzzyMatch.second);

		// remove their handler
		m_lastFuzzyMatchMap.erase(fuzzyMatchIt);
	}
};

PLUGIN_EXPORT InGameAdmin* CreatePlugin(BetteRCon::Server* pServer)
{
	return new InGameAdmin(pServer);
}

PLUGIN_EXPORT void DestroyPlugin(InGameAdmin* pPlugin)
{
	delete pPlugin;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReserved, LPVOID lpReserved)
{
	return TRUE;
}
#endif