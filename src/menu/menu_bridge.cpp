#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/config/config.h"

#include "vendor/interfaces/ics2menus.h"

AdminMenuBridge g_AdminMenus;

// How long an admin menu stays on screen before timing out (seconds).
static constexpr float kMenuDuration = 30.0f;

// Map the admin config's MenuType to the plugin enum.
// "default" (and anything unknown) delegates the style choice to mm-cs2menus' own config.
static MenuType ConfiguredMenuType()
{
	const std::string &type = g_CS2AConfig.menuType;
	if (type == "chat")
	{
		return MenuType::Chat;
	}
	if (type == "html")
	{
		return MenuType::Html;
	}
	return MenuType::Default;
}

// Map a config key name to a MenuButton.
// "default" (and anything unknown) returns MenuButton::Default, which tells the menu plugin to use its own binding.
static MenuButton ParseMenuButton(const std::string &name)
{
	if (name == "none" || name == "off")
	{
		return MenuButton::None; // disable this action (single-key scroll)
	}
	if (name == "w" || name == "forward")
	{
		return MenuButton::W;
	}
	if (name == "s" || name == "back")
	{
		return MenuButton::S;
	}
	if (name == "a" || name == "left" || name == "moveleft")
	{
		return MenuButton::A;
	}
	if (name == "d" || name == "right" || name == "moveright")
	{
		return MenuButton::D;
	}
	if (name == "e" || name == "use" || name == "interact")
	{
		return MenuButton::Use;
	}
	if (name == "shift" || name == "speed" || name == "walk")
	{
		return MenuButton::Speed;
	}
	if (name == "ctrl" || name == "duck" || name == "crouch")
	{
		return MenuButton::Duck;
	}
	if (name == "space" || name == "jump")
	{
		return MenuButton::Jump;
	}
	if (name == "r" || name == "reload")
	{
		return MenuButton::Reload;
	}
	if (name == "mouse1" || name == "attack")
	{
		return MenuButton::Attack;
	}
	if (name == "mouse2" || name == "attack2")
	{
		return MenuButton::Attack2;
	}
	if (name == "tab" || name == "score")
	{
		return MenuButton::Score;
	}
	if (name == "f" || name == "inspect" || name == "lookatweapon")
	{
		return MenuButton::Inspect;
	}
	return MenuButton::Default; // "default" / unknown -> inherit menu plugin's key
}

void AdminMenuBridge::Init()
{
	Refresh();
}

void AdminMenuBridge::Refresh()
{
	ICS2Menus *prev = m_pMenus;
	ICS2Menus *now = nullptr;

	if (g_SMAPI)
	{
		now = static_cast<ICS2Menus *>(g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr));
	}

	// If the menu plugin unloaded, our handles belong to a now-dead instance.
	if (prev && !now)
	{
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			m_extHandle[i] = kInvalidMenuHandle;
		}
		MMU_LOG_WARN("mm-cs2menus unloaded - admin menus disabled.\n");
	}
	else if (!prev && now)
	{
		MMU_LOG_INFO("mm-cs2menus found - menus enabled.\n");
	}

	m_pMenus = now;
}

void AdminMenuBridge::Shutdown()
{
	if (m_pMenus)
	{
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			if (m_extHandle[i] != kInvalidMenuHandle)
			{
				m_pMenus->CancelMenu(i);
			}
		}
	}

	m_pMenus = nullptr;
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_extHandle[i] = kInvalidMenuHandle;
	}
}

bool AdminMenuBridge::Available() const
{
	return m_pMenus != nullptr;
}

void AdminMenuBridge::CancelMenu(int slot)
{
	if (m_pMenus && slot >= 0 && slot <= MAXPLAYERS)
	{
		m_pMenus->CancelMenu(slot);
	}
}

bool AdminMenuBridge::EatsChatInput(int slot) const
{
	if (!m_pMenus || slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	// Ask for the resolved type rather than the menu's own:
	// "default" defers to the menu plugin's config, so only the per-viewer answer says what's on screen.
	return m_pMenus->HasMenu(slot) && m_pMenus->GetActiveMenuType(slot) == MenuType::Chat;
}

bool AdminMenuBridge::ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect)
{
	if (!m_pMenus || slot < 0 || slot > MAXPLAYERS)
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

	MenuHandle h = m_pMenus->CreateMenu(ConfiguredMenuType(), title,
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
		m_pMenus->AddItem(h, item.text.c_str(), item.info.c_str(), item.disabled);
	}
	m_pMenus->SetExitButton(h, true);
	m_pMenus->SetCloseOnSelect(h, true);

	// Apply configured HTML nav-key overrides.
	// MenuButton::Default delegates back to the menu plugin's own binding.
	m_pMenus->SetMenuKey(h, MenuNavAction::Up, ParseMenuButton(g_CS2AConfig.menuNavUp));
	m_pMenus->SetMenuKey(h, MenuNavAction::Down, ParseMenuButton(g_CS2AConfig.menuNavDown));
	m_pMenus->SetMenuKey(h, MenuNavAction::Select, ParseMenuButton(g_CS2AConfig.menuNavSelect));
	m_pMenus->SetMenuKey(h, MenuNavAction::Back, ParseMenuButton(g_CS2AConfig.menuNavBack));

	// One-shot: free the menu when its display ends, and forget the handle.
	m_pMenus->SetMenuEndCallback(h,
								 [this](MenuHandle menu, int s, MenuEndReason)
								 {
									 if (s >= 0 && s <= MAXPLAYERS && m_extHandle[s] == menu)
									 {
										 m_extHandle[s] = kInvalidMenuHandle;
									 }
									 if (m_pMenus)
									 {
										 m_pMenus->DestroyMenu(menu);
									 }
								 });

	// Record before DisplayMenu: a chained ShowMenu replaces the current menu for
	// the slot and fires its end callback, which must not clear the handle we just set.
	m_extHandle[slot] = h;
	m_pMenus->DisplayMenu(h, slot, kMenuDuration);
	return true;
}
