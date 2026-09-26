#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/config/config.h"

AdminMenuBridge g_AdminMenus;

// How long an admin menu stays on screen before timing out (seconds).
static constexpr float kMenuDuration = 30.0f;

void AdminMenuBridge::Init()
{
	Refresh();
}

void AdminMenuBridge::Refresh()
{
	switch (m_menus.Refresh())
	{
		case mmu::BridgeChange::Unloaded:
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
	m_menus.Shutdown();
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

bool AdminMenuBridge::ShowMenu(int slot, const char *title, const std::vector<AdminMenuItem> &items, SelectFn onSelect, bool mapList, bool grid)
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

	const std::string *section = nullptr;
	for (const auto &item : items)
	{
		if (!item.section.empty() && (!section || *section != item.section))
		{
			m_menus->AddSection(h, item.section.c_str());
			section = &item.section;
		}
		int added = m_menus->AddItem(h, item.text.c_str(), item.info.c_str(), item.disabled);
		if (!item.image.empty())
		{
			m_menus->SetItemImage(h, added, item.image.c_str());
		}
		if (!item.subtext.empty())
		{
			m_menus->SetItemSubtext(h, added, item.subtext.c_str());
		}
	}
	if (grid)
	{
		m_menus->SetMenuLayout(h, MenuLayout::Grid);
	}
	if (mapList)
	{
		m_menus->SetMenuStyle(h, MenuStyle::PagePrefixDelimiter, "_");
	}
	return Present(slot, h);
}

bool AdminMenuBridge::ShowForm(int slot, const char *title, const std::vector<AdminFormField> &fields, const char *confirmText, ConfirmFn onConfirm)
{
	if (!m_menus || slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	const int confirmItem = static_cast<int>(fields.size());
	const int fieldCount = confirmItem;
	// Only the Confirm row selects, fields change in place.
	MenuHandle h = m_menus->CreateMenu(g_CS2AConfig.menu.Type(), title,
									   [this, onConfirm, confirmItem, fieldCount](MenuHandle menu, int s, int item)
									   {
										   if (item != confirmItem || !onConfirm || !m_menus)
										   {
											   return;
										   }
										   std::vector<int> values;
										   for (int f = 0; f < fieldCount; f++)
										   {
											   values.push_back(m_menus->GetItemValue(menu, f));
										   }
										   onConfirm(s, values);
									   });
	if (h == kInvalidMenuHandle)
	{
		return false;
	}

	for (const AdminFormField &field : fields)
	{
		if (field.options.empty())
		{
			m_menus->AddToggle(h, field.text.c_str(), field.value != 0, "");
			continue;
		}
		std::vector<const char *> options;
		for (const std::string &option : field.options)
		{
			options.push_back(option.c_str());
		}
		m_menus->AddChoice(h, field.text.c_str(), options.data(), static_cast<int>(options.size()), field.value, "");
	}
	m_menus->AddItem(h, confirmText, "", false);
	return Present(slot, h);
}

bool AdminMenuBridge::Present(int slot, MenuHandle h)
{
	m_menus->SetExitButton(h, true);
	m_menus->SetCloseOnSelect(h, true);
	g_CS2AConfig.menu.ApplyKeys(m_menus.Get(), h);
	return m_menus.Present(slot, h, kMenuDuration);
}
