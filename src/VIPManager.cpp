#include <BetteRCon/Plugin.h>

// STL
#include <chrono>
#include <fstream>
#include <memory>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
// Windows stuff
#include <Windows.h>
#endif

// A set of in-game commands including admin commands.
class VIPManager : public BetteRCon::Plugin
{
private:
	struct VIP
	{
		std::string name;
		std::string eaguid;
		std::chrono::system_clock::time_point expiry;
	};

	using Hours_t = std::chrono::hours;
	using Days_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<24>, Hours_t::period>>;
	using Weeks_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<7>, Days_t::period>>;
	using Months_t = std::chrono::duration<int, std::ratio_multiply<std::ratio<30>, Days_t::period>>;

	using PlayerInfo_t = BetteRCon::Server::PlayerInfo;
	using PlayerMap_t = BetteRCon::Server::PlayerMap_t;
	using PendingVIPMap_t = std::unordered_map<std::string, std::string>;
	using VIPMap_t = std::unordered_map<std::string, VIP>;

	PendingVIPMap_t m_pendingVIPs;
	// may have some duplicates
	VIPMap_t m_VIPs;

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
			BetteRCon::Internal::g_stdErrLog << "[VIPManager] Failed to parse duration coefficient: " << durationStr.substr(0, durationStr.size() - 1) << '\n';
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
			BetteRCon::Internal::g_stdErrLog << "[VIPManager] Failed to parse duration type: " << durationType << '\n';
			return {};
		}
	}

	void ReadPendingVIPDatabase()
	{
		// try to open the database
		std::ifstream inFile("plugins/PendingVIPs.cfg");
		if (inFile.good() == false)
			return;

		// read each VIP with their duration
		std::string pendingVIPLine;
		while (inFile >> pendingVIPLine)
		{
			const size_t comma = pendingVIPLine.find(',');
			if (comma == std::string::npos)
			{
				BetteRCon::Internal::g_stdErrLog << "[VIPManager] Failed to find comma for pending VIP\n";
				inFile.close();
				Disable();
				return;
			}

			// split the name and duration up, and add it to our admin maps
			std::string name = pendingVIPLine.substr(0, comma);
			std::string duration = pendingVIPLine.substr(comma + 1);

			// check to see if the player is in-game already
			const PlayerMap_t players = GetPlayers();

			const PlayerMap_t::const_iterator playerIt = players.find(name);
			if (playerIt != players.end())
			{
				// the player is in-game. give them VIP status
				const std::shared_ptr<PlayerInfo_t> pPlayer = playerIt->second;
				m_VIPs.emplace(name, VIP{ pPlayer->name, pPlayer->GUID, std::chrono::system_clock::now() + ParseDuration(duration) });
				continue;
			}

			// create a new pending VIP with the duration
			m_pendingVIPs.emplace(std::move(name), duration);
		}

		inFile.close();

		// write the database to show changes
		WritePendingVIPDatabase();
		WriteVIPDatabase();
	}
	void WritePendingVIPDatabase() 
	{
		// open the output file
		std::ofstream outFile("plugins/PendingVIPs.cfg");
		if (outFile.good() == false)
			return;

		// write each admin
		for (const PendingVIPMap_t::value_type& pendingVIP : m_pendingVIPs)
			outFile << pendingVIP.first << ',' << pendingVIP.second << '\n';

		outFile.close();
	}

	void ReadVIPDatabase() 
	{
		// open the input file
		std::ifstream inFile("plugins/VIPs.cfg", std::ios::binary);
		if (inFile.good() == false)
			return;

		// read the file into a vector
		std::vector<char> dbData(std::istreambuf_iterator<char>(inFile), {});

		// empty DB
		if (dbData.size() == 0)
			return;

		const uint32_t dbSize = *reinterpret_cast<uint32_t*>(&dbData[0]);

		size_t offset = sizeof(uint32_t);
		for (size_t i = 0; i < dbSize; ++i)
		{
			// naive bounds checking
			if (offset >= dbData.size())
			{
				BetteRCon::Internal::g_stdErrLog << "[VIPManager] Invalid VIP DB\n";
				return;
			}

			// read the name
			const uint8_t nameLen = *reinterpret_cast<uint8_t*>(&dbData[offset]);
			++offset;

			std::string name(&dbData[offset], nameLen);
			offset += nameLen;

			// read the guid
			const uint8_t guidLen = *reinterpret_cast<uint8_t*>(&dbData[offset]);
			++offset;

			std::string guid(&dbData[offset], guidLen);
			offset += guidLen;

			// read the time point
			std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(*reinterpret_cast<time_t*>(&dbData[offset]));
			offset += sizeof(time_t);

			// make sure they are not expired
			if (std::chrono::system_clock::now() >= tp)
				continue;

			m_VIPs.emplace(guid, VIP{ std::move(name), guid, tp });
		}
		
		inFile.close();
	}
	void WriteVIPDatabase() 
	{
		// open the output file
		std::ofstream outFile("plugins/VIPs.cfg", std::ios::binary);
		if (outFile.good() == false)
			return;

		// write the number of VIPs
		std::vector<char> dbData(sizeof(uint32_t));

		*reinterpret_cast<uint32_t*>(&dbData[0]) = m_VIPs.size();

		for (const VIPMap_t::value_type& vipPair : m_VIPs)
		{
			const VIP& VIP = vipPair.second;
			const std::string& name = VIP.name;

			// write the name's length
			dbData.push_back(name.size() % 0xff);

			// write the string
			dbData.insert(dbData.end(), name.begin(), name.end());

			const std::string& guid = VIP.eaguid;
			// write the guid's length
			dbData.push_back(guid.size() % 0xff);

			// write the guid
			dbData.insert(dbData.end(), guid.begin(), guid.end());

			// write the expiry time
			dbData.resize(dbData.size() + sizeof(time_t));
			*reinterpret_cast<time_t*>(&dbData[dbData.size() - sizeof(time_t)]) = std::chrono::system_clock::to_time_t(VIP.expiry);
		}

		outFile.write(dbData.data(), dbData.size());
		outFile.close();
	}

	bool IsVIP(const std::shared_ptr<PlayerInfo_t>& pPlayer)
	{
		// first see if they are not in either DB
		const VIPMap_t::iterator vipIt = m_VIPs.find(pPlayer->name);
		if (vipIt == m_VIPs.end())
			return false;

		// see if their VIP status expired
		if (std::chrono::system_clock::now() >= vipIt->second.expiry)
		{
			// remove them
			m_VIPs.erase(vipIt);
			return false;
		}

		return true;
	}

	void HandleKillMe(const std::shared_ptr<PlayerInfo_t>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{

	}
public:
	VIPManager(BetteRCon::Server* pServer)
		: Plugin(pServer) 
	{
		// register VIP commands
		RegisterCommand("killme", std::bind(&VIPManager::HandleKillMe, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "VIPManager"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.0"; }

	virtual void Enable() { Plugin::Enable(); ReadVIPDatabase(); ReadPendingVIPDatabase(); }

	virtual void Disable() { Plugin::Disable(); WriteVIPDatabase(); WritePendingVIPDatabase(); }

	virtual ~VIPManager() {}
};

PLUGIN_EXPORT VIPManager* CreatePlugin(BetteRCon::Server* pServer)
{
	return new VIPManager(pServer);
}

PLUGIN_EXPORT void DestroyPlugin(VIPManager* pPlugin)
{
	delete pPlugin;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReserved, LPVOID lpReserved)
{
	return TRUE;
}
#endif