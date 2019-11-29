#ifndef BETTERCON_PLUGIN_H_
#define BETTERCON_PLUGIN_H_

/*
 *	Plugin
 *	11/12/19 16:22
 */

// PreProcessor Macros
#ifdef _WIN32
#define BPLUGIN_EXPORT __declspec(dllexport) extern "C"
#elif __GNUC__
#define BPLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define BRBEGINPLUGIN(name) class name : public BetteRCon::Plugin		\
{

#ifdef _WIN32
#include <Windows.h>
#define BRENDPLUGINIMPL(name) };										\
BPLUGIN_EXPORT name* CreatePlugin()										\
{																		\
	return new name;													\
}																		\
BPLUGIN_EXPORT void DestroyPlugin(name* pPlugin)						\
{																		\
	delete pPlugin;														\
}
#define BRENDPLUGIN(name) BRENDPLUGINIMPL(name)							\
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)	\
{																		\
	return TRUE;														\
}
#elif __linux__
#define BRENDPLUGIN(name) BRENDPLUGINIMPL(name)
#endif

// STL
#include <functional>
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
	private:
		bool m_enabled = false;

		HandlerMap_t m_eventHandlers;
	};
}

#endif