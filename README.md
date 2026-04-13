# CS2 Admin

Metamod Admin plugin for CS2 with SM style configuration and SB++ support.

Shared maplist with [cs2-rockthevote](https://github.com/FemboyKZ/cs2-rockthevote).

## API

Other Metamod: Source plugins can access public methods found in [ics2admin.h](/src/public/ics2admin.h) and [forwards.h](/src/public/forwards.h)

```cpp
#include <public/ics2admin.h>

// In AllPluginsLoaded or later:
ICS2Admin *admin = (ICS2Admin *)g_SMAPI->MetaFactory(
    CS2ADMIN_INTERFACE, nullptr, nullptr);

if (admin && admin->HasFlag(slot, CS2ADMIN_FLAG_BAN))
    // player can ban
```

```cpp
#include <public/forwards.h>

ICS2AdminForwards *fwd = (ICS2AdminForwards *)g_SMAPI->MetaFactory(
    CS2ADMIN_FORWARDS_INTERFACE, nullptr, nullptr);
```

## Installation

### Requirements

* CS2 Server (Recommended to use [SteamRT3 Docker](https://gitlab.steamos.cloud/steamrt/sniper/sdk/-/blob/steamrt/sniper/README.md)
* [MetaMod: Source 2.0](https://www.metamodsource.net/downloads.php/?branch=master)
* [sql_mm](https://github.com/zer0k-z/sql_mm)
* (Optional) MySQL Database
* (Optional) [SB++ Web Panel](https://github.com/sbpp/sourcebans-pp) (Tested only on 1.8+)

### How to

1. Get the latest [release](https://github.com/FemboyKZ/mm-cs2admin/releases) for your operating system (linux/windows).
2. Extract the contents of the release archive in your server's root folder ``/game/csgo/``.
3. Configure the main cfg in ``/game/csgo/cfg/cs2admin/core.cfg`` with database credentials and other preferences.
4. Configure Admins in ``admins.cfg`` or ``admins.ini``, Admin Groups in ``adming_groups.cfg`` and Admin overrides in ``admin_overrides.cfg``
    (these can be directly copied from existing SM cfg with the same names)
5. Create a new file called ``maplist.txt`` or rename the bundled ``maplist.example.txt`` file to it, then add your maps to the file.

The cs2admin plugin mirrors SourceMod's Admin configuration structure completely.
This means the following articles are valid here too:

* [Adding Admins](https://wiki.alliedmods.net/Adding_Admins_(SourceMod))
* [Adding Groups](https://wiki.alliedmods.net/Adding_Groups_(SourceMod))
* [Overriding Commands](https://wiki.alliedmods.net/Overriding_Command_Access_(SourceMod))

## Usage

### Commands

#### Chat Commands (in-game)

| Command | Usage | Permissions | Description |
| --- | --- | --- | --- |
| `!ban` | `!ban <target> <time> [reason]` | `d` (Ban) | Ban a connected player. Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!unban` | `!unban <steamid>` | `e` (Unban) | Unban a player by SteamID. |
| `!banip` | `!banip <ip> <time> [reason]` | `d` (Ban) | Ban an IP address. Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!kick` | `!kick <target> [reason]` | `c` (Kick) | Kick a connected player from the server. |
| `!slay` | `!slay <target>` | `f` (Slay) | Slay a player. Supports multi-target selectors (@all, @t, @ct, etc.). |
| `!mute` | `!mute <target> <time> [reason]` | `j` (Chat) | Mute a player (block voice). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!unmute` | `!unmute <target>` | `j` (Chat) | Unmute a player. |
| `!gag` | `!gag <target> <time> [reason]` | `j` (Chat) | Gag a player (block text chat). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!ungag` | `!ungag <target>` | `j` (Chat) | Ungag a player. |
| `!silence` | `!silence <target> <time> [reason]` | `j` (Chat) | Silence a player (mute + gag). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!unsilence` | `!unsilence <target>` | `j` (Chat) | Unsilence a player (unmute + ungag). |
| `!comms` | `!comms [target]` | `j` (Chat) | Show comm restriction status for a player (defaults to self). |
| `!listbans` | `!listbans <target>` | `d` (Ban) | List active bans for a connected player. |
| `!listcomms` | `!listcomms <target>` | `j` (Chat) | List active comm restrictions for a connected player. |
| `!who` | `!who` | `b` (Generic) | List all online admins with their flags, group, and immunity level. |
| `!listdc` | `!listdc` | `d` (Ban) | Show recently disconnected players (name, SteamID, IP, time ago). |
| `!give` | `!give <target> <weapon>` | `n` (Cheats) | Give a weapon to player(s). Auto-prepends `weapon_` if missing. Supports multi-target. |
| `!strip` | `!strip <target>` | `n` (Cheats) | Strip all weapons from player(s). Supports multi-target selectors. |
| `!rcon` | `!rcon <command>` | `m` (RCON) | Execute a server console command. |
| `!map` | `!map <mapname\|workshopid>` | `g` (Changemap) | Change the current map. Supports partial name match from maplist or raw workshop IDs. |
| `!maps` | `!maps [page]` | `g` (Changemap) | List available maps from the maplist (paginated). |
| `!pm` | `!pm <target> <message>` | `j` (Chat) | Private message a player. Echoes to all online admins. |
| `!entfire` | `!entfire <entity> <input> [value]` | `n` (Cheats) | Fire an input on a map entity via `ent_fire`. |
| `!report` | `!report <target> <reason>` | None | Report a player to online admins. Subject to cooldown. |
| `!adminhelp` | `!adminhelp [page]` | None | List all available commands (paginated). |
| `!find` | `!find <text>` | None | Search commands by name. |

#### Console / RCON Commands

| Command | Usage | Description |
| --- | --- | --- |
| `mm_reload` | `mm_reload` | Reload config and admin cache, re-verify all connected players. |
| `mm_rehash` | `mm_rehash` | Rebuild admin cache from database and flat files. |
| `cs2admin_version` | `cs2admin_version` | Display the loaded CS2Admin version. |
| `sc_fw_block` | `sc_fw_block <authid> <time> <type> <reason>` | (Web panel RCON) Apply a mute (type 1) or gag (type 2) to a connected player. |
| `sc_fw_unmute` | `sc_fw_unmute <authid>` | (Web panel RCON) Unmute a connected player. |
| `sc_fw_ungag` | `sc_fw_ungag <authid>` | (Web panel RCON) Ungag a connected player. |

#### Target Selectors

Commands that accept a `<target>` support the following selectors:

| Selector | Description |
| --- | --- |
| `@me` | Yourself |
| `@all` | All players |
| `@t` | All Terrorists |
| `@ct` | All Counter-Terrorists |
| `@spec` | All Spectators |
| `@dead` | All dead players |
| `@alive` | All alive players |
| `@random` | A random player |
| `@bot` | All bots |
| `@human` | All human players |
| `$STEAM_0:X:X` | Target by SteamID (Steam2, Steam3, or SteamID64) |
| `&name` | Target by exact name |
| `#slot` | Target by player slot number |
| `name` | Partial name match |

#### Duration Suffixes

Time values for ban/mute/gag commands support optional suffixes:

| Suffix | Meaning | Example |
| --- | --- | --- |
| *(none)* | Minutes (default) | `60` = 60 minutes |
| `h` | Hours | `2h` = 2 hours |
| `d` | Days | `7d` = 7 days |
| `w` | Weeks | `2w` = 2 weeks |
| `m` | Months (30 days) | `1m` = 30 days |

## Build

### Prerequisites

* This repository is cloned recursively (ie. has submodules)
* [python3](https://www.python.org/)
* [ambuild](https://github.com/alliedmodders/ambuild), make sure ``ambuild`` command is available via the ``PATH`` environment variable;
* MSVC (VS build tools)/Clang installed for Windows/Linux.

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

## Credits

* [SourceBans++](https://github.com/sbpp/sourcebans-pp)
* [SourceMod](https://github.com/alliedmodders/sourcemod)
* [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
* [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)

## Contributing

Feel free to create PRs or issues regarding anything, but this is very much just for fkz and I will not implement bloat like funcommands.
