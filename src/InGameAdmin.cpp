#include <BetteRCon/Plugin.h>

// STL
#include <functional>
#include <fstream>
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
	using AdminSet_t = std::unordered_set<std::string>;
	using ChatHandlerMap_t = std::unordered_map<std::string, std::function<void(const std::string& playerName, const std::vector<std::string>& args)>>;
	using PlayerInfo = BetteRCon::Server::PlayerInfo;
	using PlayerMap_t = BetteRCon::Server::PlayerMap_t;
	using SquadMap_t = BetteRCon::Server::SquadMap_t;
	using Team_t = BetteRCon::Server::Team;

	InGameAdmin(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// read the player information flatfile database
		ReadAdminDatabase();

		// register the test command
		RegisterCommand("test", std::bind(&InGameAdmin::HandleTest, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "InGameAdmin"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.0"; }

	void ReadAdminDatabase()
	{
		// try to open the database
		std::ifstream inFile("plugins/Admins.cfg");
		if (inFile.good() == false)
			return;

		// read each admin
		std::string adminName;
		while (inFile >> adminName)
			m_admins.emplace(std::move(adminName));

		inFile.close();
	}

	void WriteAdminDatabase()
	{
		// open the output file
		std::ofstream outFile("plugins/Admins.cfg");
		if (outFile.good() == false)
			return;

		// write each admin
		for (const AdminSet_t::value_type& adminName : m_admins)
			outFile << adminName;

		outFile.close();
	}

	void HandleTest(const std::shared_ptr<PlayerInfo>& pPlayer, const std::vector<std::string>& args, const char prefix)
	{
		// move them to the other team and squad
		const uint8_t newTeamId = (pPlayer->teamId % 2) + 1;

		// we are good to switch them. let's do it
		MovePlayer(newTeamId, UINT8_MAX, pPlayer);

		SendChatMessage(std::string("Prefix: ") + prefix, pPlayer);
	}

	virtual ~InGameAdmin() {}
private:
	AdminSet_t m_admins;
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