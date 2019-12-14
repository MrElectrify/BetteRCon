#ifndef BETTERCON_PLUGIN_H_
#define BETTERCON_PLUGIN_H_

/*
 *	Plugin
 *	11/12/19 16:22
 */

 // BetteRCon
#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Log.h>

// PreProcessor Macros
#ifdef _WIN32
#define PLUGIN_EXPORT __declspec(dllexport) extern "C"
#elif __GNUC__
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define BEGINPLUGIN(name) class name : public BetteRCon::Plugin			\
{																		\
public:
#define ENDPLUGINIMPL(name) virtual ~name() {}							\
};																		\
PLUGIN_EXPORT name* CreatePlugin(BetteRCon::Server* pServer)			\
{																		\
	return new name(pServer);											\
}																		\
PLUGIN_EXPORT void DestroyPlugin(name* pPlugin)							\
{																		\
	delete pPlugin;														\
}

#ifdef _WIN32
#include <Windows.h>
#define ENDPLUGIN(name) ENDPLUGINIMPL(name)								\
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)	\
{																		\
	return TRUE;														\
}
#elif __linux__
#define ENDPLUGIN(name) ENDPLUGINIMPL(name)
#endif

#define CREATEPLUGIN(name) name(BetteRCon::Server* pServer) : Plugin(pServer)

#define AUTHORPLUGIN(authorName) virtual std::string_view GetPluginAuthor() { return authorName; }
#define NAMEPLUGIN(name) virtual std::string_view GetPluginName() { return name; }
#define VERSIONPLUGIN(version) virtual std::string_view GetPluginVersion() { return version; }

// STL
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BetteRCon
{
	// All plugins inherit from this interface. Plugins should register all event handlers from the constructor
	class Plugin
	{
	public: 
		using EventHandler_t = std::function<void(const std::vector<std::string>& eventWords)>;
		using HandlerMap_t = std::unordered_map<std::string, EventHandler_t>;
		using Worker_t = asio::io_context;

		// Creates a plugin with the server
		Plugin(Server* pServer) : m_pServer(pServer) {}

		// Returns the name of the plugin's author
		virtual std::string_view GetPluginAuthor() = 0;
		// Returns the name of the plugin
		virtual std::string_view GetPluginName() = 0;
		// Returns the version of the plugin
		virtual std::string_view GetPluginVersion() = 0;

		// Enables a plugin. BetteRCon will start calling handlers from this point
		virtual void Enable() { m_enabled = true; }
		// Disables a plugin. BetteRCon will stop calling handlers from this point
		virtual void Disable() { m_enabled = false; }

		// Retreives whether or not the plugin should be enabled
		const bool IsEnabled() const { return m_enabled == true; }

		// Retreives all of the event handlers. Used internally by BetteRCon
		const HandlerMap_t& GetEventHandlers() const { return m_eventHandlers; }

		// Registers the desired handler to be called every time an event is fired
		void RegisterHandler(const std::string& eventName, EventHandler_t&& eventHandler) { m_eventHandlers.emplace(eventName, eventHandler); }

		// If the plugin is enabled, schedules an action in the milliseconds from now
		void ScheduleAction(Server::TimedAction_t&& timedAction, const size_t millisecondsFromNow) { m_pServer->ScheduleAction([this, timedAction = std::move(timedAction)]{ if (IsEnabled() == true) timedAction(); }, millisecondsFromNow); }

		// If the plugin is enabled, attempts to send a command to the server, and calls recvCallback when the response is received.
		// RecvCallback_t must not block, as it is called from the worker thread
		void SendCommand(const std::vector<std::string>& command, Server::RecvCallback_t&& recvCallback) { if (IsEnabled() == true) m_pServer->SendCommand(command, std::move(recvCallback)); }

		// Gets server info such as name, teams
		const Server::ServerInfo& GetServerInfo() const noexcept { return m_pServer->GetServerInfo(); }
		// Gets server players
		const Server::PlayerMap_t& GetPlayers() const noexcept { return m_pServer->GetPlayers(); }
		// Gets team map
		const Server::TeamMap_t& GetTeams() const noexcept { return m_pServer->GetTeams(); }
		// Gets team squads
		const Server::SquadMap_t& GetSquadMap(const uint8_t teamId) const noexcept { return m_pServer->GetSquadMap(teamId); }
		// Gets squad players
		const Server::PlayerMap_t& GetSquadPlayers(const uint8_t teamId, const uint8_t squadId) const noexcept { return m_pServer->GetSquadPlayers(teamId, squadId); }

		// Sends a chat message to everybody, of max 128 characters
		void SendChatMessage(const std::string& message) { SendCommand({ "admin.say", message, "all" }, {}); }
		// Sends a chat message to a player, of max 128 characters
		void SendChatMessage(const std::string& message, const std::shared_ptr<Server::PlayerInfo>& pPlayer) { SendCommand({ "admin.say", message, "player", pPlayer->name }, {}); }
		// Sends a chat message to a squad, of max 128 characters
		void SendChatMessage(const std::string& message, const uint8_t teamId, const uint8_t squadId) { SendCommand({ "admin.say", message, "squad", std::to_string(teamId), std::to_string(squadId) }, {}); }
		// Sends a chat message to a team, of max 128 characters
		void SendChatMessage(const std::string& message, const uint8_t teamId) { SendCommand({ "admin.say", message, "team", std::to_string(teamId) }, {}); }
		// Sends a chat message to a set of players, of max 128 characters
		void SendChatMessage(const std::string& message, const std::vector<const std::shared_ptr<Server::PlayerInfo>>& players) { for (const auto& pPlayer : players) SendChatMessage(message, pPlayer); }
	private:
		bool m_enabled = false;

		HandlerMap_t m_eventHandlers;

		Server* m_pServer;
	};
}

#endif