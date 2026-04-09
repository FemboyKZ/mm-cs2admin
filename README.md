# CS2 Admin

Metamod Admin plugin for CS2 with SM style configuration and SB++ support.

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

* CS2 Server (Recommended to use SteamRT3 Docker)
* [MetaMod: Source 2.0](https://www.metamodsource.net/downloads.php/?branch=master)
* [sql_mm](https://github.com/zer0k-z/sql_mm)
* (Optional) MySQL Database
* (Optional) [SB++ Web Panel](https://github.com/sbpp/sourcebans-pp) (Tested only on 1.8+)

### How to

1. Get the latest [release](https://github.com/FemboyKZ/mm-cs2admin/releases) for your operating system (linux/windows).
2. Extract the contents of the release archive in your server's root folder `/game/csgo/`.
3. Configure the main cfg in `/game/csgo/cfg/cs2admin/core.cfg`.

## Usage

### Commands

#### Chat Commands (in-game)

| Command | Usage | Permissions | Description |
| --- | --- | --- | --- |
| `!ban` | `!ban <target> <time> [reason]` | `d` (Ban) | Ban a connected player. Time in minutes, 0 = permanent. |
| `!unban` | `!unban <steamid>` | `e` (Unban) | Unban a player by SteamID. |
| `!banip` | `!banip <ip> <time> [reason]` | `d` (Ban) | Ban an IP address. Time in minutes, 0 = permanent. |
| `!mute` | `!mute <target> <time> [reason]` | `j` (Chat) | Mute a player (block voice). Time in minutes, 0 = permanent. |
| `!unmute` | `!unmute <target>` | `j` (Chat) | Unmute a player. |
| `!gag` | `!gag <target> <time> [reason]` | `j` (Chat) | Gag a player (block text chat). Time in minutes, 0 = permanent. |
| `!ungag` | `!ungag <target>` | `j` (Chat) | Ungag a player. |
| `!silence` | `!silence <target> <time> [reason]` | `j` (Chat) | Silence a player (mute + gag). Time in minutes, 0 = permanent. |
| `!unsilence` | `!unsilence <target>` | `j` (Chat) | Unsilence a player (unmute + ungag). |
| `!comms` | `!comms [target]` | `j` (Chat) | Show comm restriction status for a player (defaults to self). |
| `!listbans` | `!listbans <target>` | `d` (Ban) | List active bans for a connected player. |
| `!listcomms` | `!listcomms <target>` | `j` (Chat) | List active comm restrictions for a connected player. |
| `!report` | `!report <target> <reason>` | None | Report a player to online admins. Subject to cooldown. |

#### Console / RCON Commands

| Command | Usage | Description |
| --- | --- | --- |
| `mm_ban` | `mm_ban <#userid\|name> <time> [reason]` | Ban a connected player from the server console. |
| `mm_addban` | `mm_addban <time> <steamid> [reason]` | Add an offline ban by SteamID. |
| `mm_unban` | `mm_unban <steamid>` | Unban a player by SteamID. |
| `mm_banip` | `mm_banip <ip> <time> [reason]` | Ban an IP address. |
| `mm_reload` | `mm_reload` | Reload config and admin cache, re-verify all connected players. |
| `mm_rehash` | `mm_rehash` | Rebuild admin cache from database and flat files. |
| `cs2admin_version` | `cs2admin_version` | Display the loaded CS2Admin version. |
| `sc_fw_block` | `sc_fw_block <authid> <time> <type> <reason>` | (Web panel RCON) Apply a mute (type 1) or gag (type 2) to a connected player. |
| `sc_fw_unmute` | `sc_fw_unmute <authid>` | (Web panel RCON) Unmute a connected player. |
| `sc_fw_ungag` | `sc_fw_ungag <authid>` | (Web panel RCON) Ungag a connected player. |

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
