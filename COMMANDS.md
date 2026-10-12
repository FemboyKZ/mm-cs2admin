# Commands

## Chat Commands (in-game)

| Command         | Usage                                      | Permissions     | Description                                                                                  |
| --------------- | ------------------------------------------ | --------------- | -------------------------------------------------------------------------------------------- |
| `!ban`          | `!ban <target> time=<time> [reason=]`      | `d` (Ban)       | Ban a connected player. Time in minutes (supports suffixes: h/d/w/m), 0 = permanent.         |
| `!unban`        | `!unban <steamid>`                         | `e` (Unban)     | Unban a player by SteamID. Off when `Unban` is 0.                                            |
| `!addban`       | `!addban <steamid> time=<time> [reason=]`  | `d` (Ban)       | Ban a SteamID, online or not. Off when `Addban` is 0.                                        |
| `!banip`        | `!banip <ip> time=<time> [reason=]`        | `d` (Ban)       | Ban an IP address. Time in minutes (supports suffixes: h/d/w/m), 0 = permanent.              |
| `!kick`         | `!kick <target> [reason=]`                 | `c` (Kick)      | Kick a connected player from the server.                                                     |
| `!slay`         | `!slay <target>`                           | `f` (Slay)      | Slay a player. Supports multi-target selectors (@all, @t, @ct, etc.).                        |
| `!mute`         | `!mute <target> [time=] [reason=]`         | `j` (Chat)      | Mute a player (block voice). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent.    |
| `!unmute`       | `!unmute <target>`                         | `j` (Chat)      | Unmute a player.                                                                             |
| `!gag`          | `!gag <target> [time=] [reason=]`          | `j` (Chat)      | Gag a player (block text chat). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent. |
| `!ungag`        | `!ungag <target>`                          | `j` (Chat)      | Ungag a player.                                                                              |
| `!silence`      | `!silence <target> [time=] [reason=]`      | `j` (Chat)      | Silence a player (mute + gag). Time in minutes (supports suffixes: h/d/w/m), 0 = permanent.  |
| `!unsilence`    | `!unsilence <target>`                      | `j` (Chat)      | Unsilence a player (unmute + ungag).                                                         |
| `!comms`        | `!comms [target]`                          | `j` (Chat)      | Show comm restriction status for a player (defaults to self).                                |
| `!listbans`     | `!listbans <target\|steamid>`              | `d` (Ban)       | Last 10 bans for a player, online or by SteamID.                                             |
| `!listcomms`    | `!listcomms <target\|steamid>`             | `j` (Chat)      | Last 10 comm blocks for a player, online or by SteamID.                                      |
| `!admin`        | `!admin`                                   | `b` (Generic)   | SourceMod-style admin menu with every admin tool you can use. Needs mm-cs2menus.             |
| `!who`          | `!who [target]`                            | `b` (Generic)   | List online admins with their flags, group and immunity, or show one player's.               |
| `!reloadadmins` | `!reloadadmins`                            | `d` (Ban)       | Rebuild the admin cache from the flat files and database.                                    |
| `!execcfg`      | `!execcfg [file]`                          | `i` (Config)    | Execute a config under `cfg/`, or pick one listed in `cfg/cs2admin/adminmenu_cfgs.txt`.      |
| `!listdc`       | `!listdc`                                  | `d` (Ban)       | Show recently disconnected players (name, SteamID, IP, time ago).                            |
| `!give`         | `!give <target> weapon=<weapon>`           | `n` (Cheats)    | Give a weapon to player(s). Auto-prepends `weapon_` if missing. Supports multi-target.       |
| `!strip`        | `!strip <target>`                          | `n` (Cheats)    | Strip all weapons from player(s). Supports multi-target selectors.                           |
| `!rcon`         | `!rcon <command>`                          | `m` (RCON)      | Execute a server console command.                                                            |
| `!map`          | `!map <mapname\|workshopid\|url>`          | `g` (Changemap) | Change the current map. Supports partial name match from maplist, workshop IDs or links.     |
| `!maps`         | `!maps [page]`                             | `g` (Changemap) | List available maps from the maplist (paginated).                                            |
| `!pm`           | `!pm <target> message=<text>`              | `j` (Chat)      | Private message a player. Echoes to all online admins.                                       |
| `!entfire`      | `!entfire <entity> input=<input> [value=]` | `n` (Cheats)    | Fire an input on a map entity via `ent_fire`.                                                |
| `!report`       | `!report <target> reason=<text>`           | open            | Report a player to online admins. Subject to cooldown.                                       |
| `!adminpass`    | `!adminpass <password>`                    | open            | Unlock your admin rights when your entry has a `password`. Never shown in chat.              |
| `!adminhelp`    | `!adminhelp [page]`                        | open            | List the commands you can use (paginated).                                                   |
| `!find`         | `!find <text>`                             | open            | Search the commands you can use by name.                                                     |
| `!tag`          | `!tag [id\|none]`                          | open            | Pick which of your matched tags is displayed, or open a picker.                              |

Every chat command is also a console command as `mm_<name>`.

Arguments are `key=value` pairs in any order.
The one argument shown without `key=` goes first and needs no key,
so `!ban jvn time=1d reason=wall hack` is `!ban player=jvn time=1d reason=wall hack`.
A value runs until the next `key=`, and `key="value"` or `"key=value"` quote one that holds a `key=` of its own. `!rcon` is the exception, it sends the rest of the line to the console exactly as typed.

Permission check order. Root (`z`) always passes.

1. Group overrides (`admin_groups.cfg`). The first of the admin's groups with a match decides, flat-file groups before the database group.
2. Global overrides (`admin_overrides.cfg`, `sb_overrides`).
3. The default flag above.

Override names are case-insensitive and work with or without the `mm_`/`sm_` prefix. Command groups take an `@` prefix and follow SourceMod's plugin names, so SourceMod group overrides carry over.

| Group             | Commands                                                                       |
| ----------------- | ------------------------------------------------------------------------------ |
| `@basebans`       | `ban`, `unban`, `addban`, `banip`, `listbans`                                  |
| `@basecomm`       | `mute`, `unmute`, `gag`, `ungag`, `silence`, `unsilence`, `comms`, `listcomms` |
| `@basecommands`   | `kick`, `who`, `listdc`, `rcon`, `map`, `maps`, `reloadadmins`, `execcfg`      |
| `@adminmenu`      | `admin`                                                                        |
| `@playercommands` | `slay`                                                                         |
| `@basechat`       | `pm`                                                                           |
| `@funcommands`    | `give`, `strip`                                                                |
| `@cs2admin`       | `entfire`, `report`, `tag`, `adminhelp`, `find`, `adminpass`                   |

Commands that act on a player need strictly higher immunity than the target. Root, console and self-targeting bypass it.

Comm blocks follow SourceComms' `CommsConfig`. `DefaultTime` fills a missing time, `MaxLength` caps it, and lifting a block needs at least the immunity of whoever placed it. The `n` flag bypasses both limits.

## Console / RCON Commands

| Command            | Usage                                   | Description                                                           |
| ------------------ | --------------------------------------- | --------------------------------------------------------------------- |
| `mm_reload`        | `mm_reload`                             | Reload config, maplist and admin cache, re-verify connected players.  |
| `mm_rehash`        | `mm_rehash`                             | Rebuild admin cache from database and flat files.                     |
| `cs2admin_version` | `cs2admin_version`                      | Display the loaded CS2Admin version.                                  |
| `sc_fw_block`      | `sc_fw_block <type> <length> <steamid>` | (Web panel RCON) Mute (1), gag (2) or silence (3). Length in seconds. |
| `sc_fw_unmute`     | `sc_fw_unmute <steamid>`                | (Web panel RCON) Unmute a connected player.                           |
| `sc_fw_ungag`      | `sc_fw_ungag <steamid>`                 | (Web panel RCON) Ungag a connected player.                            |

## Target Selectors

Commands that accept a `<target>` support the following selectors:

| Selector      | Description                                                           |
| ------------- | --------------------------------------------------------------------- |
| `@me`         | Yourself                                                              |
| `@all`        | All players                                                           |
| `@t`          | All Terrorists                                                        |
| `@ct`         | All Counter-Terrorists                                                |
| `@spec`       | All Spectators                                                        |
| `@dead`       | All dead players                                                      |
| `@alive`      | All alive players                                                     |
| `@random`     | A random player                                                       |
| `@bot`        | All bots                                                              |
| `@human`      | All human players                                                     |
| `STEAM_0:X:Y` | Target by SteamID (Steam2, Steam3, SteamID64, or `$` and a SteamID64) |
| `&name`       | Target by exact name                                                  |
| `#slot`       | Target by player slot number                                          |
| `name`        | Partial name match                                                    |

## Duration Suffixes

Time values for ban/mute/gag commands support optional suffixes:

| Suffix   | Meaning           | Example           |
| -------- | ----------------- | ----------------- |
| _(none)_ | Minutes (default) | `60` = 60 minutes |
| `h`      | Hours             | `2h` = 2 hours    |
| `d`      | Days              | `7d` = 7 days     |
| `w`      | Weeks             | `2w` = 2 weeks    |
| `m`      | Months (30 days)  | `1m` = 30 days    |

Values too large to store are rejected.
