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

// Assist allows players to assist the losing team if they are unable to switch manually. ONLY FOR CONQUEST LARGE RIGHT NOW
class InGameAdmin : public BetteRCon::Plugin
{
public:
	using AdminSet_t = std::unordered_set<std::string>;
	using ChatHandlerMap_t = std::unordered_map<std::string, std::function<void(const std::vector<std::string>& args)>>;

	InGameAdmin(BetteRCon::Server* pServer)
		: Plugin(pServer)
	{
		// read the player information flatfile database
		ReadAdminDatabase();

		// listen for chat messages
		RegisterHandler("player.onChat", std::bind(&InGameAdmin::HandleOnChat, this, std::placeholders::_1));
	}

	virtual std::string_view GetPluginAuthor() const { return "MrElectrify"; }
	virtual std::string_view GetPluginName() const { return "InGameAdmin"; }
	virtual std::string_view GetPluginVersion() const { return "v1.0.0"; }

	virtual void Enable()
	{
		Plugin::Enable();
		BetteRCon::Internal::g_stdOutLog << "[InGameAdmin]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

	virtual void Disable()
	{
		Plugin::Disable();
		BetteRCon::Internal::g_stdOutLog << "[InGameAdmin]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n';
	}

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

	void HandleOnChat(const std::vector<std::string>& eventMessage)
	{
		const std::string& chatMessage = eventMessage[2];

		// make sure it isn't an empty chat message
		if (chatMessage.empty() == true)
			return;

		size_t offset = 0;
		// remove the slash if there is one
		if (chatMessage[0] == '/')
			offset = 1;

		// all commands begin with !
		if (chatMessage[offset] != '!')
			return;

		// split up their message by spaces
		std::vector<std::string> commandArgs;
		while (size_t space = chatMessage.find(' ', offset) != std::string::npos)
		{
			commandArgs.emplace_back(chatMessage.substr(offset, space - offset));
			offset = space + 1;
		}

		// see if a handler exists for the command
		const ChatHandlerMap_t::const_iterator chatHandlerIt = m_chatHandlers.find(commandArgs[0]);
		if (chatHandlerIt == m_chatHandlers.end())
			return;

		// call the handler
		chatHandlerIt->second(commandArgs);
	}

	virtual ~InGameAdmin() {}
private:
	AdminSet_t m_admins;
	ChatHandlerMap_t m_chatHandlers;
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