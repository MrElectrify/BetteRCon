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
	struct Admin
	{
		std::string name;
		std::string guid;

		bool operator==(const Admin& rhs) const
		{
			return name == rhs.name &&
				guid == rhs.guid;
		}
	};
	class AdminHash
	{
	public:
		size_t operator()(const Admin& val) const
		{
			return std::hash<std::string>()(val.name) ^ std::hash<std::string>()(val.guid);
		}
	};
	using AdminSet_t = std::unordered_set<Admin, AdminHash>;
	using ChatHandlerMap_t = std::unordered_map<std::string, std::function<void(const std::string& playerName, const std::vector<std::string>& args)>>;
	using PlayerInfo_t = BetteRCon::Server::PlayerInfo;
	using PlayerMap_t = BetteRCon::Server::PlayerMap_t;
	using SquadMap_t = BetteRCon::Server::SquadMap_t;
	using Team_t = BetteRCon::Server::Team;

	InGameAdmin(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// read the player information flatfile database
		ReadAdminDatabase();

		// register the test command
		RegisterCommand("move", std::bind(&InGameAdmin::HandleMove, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "InGameAdmin"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.0"; }

	virtual void Enable() { Plugin::Enable(); ReadAdminDatabase(); }

	virtual void Disable() { Plugin::Disable(); WriteAdminDatabase(); }

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

			// split the name and guid up, and add it to our admin list
			m_admins.emplace(Admin{ adminLine.substr(0, comma), adminLine.substr(comma + 1) });

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
		for (const AdminSet_t::value_type& adminName : m_admins)
			outFile << adminName.name << ',' << adminName.guid;

		outFile.close();
	}

	void HandleMove(const std::shared_ptr<PlayerInfo_t> pPlayer, const std::vector<std::string>& args, const char prefix)
	{

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