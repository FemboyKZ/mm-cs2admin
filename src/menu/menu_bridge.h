#ifndef _INCLUDE_ADMIN_MENU_BRIDGE_H_
#define _INCLUDE_ADMIN_MENU_BRIDGE_H_

// Optional integration with the mm-cs2menus plugin (ICS2Menus).
// There is no built-in menu backend, this bridge only wraps the external plugin.

#include "interfaces/cs2menus/menus_client.h"
#include "src/common.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AdminMenuItem
{
	std::string text;
	std::string info; // opaque tag echoed back on select (e.g. a "$<steamid64>")
	bool disabled = false;
	// Consecutive items with the same section share it. Empty for none.
	std::string section;
	// Grid tile icon, like "ak47".
	std::string image;
	std::string subtext;
	// Showcase: the wide button under the image on every tab. One per menu.
	bool pinned = false;
};

// One row of a form: a Choice over options, or a Toggle when options is empty.
struct AdminFormField
{
	std::string text;
	std::vector<std::string> options;
	int value = 0; // the selected option, or 0/1 for a toggle
};

// How ShowMenu and ShowForm put a menu up. Outside the bridge, clang rejects its initializers in a default argument there.
struct AdminMenuOptions
{
	// On top of the slot's current menu, which Back returns to. For every step after a flow's first.
	bool push = false;
	// Stays up after a pick, the caller closes it.
	bool keepOpen = false;
	// Panorama only. Grid and Showcase show sections as tabs.
	MenuLayout layout = MenuLayout::List;
	MenuTileSize tiles = MenuTileSize::Small;
	// In a showcase, beside the box otherwise. An addon image class like "ct_logo".
	std::string image;
	// Page labels skip map prefixes like "kz_", to match CS2AMapManager::GetSortedMaps.
	bool mapList = false;
	// Behind the panorama refresh button, shows the menu again in place.
	std::function<void(int slot)> rebuild;
};

class AdminMenuBridge
{
public:
	// Fired when a player picks an item: (slot, itemIndex, item's info tag).
	using SelectFn = std::function<void(int slot, int item, const std::string &info)>;
	// Fired when a form is confirmed, with each field's value in order.
	using ConfirmFn = std::function<void(int slot, const std::vector<int> &values)>;

	using Options = AdminMenuOptions;

	// Acquire the ICS2Menus interface. Call from AllPluginsLoaded().
	void Init();
	// Re-resolve the interface. Call from OnPluginLoad / OnPluginUnload.
	void Refresh();
	// Cancel anything we displayed and drop the pointer. Call from Unload().
	void Shutdown();

	// True when the external menu plugin is available.
	bool Available() const;

	// Display a menu to slot. No-op (returns false) when menus are unavailable.
	// Chain another ShowMenu with push from onSelect to build multi-step flows.
	bool ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect, const Options &options = {});

	// Fields set in place, then a confirmText row that fires onConfirm.
	bool ShowForm(int slot, const char *title, const std::vector<AdminFormField> &fields, const char *confirmText, ConfirmFn onConfirm,
				  const Options &options = {});

	// Close whatever menu the slot has open, its history with it.
	void CancelMenu(int slot);

	// Hide the slot's menu without ending it, while the admin types in chat. Another menu on the slot resumes it too.
	void Suspend(int slot);
	void Resume(int slot);

private:
	bool Present(int slot, MenuHandle h, const Options &options);

	// While a refresh button's rebuild runs, its menu goes in place of the old one.
	bool m_rebuilding[MAXPLAYERS + 1] = {};

	CS2MenusClient m_menus;
};

extern AdminMenuBridge g_AdminMenus;

#endif // _INCLUDE_ADMIN_MENU_BRIDGE_H_
