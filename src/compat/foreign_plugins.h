#ifndef _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_
#define _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_

// Detects other plugins that write the same things we do.
//
// Chat and the clan tag are separate questions with separate answers, because the plugins that take them do so under different conditions.
// cs2kz-metamod renders chat only when its own overridePlayerChat option is on,
// but it writes the clan tag unconditionally, so a server can have us own chat while it owns the tag.
class CS2AForeignPlugins
{
public:
	// Re-run detection. Call once everything is loaded, whenever a plugin loads or unloads, and on config reload.
	void Refresh();

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
};

extern CS2AForeignPlugins g_CS2AForeignPlugins;

#endif // _INCLUDE_ADMIN_FOREIGN_PLUGINS_H_
