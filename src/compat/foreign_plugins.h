#ifndef _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_
#define _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_

#include <ISmmPlugin.h>

class ICS2KZ;

// Detects other plugins that write the same things we do.
//
// Chat and the clan tag are separate questions with separate answers, because the plugins that take them do so under different conditions.
// cs2kz-metamod renders chat when its overridePlayerChat config option is on, and writes the clan tag when its
// kz_profile_clantag_enabled convar is on (older builds lack that convar and always write it). Either can be off
// while the other is on, so a server can have us own chat while cs2kz keeps the scoreboard, or the reverse.
//
// The convar is normally set in server.cfg, which runs after AllPluginsLoaded,
// so a global convar change callback re-runs detection when it flips instead of trusting the value seen at load.
class CS2AForeignPlugins
{
public:
	void Init();
	void Shutdown();

	// Re-run detection. Call once everything is loaded, whenever a plugin loads or unloads, and on config reload.
	// unloading is the plugin going away, whose interface must not be picked up again while it still answers MetaFactory.
	void Refresh(PluginId unloading = 0);

	// cs2kz's public interface, or null when cs2kz is absent or predates ICS2KZ001.
	ICS2KZ *CS2KZ() const
	{
		return m_cs2kz;
	}

	// Plugin currently rendering player chat, or null when that's ours to take.
	const char *ChatOwner() const
	{
		return m_chatOwner;
	}

	// Plugin currently writing the scoreboard clan tag, or null.
	const char *ClanTagOwner() const
	{
		return m_clanTagOwner;
	}

private:
	const char *m_chatOwner = nullptr;
	const char *m_clanTagOwner = nullptr;
	ICS2KZ *m_cs2kz = nullptr;
};

extern CS2AForeignPlugins g_CS2AForeignPlugins;

#endif // _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_
