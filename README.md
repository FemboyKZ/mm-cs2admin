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
