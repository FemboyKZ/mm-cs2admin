# CS2 Admin

![Downloads](https://img.shields.io/github/downloads/FemboyKZ/mm-cs2admin/total?style=flat-square) ![Last commit](https://img.shields.io/github/last-commit/FemboyKZ/mm-cs2admin?style=flat-square) ![Open issues](https://img.shields.io/github/issues/FemboyKZ/mm-cs2admin?style=flat-square) ![Closed issues](https://img.shields.io/github/issues-closed/FemboyKZ/mm-cs2admin?style=flat-square) ![Size](https://img.shields.io/github/repo-size/FemboyKZ/mm-cs2admin?style=flat-square) ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/FemboyKZ/mm-cs2admin/build.yml?style=flat-square)

Metamod Admin plugin for CS2 with SM style configuration and SB++ support.

Shared maplist with [cs2-rockthevote](https://github.com/FemboyKZ/cs2-rockthevote).

## API

Other Metamod: Source plugins can access public methods found in [ics2admin.h](https://github.com/FemboyKZ/mm-utils/blob/master/interfaces/cs2admin/ics2admin.h) and [forwards.h](https://github.com/FemboyKZ/mm-utils/blob/master/interfaces/cs2admin/forwards.h)

```cpp
#include "interfaces/cs2admin/ics2admin.h"

// In AllPluginsLoaded or later:
ICS2Admin *admin = (ICS2Admin *)g_SMAPI->MetaFactory(
    CS2ADMIN_INTERFACE, nullptr, nullptr);

if (admin && admin->HasFlag(slot, CS2ADMIN_FLAG_BAN))
    // player can ban
```

```cpp
#include "interfaces/cs2admin/forwards.h"

ICS2AdminForwards *fwd = (ICS2AdminForwards *)g_SMAPI->MetaFactory(
    CS2ADMIN_FORWARDS_INTERFACE, nullptr, nullptr);
```

## Installation

### Requirements

- CS2 Server (Recommended to use [SteamRT3 Docker](https://gitlab.steamos.cloud/steamrt/sniper/sdk/-/blob/steamrt/sniper/README.md))
- [MetaMod: Source 2.0](https://www.metamodsource.net/downloads.php/?branch=master)
- [sql_mm](https://github.com/zer0k-z/sql_mm)
- (Optional) MySQL Database
- (Optional) [SB++ Web Panel](https://github.com/sbpp/sourcebans-pp) (Tested only on 1.8+)
- (Optional) [mm-cs2menus](https://github.com/FemboyKZ/mm-cs2menus) - when loaded, targeting commands typed with no arguments (e.g. `!ban`, `!kick`, `!mute`, `!report`, `!map`) open an in-game picker instead of printing usage.
  Players are grouped by team, and the ban/mute/gag/silence/kick/report form sets duration and reason on one screen.
  Its "Other" reason takes the next chat line, hidden from everyone. `!addban` picks from recently disconnected players.
- (Optional) [mm-cs2rockthevote](https://github.com/FemboyKZ/mm-cs2rockthevote) - `!map` shows the same list as `!nominate`, with KZ tiers.

### How to

1. Get the latest [release](https://github.com/FemboyKZ/mm-cs2admin/releases) for your operating system (linux/windows).
2. Extract the contents of the release archive in your server's root folder `/game/csgo/`.
3. Configure the main cfg in `/game/csgo/cfg/cs2admin/core.cfg` with database credentials and other preferences.
4. Configure Admins in `admins.cfg` or `admins_simple.ini`, Admin Groups in `admin_groups.cfg` and Admin overrides in `admin_overrides.cfg`
   (these can be directly copied from existing SM cfg with the same names).
   They and `tags.cfg` ship as `*.example.*`, copy each without the `.example`.
5. Create a new file called `maplist.txt` or rename the bundled `maplist.example.txt` file to it, then add your maps to the file.

The cs2admin plugin mirrors SourceMod's Admin configuration structure completely.
This means the following articles are valid here too:

- [Adding Admins](<https://wiki.alliedmods.net/Adding_Admins_(SourceMod)>)
- [Adding Groups](<https://wiki.alliedmods.net/Adding_Groups_(SourceMod)>)
- [Overriding Commands](<https://wiki.alliedmods.net/Overriding_Command_Access_(SourceMod)>)

## Usage

See [COMMANDS](COMMANDS.md)

## Build

### Prerequisites

- This repository is cloned recursively (ie. has submodules)
- [python3](https://www.python.org/)
- [ambuild](https://github.com/alliedmodders/ambuild), make sure `ambuild` command is available via the `PATH` environment variable;
- MSVC (VS build tools)/Clang installed for Windows/Linux.

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

### Docker

```bash
docker compose run --rm build
```

## Credits

- [SourceBans++](https://github.com/sbpp/sourcebans-pp)
- [SourceMod](https://github.com/alliedmodders/sourcemod)
- [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
- [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)

## Contributing

Feel free to create PRs or issues regarding anything, but this is very much just for fkz and I will not implement bloat like funcommands.
