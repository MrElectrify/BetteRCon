#ifndef BETTERCON_PLUGIN_H_
#define BETTERCON_PLUGIN_H_

/*
 *	Plugin
 *	11/12/19 16:22
 */

// PreProcessor Macros
#ifdef _WIN32
#define BPLUGIN_EXPORT __declspec(dllexport)
#elif __GNUC__
#define BPLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// STL
#include <functional>
#include <string>
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
		virtual std::string GetPluginAuthor() = 0;
		// Returns the name of the plugin
		virtual std::string GetPluginName() = 0;
		// Returns the version of the plugin
		virtual std::string GetPluginVersion() = 0;

		// Enables a plugin. BetteRCon will start calling handlers from this point
		virtual void Enable() { m_enabled = true; }
		// Disables a plugin. BetteRCon will stop calling handlers from this point
		virtual void Disable() { m_enabled = false; }

		// Retreives whether or not the plugin should be enabled
		const bool IsEnabled() const { return m_enabled == true; }

		// Retreives all of the event handlers. Used internally by BetteRCon
		const HandlerMap_t& GetEventHandlers() const { return m_eventHandlers; }
	private:
		bool m_enabled = false;

		HandlerMap_t m_eventHandlers;
	};
}

#endif