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
		using CommandHandler_t = std::function<void(const std::shared_ptr<Server::PlayerInfo>& pPlayer, const std::vector<std::string>& args, const char prefix)>;
		using CommandHandlerMap_t = std::unordered_map<std::string, CommandHandler_t>;
		using EventHandler_t = std::function<void(const std::vector<std::string>& eventWords)>;
		using EventHandlerMap_t = std::unordered_multimap<std::string, EventHandler_t>;
		using Worker_t = asio::io_context;

		// Creates a plugin with the server
		Plugin(Server* pServer) : m_pServer(pServer) {}

		// Returns the name of the plugin's author
		virtual std::string_view GetPluginAuthor() const = 0;
		// Returns the name of the plugin
		virtual std::string_view GetPluginName() const = 0;
		// Returns the version of the plugin
		virtual std::string_view GetPluginVersion() const = 0;

		// Enables a plugin. BetteRCon will start calling handlers from this point
		virtual void Enable() { m_enabled = true; BetteRCon::Internal::g_stdOutLog << "[" << GetPluginName() << "]: Enabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n'; }
		// Disables a plugin. BetteRCon will stop calling handlers from this point
		virtual void Disable() { m_enabled = false; BetteRCon::Internal::g_stdOutLog << "[" << GetPluginName() << "]: Disabled " << GetPluginName() << " version " << GetPluginVersion() << " by " << GetPluginAuthor() << '\n'; }

		// Retreives whether or not the plugin should be enabled
		const bool IsEnabled() const { return m_enabled == true; }

		// Retrieves all of the command handlers. Used internall by BetteRCon
		const CommandHandlerMap_t& GetCommandHandlers() const noexcept { return m_commandHandlers; }
		// Retrieves all of the event handlers. Used internally by BetteRCon
		const EventHandlerMap_t& GetEventHandlers() const noexcept { return m_eventHandlers; }

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
		// Gets all of the teams
		const Server::TeamMap_t& GetTeams() const noexcept { return m_pServer->GetTeams(); }
		// Gets a team
		const Server::Team& GetTeam(const uint8_t teamId) const noexcept { return m_pServer->GetTeam(teamId); }
		// Gets a squad
		const Server::PlayerMap_t& GetSquad(const uint8_t teamId, const uint8_t squadId) const noexcept { return m_pServer->GetSquad(teamId, squadId); }

		// Sends a chat message to everybody, of max 128 characters
		void SendChatMessage(const std::string& message) { SendCommand({ "admin.say", message, "all" }, [](const Server::ErrorCode_t&, const std::vector<std::string>&) {}); }
		// Sends a chat message to a player, of max 128 characters
		void SendChatMessage(const std::string& message, const std::shared_ptr<Server::PlayerInfo>& pPlayer) { SendCommand({ "admin.say", message, "player", pPlayer->name }, [](const Server::ErrorCode_t&, const std::vector<std::string>&) {}); }
		// Sends a chat message to a squad, of max 128 characters
		void SendChatMessage(const std::string& message, const uint8_t teamId, const uint8_t squadId) { SendCommand({ "admin.say", message, "squad", std::to_string(teamId), std::to_string(squadId) }, [](const Server::ErrorCode_t&, const std::vector<std::string>&) {}); }
		// Sends a chat message to a team, of max 128 characters
		void SendChatMessage(const std::string& message, const uint8_t teamId) { SendCommand({ "admin.say", message, "team", std::to_string(teamId) }, [](const Server::ErrorCode_t&, const std::vector<std::string>&) {}); }
		// Sends a chat message to a set of players, of max 128 characters
		void SendChatMessage(const std::string& message, const std::vector<std::shared_ptr<Server::PlayerInfo>>& players) { for (const auto& pPlayer : players) { SendChatMessage(message, pPlayer); } }

		// Moves a player by killing them if they are alive. If squadId is UINT8_MAX, find a suitable squad
		void MovePlayer(const uint8_t teamId, uint8_t squadId, const std::shared_ptr<Server::PlayerInfo>& pPlayer) 
		{ 
			if (squadId == UINT8_MAX)
			{
				// find them a squad
				const Server::Team& newTeam = GetTeam(teamId);

				constexpr size_t SQUAD_MAX = 5;

				for (const Server::SquadMap_t::value_type& squad : newTeam.squads)
				{
					if (squad.second.size() < SQUAD_MAX)
					{
						squadId = squad.first;
						break;
					}
				}

				// we didn't find a squad
				if (squadId == UINT8_MAX)
					squadId = 0;
			}

			m_pServer->MovePlayer(teamId, squadId, pPlayer); 
		}

		// Registers a command with a given handler
		void RegisterCommand(const std::string& commandName, CommandHandler_t&& commandHandler) { m_commandHandlers.emplace(commandName, std::move(commandHandler)); }
	private:
		bool m_enabled = false;

		CommandHandlerMap_t m_commandHandlers;
		EventHandlerMap_t m_eventHandlers;

		Server* m_pServer;
	};
}

#endif