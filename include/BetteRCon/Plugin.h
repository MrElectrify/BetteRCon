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
#define ENDPLUGINIMPL(name) ~name() {}									\
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
		void ScheduleAction(Server::TimedAction_t&& timedAction, const size_t millisecondsFromNow) { if (IsEnabled() == true) m_pServer->ScheduleAction(std::move(timedAction), millisecondsFromNow); }

		// If the plugin is enabled, attempts to send a command to the server, and calls recvCallback when the response is received.
		// RecvCallback_t must not block, as it is called from the worker thread
		void SendCommand(const std::vector<std::string>& command, Server::RecvCallback_t&& recvCallback) { if (IsEnabled() == true) m_pServer->SendCommand(command, std::move(recvCallback)); }
	private:
		bool m_enabled = false;

		HandlerMap_t m_eventHandlers;

		Server* m_pServer;
	};
}

#endif