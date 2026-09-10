#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/config/config.h"

#include "interfaces/cs2menus/ics2menus.h"

AdminMenuBridge g_AdminMenus;

// How long an admin menu stays on screen before timing out (seconds).
static constexpr float kMenuDuration = 30.0f;

AdminMenuBridge::AdminMenuBridge() : m_menus(CS2MENUS_INTERFACE) {}

void AdminMenuBridge::Init()
{
	Refresh();
}

void AdminMenuBridge::Refresh()
{
	switch (m_menus.Refresh())
	{
		case mmu::BridgeChange::Unloaded:
			// Handles belonged to the unloaded instance.
			for (int i = 0; i <= MAXPLAYERS; i++)
			{
				m_extHandle[i] = kInvalidMenuHandle;
			}
			MMU_LOG_WARN("mm-cs2menus unloaded - admin menus disabled.\n");
			break;
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2menus found - menus enabled.\n");
			break;
		case mmu::BridgeChange::Unchanged:
			break;
	}
}

void AdminMenuBridge::Shutdown()
{
	if (m_menus)
	{
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			if (m_extHandle[i] != kInvalidMenuHandle)
			{
				m_menus->CancelMenu(i);
			}
		}
	}

	m_menus.Shutdown();
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_extHandle[i] = kInvalidMenuHandle;
	}
}

bool AdminMenuBridge::Available() const
{
	return m_menus.Available();
}

void AdminMenuBridge::CancelMenu(int slot)
{
	if (m_menus && slot >= 0 && slot <= MAXPLAYERS)
	{
		m_menus->CancelMenu(slot);
	}
}

bool AdminMenuBridge::EatsChatInput(int slot) const
{
	if (!m_menus || slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	// Ask for the resolved type rather than the menu's own:
	// "default" defers to the menu plugin's config, so only the per-viewer answer says what's on screen.
	return m_menus->HasMenu(slot) && m_menus->GetActiveMenuType(slot) == MenuType::Chat;
}

bool AdminMenuBridge::ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect)
{
	if (!m_menus || slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	// Echo each item's info tag back to the caller so flow code doesn't track indices itself.
	std::vector<std::string> infos;
	infos.reserve(items.size());
	for (const auto &item : items)
	{
		infos.push_back(item.info);
	}

	MenuHandle h = m_menus->CreateMenu(g_CS2AConfig.menu.Type(), title,
									   [onSelect, infos](MenuHandle, int s, int item)
									   {
										   if (onSelect && item >= 0 && item < static_cast<int>(infos.size()))
										   {
											   onSelect(s, item, infos[item]);
										   }
									   });
	if (h == kInvalidMenuHandle)
	{
		return false;
	}

	for (const auto &item : items)
	{
		m_menus->AddItem(h, item.text.c_str(), item.info.c_str(), item.disabled);
	}
	m_menus->SetExitButton(h, true);
	m_menus->SetCloseOnSelect(h, true);

	g_CS2AConfig.menu.ApplyKeys(m_menus.Get(), h);

	// One-shot: free the menu when its display ends, and forget the handle.
	m_menus->SetMenuEndCallback(h,
								[this](MenuHandle menu, int s, MenuEndReason)
								{
									if (s >= 0 && s <= MAXPLAYERS && m_extHandle[s] == menu)
									{
										m_extHandle[s] = kInvalidMenuHandle;
									}
									if (m_menus)
									{
										m_menus->DestroyMenu(menu);
									}
								});

	// Record before DisplayMenu: a chained ShowMenu replaces the current menu for
	// the slot and fires its end callback, which must not clear the handle we just set.
	m_extHandle[slot] = h;
	m_menus->DisplayMenu(h, slot, kMenuDuration);
	return true;
}
