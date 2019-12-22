#include <BetteRCon/Plugin.h>

// STL
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

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
	using VIPMap_t = std::unordered_map<std::string, std::shared_ptr<VIP>>;

	PendingVIPMap_t m_pendingVIPs;
	VIPMap_t m_VIPNames;
	VIPMap_t m_VIPGUIDs;

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

			// see if they are already a VIP
			const VIPMap_t::iterator vipIt = m_VIPNames.find(name);
			if (vipIt != m_VIPNames.end())
			{
				// we are updating an existing VIP. add the time
				vipIt->second->expiry += ParseDuration(duration);

				// check to make sure they have not expired
				if (std::chrono::system_clock::now() > vipIt->second->expiry)
				{
					// don't forget about their guid
					const VIPMap_t::iterator vipGUIDIt = m_VIPGUIDs.find(vipIt->second->eaguid);
					if (vipGUIDIt != m_VIPGUIDs.end())
						m_VIPGUIDs.erase(vipGUIDIt);

					m_VIPNames.erase(vipIt);
				}
				continue;
			}

			// check to see if the player is in-game already
			const PlayerMap_t players = GetPlayers();

			const PlayerMap_t::const_iterator playerIt = players.find(name);
			if (playerIt != players.end())
			{
				// the player is in-game. give them VIP status
				const std::shared_ptr<PlayerInfo_t> pPlayer = playerIt->second;

				const std::shared_ptr<VIP> pVIP = std::make_shared<VIP>(VIP{ pPlayer->name, pPlayer->GUID, std::chrono::system_clock::now() + ParseDuration(duration) });

				m_VIPNames.emplace(name, pVIP);
				m_VIPGUIDs.emplace(std::move(name), pVIP);
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

	void ReadVIPDatabase() {}
	void WriteVIPDatabase() 
	{
		// open the output file
		std::ofstream outFile("plugins/VIPs.cfg", std::ios::binary);
		if (outFile.good() == false)
			return;

		// write the number of VIPs
		std::vector<char> dbData(sizeof(uint32_t));

		*reinterpret_cast<uint32_t*>(&dbData[0]) = m_VIPNames.size();

		for (const VIPMap_t::value_type& vip : m_VIPNames)
		{
			const std::shared_ptr<VIP>& pPlayer = vip.second;
			const std::string& name = pPlayer->name;

			// write the name's length
			dbData.push_back(name.size() % 0xff);

			// write the string
			dbData.insert(dbData.end(), name.begin(), name.end());

			const std::string& guid = pPlayer->eaguid;
			// write the guid's length
			dbData.push_back(guid.size() % 0xff);

			// write the guid
			dbData.insert(dbData.end(), guid.begin(), guid.end());

			// write the expiry time
			dbData.resize(dbData.size() + sizeof(uint64_t));
			*reinterpret_cast<uint64_t*>(&dbData[dbData.size() - sizeof(uint64_t)]) = pPlayer->expiry.time_since_epoch().count();
		}

		outFile.write(dbData.data(), dbData.size());
		outFile.close();
	}
public:
	VIPManager(BetteRCon::Server* pServer)
		: Plugin(pServer) {}

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