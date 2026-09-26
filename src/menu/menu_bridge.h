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
};

// One row of a form: a Choice over options, or a Toggle when options is empty.
struct AdminFormField
{
	std::string text;
	std::vector<std::string> options;
	int value = 0; // the selected option, or 0/1 for a toggle
};

class AdminMenuBridge
{
public:
	// Fired when a player picks an item: (slot, itemIndex, item's info tag).
	using SelectFn = std::function<void(int slot, int item, const std::string &info)>;
	// Fired when a form is confirmed, with each field's value in order.
	using ConfirmFn = std::function<void(int slot, const std::vector<int> &values)>;

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
	// mapList makes panorama page labels skip map prefixes like "kz_", to match CS2AMapManager::GetSortedMaps.
	// grid shows panorama menus as image tiles.
	bool ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect, bool mapList = false, bool grid = false);

	// Fields set in place, then a confirmText row that fires onConfirm.
	bool ShowForm(int slot, const char *title, const std::vector<AdminFormField> &fields, const char *confirmText, ConfirmFn onConfirm);

	// Close whatever menu the slot has open.
	void CancelMenu(int slot);

private:
	bool Present(int slot, MenuHandle h);

	CS2MenusClient m_menus;
};

extern AdminMenuBridge g_AdminMenus;

#endif // _INCLUDE_ADMIN_MENU_BRIDGE_H_
