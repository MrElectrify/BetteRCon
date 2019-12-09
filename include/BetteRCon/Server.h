#ifndef BETTERCON_SERVER_H_
#define BETTERCON_SERVER_H_

/*
 *	BetteRCon Server Main
 *	10/9/19 01:14
 */

 // BetteRCon
#include <BetteRCon/Internal/Connection.h>

// STL
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#define HMOD HMODULE
#elif __linux__
#define HMOD void*
#endif

namespace BetteRCon
{
	class Plugin;

	/*
	 *	BetteRConServer is a class signifying a connection to a Battlefield server.
	 *	It encompasses the connection itself, the thread, layer, player management,
	 *	and plugins.
	 */
	class Server
	{
	public:
		enum LoginResult
		{
			LoginResult_OK,					// Success
			LoginResult_PasswordNotSet,		// Password was not set by the server
			LoginResult_InvalidPasswordHash,// Password was incorrect
			LoginResult_Unknown,			// Socket-related error, or unknown response. More information can be retreived by GetLastErrorCode()
			LoginResult_Count
		};

		static const std::string s_LoginResultStr[LoginResult_Count];

		struct ServerInfo
		{
			std::string m_serverName;
			int32_t m_playerCount;
			int32_t m_maxPlayerCount;
			std::string m_gameMode;
			std::string m_map;
			int32_t m_roundsPlayed;
			int32_t m_roundsTotal;
			struct Scores
			{
				std::vector<int32_t> m_teamScores;
				int32_t m_goalScore;
			} m_scores;
			std::string m_onlineState;
			bool m_ranked;
			bool m_punkBuster;
			bool m_hasPassword;
			int32_t m_serverUpTime;
			int32_t m_roundTime;
			std::string m_serverIpAndPort;
			std::string m_punkBusterVersion;
			bool m_joinQueueEnabled;
			std::string m_region;
			std::string m_closestPingSite;
			std::string m_country;
			bool m_matchmakingEnabled;
			int32_t m_blazePlayerCount;
			std::string m_blazeGameState;
		};
		struct PlayerInfo
		{
			std::string name;
			std::string GUID;
			uint8_t teamId;
			uint8_t squadId;
			uint32_t kills;
			uint32_t deaths;
			uint32_t score;
			uint8_t rank;
			uint16_t ping;
			uint16_t type;
			std::string ipAddress;
			uint16_t port;
			std::chrono::system_clock::time_point firstSeen;
		};

		using Connection_t = Internal::Connection;
		using Endpoint_t = Connection_t::Endpoint_t;
		using ErrorCode_t = Connection_t::ErrorCode_t;
		using DisconnectCallback_t = std::function<void(const ErrorCode_t& ec)>;
		using EventCallback_t = std::function<void(const std::vector<std::string>& eventArgs)>;
		using LoginCallback_t = std::function<void(const LoginResult result)>;
		using Packet_t = Internal::Packet;
		using PlayerMap_t = std::unordered_map<std::string, std::shared_ptr<PlayerInfo>>;
		// unordered map of teams, with val of unordered map of squads, with val of unordered map of playernames, with val of playerInfo ptr
		using SquadMap_t = std::unordered_map<uint8_t, PlayerMap_t>;
		using TeamMap_t = std::unordered_map<uint8_t, SquadMap_t>;
		using PlayerInfoCallback_t = std::function<void(const PlayerMap_t& players, const TeamMap_t& teams)>;
		// success is always true when load is false. failReason is only populated if success is false
		using PluginCallback_t = std::function<void(const std::string& pluginName, const bool load, const bool success, const std::string& failReason)>;
		using RecvCallback_t = std::function<void(const ErrorCode_t& ec, const std::vector<std::string>& response)>;
		using ServerInfoCallback_t = std::function<void(const ServerInfo& info)>;
		using TimedAction_t = std::function<void()>;
		using Worker_t = Connection_t::Worker_t;
		// Default constructor. Creates thread
		Server();

		// Attempts to connect to a server. Throws ErrorCode_t on error
		void Connect(const Endpoint_t& endpoint);
		// Attempts to connect to a server. Returns ErrorCode_t in on error
		void Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept;

		// Attempts to login to the server using a hashed password, and begins the serverInfo/playerInfo loop on success. 
		// Calls disconnectCallback when the server disconnects, pluginCallback when a plugin (un)loads or fails to load, 
		// eventCallback for every event, loginCallback on completion with the result, and saves serverInfoCallback and playerInfoCallback
		void Login(const std::string& password, LoginCallback_t&& loginCallback, DisconnectCallback_t&& disconnectCallback, PluginCallback_t&& pluginCallback, EventCallback_t&& eventCallback, ServerInfoCallback_t&& serverInfoCallback, PlayerInfoCallback_t&& playerInfoCallback);

		// Attempts to disconnect from an active server. Throws ErrorCode_t on error
		void Disconnect();
		// Attempts to disconnect from an active server. Returns ErrorCode_t in ec error
		void Disconnect(ErrorCode_t& ec) noexcept;

		// Returns whether or not we are connected
		bool IsConnected() const noexcept;

		// Gets server info
		const ServerInfo& GetServerInfo() const noexcept;

		// Gets the last error code, which will tell why the server disconnected if it did
		ErrorCode_t GetLastErrorCode() const noexcept;

		// Attempts to send a command to the server, and calls recvCallback when the response is received.
		// RecvCallback_t must not block, as it is called from the worker thread
		void SendCommand(const std::vector<std::string>& command, RecvCallback_t&& recvCallback);

		// Registers a callback that will be called any time an event is received
		void RegisterCallback(const std::string& eventName, EventCallback_t&& eventCallback);

		// Enables a plugin by name. Returns true on success
		bool EnablePlugin(const std::string& pluginName);

		// Disables a plugin by name. Returns true on success
		bool DisablePlugin(const std::string& pluginName);

		// Schedules an action to be executed in the future
		void ScheduleAction(TimedAction_t&& timedAction, const std::chrono::system_clock::duration& timeFromNow);
		// Schedules an action to be executed in the future
		void ScheduleAction(TimedAction_t&& timedAction, const size_t millisecondsFromNow);

		~Server();
	private:
		void SendResponse(const std::vector<std::string>& response, const int32_t sequence);

		void HandleEvent(const ErrorCode_t& ec, std::shared_ptr<Packet_t> event);
		void HandleLoginRecvHash(const ErrorCode_t& ec, const std::vector<std::string>& response, const std::string& password, const LoginCallback_t& loginCallback);
		void HandleLoginRecvResponse(const ErrorCode_t& ec, const std::vector<std::string>& response, const LoginCallback_t& loginCallback);
		
		void HandleOnAuthenticated(const std::vector<std::string>& eventArgs);
		void HandleOnLeave(const std::vector<std::string>& eventArgs);
		void HandleOnTeamChange(const std::vector<std::string>& eventArgs);
		void HandleOnSquadChange(const std::vector<std::string>& eventArgs);

		using PluginDestructor_t = std::add_pointer_t<void(Plugin*)>;
		using PluginFactory_t = std::add_pointer_t<Plugin*(Server*)>;

		void LoadPlugins();

		static int32_t s_lastSequence;

		Worker_t m_worker;
		Connection_t m_connection;
		std::thread m_thread;

		std::unordered_multimap<std::string, EventCallback_t> m_eventCallbacks;

		// server info
		ServerInfo m_serverInfo;
		asio::steady_timer m_serverInfoTimer;

		void HandleServerInfo(const ErrorCode_t& ec, const std::vector<std::string>& serverInfo);
		void HandleServerInfoTimerExpire(const ErrorCode_t& ec);

		// callbacks
		DisconnectCallback_t m_disconnectCallback;
		EventCallback_t m_eventCallback;
		PluginCallback_t m_pluginCallback;
		ServerInfoCallback_t m_serverInfoCallback;
		PlayerInfoCallback_t m_playerInfoCallback;
		
		// plugins
		struct PluginInfo
		{
			HMOD hPluginModule;
			Plugin* pPlugin;
			PluginDestructor_t pDestructor;
		};
		std::unordered_map<std::string, PluginInfo> m_plugins;

		// player info
		// we store as shared_ptrs with redundancy because we want fast accessing of teams and squads, as well as easy traversal of all players
		PlayerMap_t m_players;
		TeamMap_t m_teams;
		asio::steady_timer m_playerInfoTimer;

		void HandlePlayerList(const ErrorCode_t& ec, const std::vector<std::string>& playerList);
		void HandlePlayerListTimerExpire(const ErrorCode_t& ec);
	};
}

#endif