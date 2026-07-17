#ifndef _INCLUDE_ADMIN_MENU_BRIDGE_H_
#define _INCLUDE_ADMIN_MENU_BRIDGE_H_

// Optional integration with the mm-cs2menus plugin (ICS2Menus).
// There is no built-in menu backend, this bridge only wraps the external plugin.

#include "src/common.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ICS2Menus;

struct AdminMenuItem
{
	std::string text;
	std::string info; // opaque tag echoed back on select (e.g. a "$<steamid64>")
	bool disabled = false;
};

class AdminMenuBridge
{
public:
	// Fired when a player picks an item: (slot, itemIndex, item's info tag).
	using SelectFn = std::function<void(int slot, int item, const std::string &info)>;

	// Acquire the ICS2Menus interface. Call from AllPluginsLoaded().
	void Init();
	// Re-resolve the interface. Call from OnPluginLoad / OnPluginUnload.
	void Refresh();
	// Cancel anything we displayed and drop the pointer. Call from Unload().
	void Shutdown();

	// True when the external menu plugin is available.
	bool Available() const;

	// Display a one-shot menu to slot.
	// No-op (returns false) when menus are unavailable.
	// Chain another ShowMenu from onSelect to build multi-step flows.
	bool ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect);

	// Close whatever menu the slot has open.
	void CancelMenu(int slot);

	// True when cs2menus is consuming this player's say input to drive an open chat menu.
	// Anything else hooking say has to leave those messages alone,
	// since cs2menus suppresses them and they were never meant as chat.
	bool EatsChatInput(int slot) const;

private:
	ICS2Menus *m_pMenus = nullptr;
	// External menu handle currently displayed to each slot (0 = none).
	uint32_t m_extHandle[MAXPLAYERS + 1] = {};
};

extern AdminMenuBridge g_AdminMenus;

#endif // _INCLUDE_ADMIN_MENU_BRIDGE_H_
