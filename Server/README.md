# Cypress Server

Open-source reimplementation of dedicated servers for Plants vs. Zombies shooters, injected as a DLL (`dinput8.dll`) into the Frostbite engine at runtime.

Based on [KYBER](https://github.com/ArmchairDevelopers/Kyber) (Star Wars: Battlefront II private servers).

## Supported Games

| Game | Version | Output DLL |
|------|---------|-----------|
| Garden Warfare 1 | v1.0.3.0 | `cypress_GW1.dll` |
| Garden Warfare 2 | v1.0.12 (PreEAAC) | `cypress_GW2.dll` |
| Battle for Neighborville | Latest | `cypress_BFN.dll` |

## How It Works

1. Compiled DLL is placed in the game directory as `dinput8.dll` (DirectInput hijack)
2. When the game loads, the DLL installs [MinHook](https://github.com/tsudakageyu/minhook) function hooks into Frostbite engine functions
3. Hooks reimplement dedicated server functionality (player connections, level loading, console commands)
4. A **side-channel** TCP protocol provides real-time events to the [Launcher](https://github.com/PvZ-Cypress/Launcher)

### Key Features

- Dedicated server hosting with up to **48+ players** (vs 24 stock)
- Headless thin-client mode (server without rendering)
- Custom playlists with mixed game modes
- Player moderation (kicks, bans, moderator system)
- Hardware fingerprint bans (HWID + component-based tracking)
- Name validation (slur filter, ID_ blocking, impersonation protection)
- Side-channel TCP protocol with HMAC challenge-response auth
- Relay tunnel for NAT traversal
- Anticheat system (GW2: loadout validation, OOB detection, damage/spawn listeners)
- Console command system

## Terms of Service

By using this software you agree to the [Cypress Terms of Service](../TOS.md).

## Building

**Requirements:** Visual Studio 2022+ with the C++ desktop development workload (MSVC v143/v145, Windows SDK).

```powershell
# Build all game variants
.\build.ps1

# Build a specific game
.\build.ps1 -Game GW2

# Debug build
.\build.ps1 -Game GW2 -Configuration Debug
```

Or via Visual Studio - open `Cypress.sln`, select the configuration (e.g. `Release - GW2 | x64`), and build.

### Build Configurations

Each game has its own configuration that sets preprocessor defines (`CYPRESS_GW1`, `CYPRESS_GW2`, `CYPRESS_BFN`):

| Configuration | Platform | Output |
|--------------|----------|--------|
| `Release - GW1 \| x64` | x64 | `cypress_GW1.dll` |
| `Release - GW2 \| x64` | x64 | `cypress_GW2.dll` |
| `Release - BFN \| x64` | x64 | `cypress_BFN.dll` |
| `Debug - *` variants | x64 | Same, with debug symbols |

## Usage

Place the compiled DLL in the game's directory as `dinput8.dll`, then launch with command-line arguments. See `Examples/` for batch file templates:

```batch
:: Host a dedicated server (GW2)
GW2.Main_Win64_Retail.exe ^
  -server ^
  -listen 192.168.1.91:25200 ^
  -level Level_Coop_ZombossFactory ^
  -inclusion GameMode=TeamVanquishLarge0;TOD=Day;HostedMode=ServerHosted ^
  -Network.ServerPort 25200 ^
  -Network.MaxClientCount 48 ^
  -console

:: Join a server
GW2.Main_Win64_Retail.exe ^
  -playerName YourName ^
  -Client.ServerIp 192.168.1.91:25200
```

For a UI-based experience, use the Cypress Launcher (root of this repo).

## Project Structure

```
Source/
  Core/
    Program.h/cpp         # Entry point, DLL lifecycle, stdin command reader
    Server.h/cpp          # Server management, player tracking, status UI
    Client.h/cpp          # Client state, side-channel TCP, HWID generation
    Logging.h/cpp         # JSON + colored console logging
    Config.h              # Feature flags (HAS_DEDICATED_SERVER, etc.)
    Settings.h            # Frostbite SettingsManager wrapper
    VersionInfo.h/cpp     # Version constants
    Console/              # Game-specific console commands
  GameHooks/
    fbMainHooks.*         # Core engine hooks (init, main, console)
    fbServerHooks.*       # Server lifecycle hooks (start, update, player join/leave)
    fbClientHooks.*       # Client state hooks
    fbEnginePeerHooks.*   # Kyber socket manager integration
  GameModules/
    GW1Module.cpp         # GW1-specific patches and hooks
    GW2Module.cpp         # GW2-specific patches and hooks
    BFNModule.cpp         # BFN-specific patches (Lua console, thin-client UI)
  Anticheat/              # Server-side cheat detection (GW2)
include/
  SideChannel.h/cpp       # Side-channel TCP server/client/tunnel
  ServerBanlist.h         # Ban system with hardware fingerprinting
  ServerPlaylist.h        # Playlist rotation logic
  HWID.h                  # Hardware fingerprint generation
  FreeCam.h              # Free camera support
  MemUtil.h              # Memory patching and hooking helpers
  StringUtil.h           # String utilities
  IGameModule.h          # Game module interface
  json.hpp               # nlohmann/json
  Kyber/                  # Socket management (from KYBER project)
  MinHook/                # Runtime function hooking
  fb/                     # Reverse-engineered Frostbite engine types
  EASTL/                  # EA's STL replacement
  GameModules/            # Game module headers
Examples/                 # Launch script templates
```

## Credits

<table>
  <tr>
    <td align="center"><a href="https://github.com/ArmchairDevelopers/Kyber"><img src="https://github.com/ArmchairDevelopers.png" width="60" /><br /><b>KYBER</b></a><br />Frostbite socket manager reimplementation</td>
    <td align="center"><a href="https://github.com/Andersson799"><img src="https://github.com/Andersson799.png" width="60" /><br /><b>Andersson799</b></a><br />Frostbite dedicated server reverse engineering</td>
    <td align="center"><a href="https://github.com/breakfastbrainz2"><img src="https://github.com/breakfastbrainz2.png" width="60" /><br /><b>BreakfastBrainz2</b></a><br />GW1 & GW2 dedicated servers</td>
    <td align="center"><a href="https://github.com/ghdrago"><img src="https://github.com/ghdrago.png" width="60" /><br /><b>Ghup</b></a><br />BFN dedicated servers</td>
    <td align="center"><a href="https://github.com/dylannws"><img src="https://github.com/dylannws.png" width="60" /><br /><b>Dylan</b></a><br />GW2 anticheat</td>
    <td align="center"><a href="https://github.com/v0ee"><img src="https://github.com/v0ee.png" width="60" /><br /><b>v0e</b></a><br />Side-channel, relay tunnel, settings commands</td>
  </tr>
</table>

## License

[GPL-3.0](LICENSE)

