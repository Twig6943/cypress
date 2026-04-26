function switchDoc(doc) {
    document.querySelectorAll('.docs-nav-btn').forEach(el => el.classList.remove('active'));
    const btn = document.querySelector('[data-doc="' + doc + '"]');
    if (btn) btn.classList.add('active');
    document.getElementById('docsContent').innerHTML = getDocContent(doc);
}

function getDocContent(doc) {
    switch (doc) {
        case 'quickstart': return docQuickStart();
        case 'joining': return docJoining();
        case 'hosting': return docHosting();
        case 'server-commands': return docServerCommands();
        case 'levels': return docLevels();
        case 'modifiers': return docModifiers();
        case 'playlists': return docPlaylists();
        default: return '';
    }
}

function docQuickStart() {
    return '<div class="docs-section active"><h3>Quick Start Guide</h3><div class="steps">'
        + step(1,'Open EA Desktop','EA Desktop <strong>must</strong> be running with an account that owns the game, or you\'ll get a black screen on launch.')
        + step(2,'Select Your Game','Use the <strong>Game</strong> dropdown in the sidebar to pick GW1, GW2, or BFN.')
        + step(3,'Set Game Directory','Click <strong>Browse</strong> or <strong>Auto-detect</strong> in the sidebar to locate your game installation folder.')
        + step(4,'Sign In','Go to the <strong>Profile</strong> tab and sign in with your EA account. Register a <strong>Cypress username</strong> if you haven\'t already - this is your identity on the server browser and in-game.')
        + step(5,'Join or Host','<strong>Joining?</strong> The easiest way is the <strong>Server Browser</strong> tab - just double-click a server. You can also join by IP in the <strong>Join</strong> tab, or enter a <strong>Relay Code</strong> if the host gave you one.<br><strong>Hosting?</strong> Configure your map, mode, and settings in the <strong>Host</strong> tab. Enable <strong>EU Relay</strong> if you can\'t port forward.')
        + step(6,'Keep This Window Open','The launcher manages the game process. Closing it will kill the server or disconnect you.')
        + '</div></div>';
}

function docJoining() {
    return '<div class="docs-section active"><h3>How to Join a Server</h3>'
        + '<div class="info-card"><h5>Server Browser (Easiest)</h5><div class="steps compact">'
        + step(1,'','Open the <strong>Server Browser</strong> tab')
        + step(2,'','Filter by game, player count, or search by name')
        + step(3,'','Double-click a server to join. If it has a password, you\'ll be prompted for it')
        + '</div></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Direct / VPN (by IP)</h5><div class="steps compact">'
        + step(1,'','In the <strong>Join</strong> tab, make sure <strong>Connection Path</strong> is set to <strong>Direct/VPN</strong>')
        + step(2,'','Enter the host\'s <strong>IP address</strong> (public IP, LAN IP, or VPN IP like Radmin/Hamachi)')
        + step(3,'','Enter the <strong>Server Password</strong> if one was set')
        + step(4,'','Click <strong>Join Server</strong>')
        + '</div></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Relay Code</h5><p style="margin-bottom:8px;font-size:12px;color:var(--text-muted);">If the host is using the EU Relay (or a custom relay), they\'ll have a 6-character code to share.</p><div class="steps compact">'
        + step(1,'','In the <strong>Join</strong> tab, switch <strong>Connection Path</strong> to <strong>Relay</strong>')
        + step(2,'','Enter the <strong>Relay Code</strong> the host gave you - the address auto-fills when valid')
        + step(3,'','Click <strong>Join Server</strong>')
        + '</div></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>VPN Setup (Radmin / Hamachi)</h5><p style="margin-bottom:8px;font-size:12px;color:var(--text-muted);">Only needed if the host is using a VPN instead of port forwarding or relay.</p><div class="steps compact">'
        + step(1,'','Install <a href="https://www.radmin-vpn.com/" class="doc-link external-link">Radmin VPN</a> (or similar) and join the host\'s network')
        + step(2,'','Use the host\'s <strong>VPN IP</strong> as the server address')
        + '</div></div></div>';
}

function docHosting() {
    return '<div class="docs-section active"><h3>How to Host a Server</h3>'
        + '<div class="info-card"><h5>Method 1 - EU Relay (No Port Forwarding)</h5><p style="margin-bottom:8px;font-size:12px;color:var(--text-muted);">The easiest way to host. Your server connects through a relay so players don\'t need your IP and you don\'t need to touch your router.</p><div class="steps compact">'
        + step(1,'','In the <strong>Host &gt; Network</strong> tab, enable <strong>EU Relay</strong>')
        + step(2,'','Configure your map, mode, and any modifiers')
        + step(3,'','Click <strong>Start Server</strong> - you\'ll get a <strong>Relay Code</strong> to share with players')
        + step(4,'','Your server will also appear in the <strong>Server Browser</strong> if "Listed in Browser" is enabled')
        + '</div></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Method 2 - Port Forwarding</h5><div class="steps compact">'
        + step(1,'','Open CMD and run <code>ipconfig</code>. Note your <strong>IPv4 address</strong> and <strong>Default Gateway</strong>')
        + step(2,'','Open the Default Gateway IP in your browser and log in to your router')
        + step(3,'','Find <strong>Port Forwarding</strong> settings and create two rules:<br>&bull; Port <code>25200</code>, Protocol <code>UDP</code>, IP = your IPv4 (game traffic)<br>&bull; Port <code>14638</code>, Protocol <code>TCP</code>, IP = your IPv4 (side channel - used for server browser, identity, and player queries)<br>For multiple servers, use ranges starting at those ports')
        + step(4,'','In Host &gt; Network, enter your <strong>IPv4</strong> as the Bind Address and click <strong>Start Server</strong>')
        + step(5,'','Share your <strong>public IP</strong> (<a href="https://ipchicken.com/" class="doc-link external-link">ipchicken.com</a>) with players')
        + '</div><p style="margin-top:8px;font-size:12px;color:var(--text-muted);">Your IPv4 can change periodically. Re-check with ipconfig if your server stops working. Allow any firewall prompts.</p></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Method 3 - Radmin VPN</h5><p style="margin-bottom:8px;font-size:12px;color:var(--text-muted);">Everyone who wants to join must also install Radmin and join your network.</p><div class="steps compact">'
        + step(1,'','Install <a href="https://www.radmin-vpn.com/" class="doc-link external-link">Radmin VPN</a> and create a network')
        + step(2,'','Copy your <strong>Radmin IP</strong> (shown at top) and enter it as the <strong>Bind Address</strong>')
        + step(3,'','Configure map &amp; mode, click <strong>Start Server</strong>')
        + step(4,'','Share your Radmin network name, password, and IP with players')
        + '</div></div>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Server Settings</h5>'
        + '<table class="ref-table"><tr><th>Setting</th><th>Description</th></tr>'
        + '<tr><td><strong>Listed in Browser</strong></td><td>Makes your server visible in the Server Browser tab for everyone</td></tr>'
        + '<tr><td><strong>GCBDB</strong></td><td>Global ban database - auto-kicks known cheaters on join</td></tr>'
        + '<tr><td><strong>Block ID_ Usernames</strong></td><td>Rejects players with "ID_" prefix names (anti-spam)</td></tr>'
        + '<tr><td><strong>Max Players</strong></td><td>1-64 player limit</td></tr>'
        + '<tr><td><strong>Server Password</strong></td><td>Require a password to join</td></tr>'
        + '<tr><td><strong>MOTD</strong></td><td>Message of the day shown in the server browser. Supports bold, italic, colors, gradients, and animations</td></tr>'
        + '<tr><td><strong>Server Icon</strong></td><td>Custom 64x64 icon shown in the server browser</td></tr>'
        + '<tr><td><strong>Anticheat</strong></td><td>Server-side cheat prevention with configurable checks (Host &gt; Anticheat tab)</td></tr>'
        + '</table></div></div>';
}

function docServerCommands() {
    return '<div class="docs-section active"><h3>Server Console Commands</h3>'
        + '<p>Type these into the server window\'s text input at the bottom (below the FPS/Level info).</p>'
        + '<h4>All Games (GW1, GW2, BFN)</h4>'
        + '<table class="ref-table"><tr><th>Command</th><th>Description</th><th>Example</th></tr>'
        + cmdRow('Server.RestartLevel','Restart the current level','Server.RestartLevel')
        + cmdRow('Server.LoadLevel','Load a specific level + inclusion','Server.LoadLevel Level_FE_Hub GameMode=FreeRoam;TOD=Day;HostedMode=ServerHosted')
        + cmdRow('Server.KickPlayer','Kick a player by name','Server.KickPlayer Jim')
        + cmdRow('Server.KickPlayerById','Kick by server-assigned ID','Server.KickPlayerById 4')
        + cmdRow('Server.BanPlayer','Ban a player by name','Server.BanPlayer Jim')
        + cmdRow('Server.BanPlayerById','Ban by server-assigned ID','Server.BanPlayerById 4')
        + cmdRow('Server.UnbanPlayer','Unban a previously banned player','Server.UnbanPlayer Jim')
        + cmdRow('Server.LoadNextPlaylistSetup','Advance to the next playlist entry','Server.LoadNextPlaylistSetup')
        + '</table>'
        + '<h4>GW2 Only</h4>'
        + '<table class="ref-table"><tr><th>Command</th><th>Description</th><th>Example</th></tr>'
        + cmdRow('Server.Say','Broadcast a message to all players for N seconds',"Server.Say 'Hello World!' 5")
        + cmdRow('Server.SayToPlayer','Message a specific player for N seconds',"Server.SayToPlayer 'Jim' 'Hello!' 5")
        + '</table>'
        + '<div class="info-card" style="margin-top:12px;"><h5>Runtime Settings</h5><p>You can also change any game setting live by typing it directly, e.g. <code>GameMode.ModeTeamId 2</code> or <code>GameMode.CrazyOption2 true</code></p></div>'
        + '</div>';
}

function cmdRow(cmd, desc, ex) {
    return '<tr><td><code>' + cmd + '</code></td><td>' + desc + '</td><td><code>' + ex + '</code></td></tr>';
}

function step(n, title, text) {
    return '<div class="step"><span class="step-num">' + n + '</span><div class="step-content">' + (title ? '<h4>' + title + '</h4>' : '') + '<p>' + text + '</p></div></div>';
}

function docLevels() {
    let html = '<div class="docs-section active"><h3>Level &amp; Mode Reference</h3>'
        + '<p>Complete reference of all levels, supported game modes, and how inclusion strings work for each game.</p>'
        + '<div class="game-filter">'
        + '<button class="game-filter-btn active" onclick="filterDoc(\'lvl\',\'gw1\',this)">GW1</button>'
        + '<button class="game-filter-btn" onclick="filterDoc(\'lvl\',\'gw2\',this)">GW2</button>'
        + '<button class="game-filter-btn" onclick="filterDoc(\'lvl\',\'bfn\',this)">BFN</button>'
        + '</div>';

    html += '<div id="lvl-gw1" class="doc-filter-section">'
        + '<div class="info-card"><h5>GW1 Level Format</h5><p>GW1 requires the <strong>full level path</strong>. The number after the GameMode (0 or 1) determines which map variant loads.</p><pre>Level = _pvz/Levels/Mainstreet/Level_COOP_Mainstreet/Level_COOP_Mainstreet\nInclusion = GameMode=Coop0;TOD=Day</pre></div>'
        + '<h4>Levels</h4><table class="ref-table"><tr><th>Level Path</th><th>Name</th><th>Category</th><th>Variants</th><th>Night?</th></tr>';
    GAME_DATA.GW1.levels.forEach(l => {
        html += '<tr><td><code>' + l.id + '</code></td><td>' + l.name + '</td><td>' + l.cat + '</td><td>' + (l.variant||'0') + '</td><td>' + (l.night?'Yes':'No') + '</td></tr>';
    });
    html += '</table><h4>Game Modes</h4><table class="ref-table"><tr><th>Mode ID</th><th>Name</th><th>Notes</th></tr>';
    GAME_DATA.GW1.modes.forEach(m => {
        html += '<tr><td><code>' + m.id + '</code></td><td>' + m.name + '</td><td>' + (m.note||'Append variant 0 or 1') + '</td></tr>';
    });
    html += '</table></div>';

    html += '<div id="lvl-gw2" class="doc-filter-section" style="display:none;">'
        + '<div class="info-card"><h5>GW2 Level Format</h5><p>GW2 uses <strong>short Level IDs</strong> (not full paths). Inclusion must include <code>HostedMode</code>.</p><pre>Level = Level_FE_Hub\nInclusion = GameMode=FreeRoam;TOD=Day;HostedMode=ServerHosted</pre>'
        + '<p style="margin-top:6px;"><strong>HostedMode values:</strong> <code>ServerHosted</code> (normal MP), <code>PeerHosted</code> (private bots), <code>LocalHosted</code> (solo ops/bots)</p></div>'
        + '<h4>Levels</h4><table class="ref-table"><tr><th>Level ID</th><th>Name</th><th>Category</th><th>Supported Modes</th></tr>';
    GAME_DATA.GW2.levels.forEach(l => {
        const modeNames = (l.modes||[]).map(mid => { const m = GAME_DATA.GW2.modes.find(x => x.id === mid); return m ? m.name : mid; }).join(', ');
        html += '<tr><td><code>' + l.id + '</code></td><td>' + l.name + '</td><td>' + l.cat + '</td><td style="font-size:11px;">' + modeNames + '</td></tr>';
    });
    html += '</table><h4>Game Modes</h4><table class="ref-table"><tr><th>Mode ID</th><th>Name</th><th>Category</th></tr>';
    GAME_DATA.GW2.modes.forEach(m => {
        html += '<tr><td><code>' + m.id + '</code></td><td>' + m.name + '</td><td>' + m.cat + '</td></tr>';
    });
    html += '</table></div>';

    html += '<div id="lvl-bfn" class="doc-filter-section" style="display:none;">'
        + '<div class="info-card"><h5>BFN Level Format</h5><p>BFN uses <strong>DSubs</strong> (sub-levels of Level_Picnic_Root) and <strong>StartPoints</strong> which function like sub-gamemodes.</p><pre>DSub = DSub_SocialSpace\nInclusion = GameMode=Mode_SocialSpace\nStartPoint = StartPoint_SocialSpace</pre></div>'
        + '<h4>DSubs (Levels)</h4><table class="ref-table"><tr><th>DSub ID</th><th>Name</th><th>Category</th></tr>';
    GAME_DATA.BFN.levels.forEach(l => {
        html += '<tr><td><code>' + l.id + '</code></td><td>' + l.name + '</td><td>' + l.cat + '</td></tr>';
    });
    html += '</table><h4>Game Modes</h4><table class="ref-table"><tr><th>Mode ID</th><th>Name</th><th>Category</th></tr>';
    GAME_DATA.BFN.modes.forEach(m => {
        html += '<tr><td><code>' + m.id + '</code></td><td>' + m.name + '</td><td>' + m.cat + '</td></tr>';
    });
    html += '</table><h4>Start Points</h4><table class="ref-table"><tr><th>StartPoint ID</th><th>Name / Mode</th></tr>';
    GAME_DATA.BFN.startPoints.forEach(s => {
        html += '<tr><td><code>' + s.id + '</code></td><td>' + s.name + '</td></tr>';
    });
    html += '</table></div>';

    html += '</div>';
    return html;
}

function docModifiers() {
    let html = '<div class="docs-section active"><h3>Game Modifier Reference</h3>'
        + '<p>All modifiers available per game. These are automatically handled as toggles/dropdowns in the <strong>Host &gt; Game Modifiers</strong> section. For manual use in launch args, prefix with <code>-</code> and separate with spaces.</p>'
        + '<pre>-GameMode.CrazyOption2 true -GameMode.CrazyOption5 true -GameMode.StoredDifficultyIndex 4</pre>'
        + '<div class="game-filter">'
        + '<button class="game-filter-btn active" onclick="filterDoc(\'mod\',\'gw1\',this)">GW1</button>'
        + '<button class="game-filter-btn" onclick="filterDoc(\'mod\',\'gw2\',this)">GW2</button>'
        + '<button class="game-filter-btn" onclick="filterDoc(\'mod\',\'bfn\',this)">BFN</button>'
        + '</div>';

    html += '<div id="mod-gw1" class="doc-filter-section"><table class="ref-table"><tr><th>Setting</th><th>Name</th><th>Description</th><th>Type</th></tr>';
    GAME_DATA.GW1.modifierCategories.forEach(cat => {
        if (cat.mods) cat.mods.forEach(m => {
            html += '<tr><td><code>' + m.key + '</code></td><td>' + m.name + '</td><td>' + m.desc + '</td><td>' + m.type + '</td></tr>';
        });
    });
    html += '</table></div>';

    html += '<div id="mod-gw2" class="doc-filter-section" style="display:none;">';
    GAME_DATA.GW2.modifierCategories.forEach(cat => {
        if (cat.special === 'gw2_costumes') {
            html += '<h4>Character Restrictions (AvailableCostumes)</h4>'
                + '<p>Restrict which characters can be used. Use semicolon-separated character IDs.</p>'
                + '<table class="ref-table"><tr><th>Preset Name</th><th>Description</th></tr>';
            Object.keys(GW2_COSTUME_PRESETS).forEach(name => {
                html += '<tr><td><strong>' + name + '</strong></td><td style="font-size:11px;word-break:break-all;"><code>' + GW2_COSTUME_PRESETS[name].substring(0,60) + '...</code></td></tr>';
            });
            html += '</table>';
            html += '<h4>AI Character Sets</h4><table class="ref-table"><tr><th>Set Name</th><th>Plant ID</th><th>Zombie ID</th></tr>';
            Object.keys(GW2_AI_SETS.Plants).forEach(name => {
                html += '<tr><td>' + name + '</td><td><code>' + GW2_AI_SETS.Plants[name] + '</code></td><td><code>' + (GW2_AI_SETS.Zombies[name]||'None') + '</code></td></tr>';
            });
            html += '</table>';
        } else if (cat.mods) {
            html += '<h4>' + cat.name + '</h4><table class="ref-table"><tr><th>Setting</th><th>Name</th><th>Description</th><th>Type</th></tr>';
            cat.mods.forEach(m => {
                html += '<tr><td><code>' + m.key + '</code></td><td>' + m.name + '</td><td>' + m.desc + '</td><td>' + m.type + '</td></tr>';
            });
            html += '</table>';
        }
    });
    html += '</div>';

    html += '<div id="mod-bfn" class="doc-filter-section" style="display:none;">';
    GAME_DATA.BFN.modifierCategories.forEach(cat => {
        if (cat.special === 'bfn_classes') {
            html += '<h4>Character Kill Switches</h4>'
                + '<p>Prevent specific classes from being selected using <code>SyncedPVZSettings.CharacterKillSwitches</code>. Format: <code>["RoseClass","ImpClass",...]</code></p>'
                + '<table class="ref-table"><tr><th>Preset</th><th>Disabled Classes</th></tr>';
            Object.entries(BFN_KILLSWITCH_PRESETS).forEach(([name, classes]) => {
                const enabled = [...BFN_CLASSES.Plants, ...BFN_CLASSES.Zombies].filter(c => !classes.includes(c.kit)).map(c => c.name);
                html += '<tr><td><strong>' + name + '</strong></td><td style="font-size:11px;">Allows: ' + enabled.join(', ') + '</td></tr>';
            });
            html += '</table>';
            html += '<h4>AI Spawn Bitmask</h4>'
                + '<p>Use <code>SoloPlaySettings.DisabledPlantAIClassMask</code> / <code>DisabledZombieAIClassMask</code> to prevent AI classes from spawning. Add up the values of classes you want to disable.</p>'
                + '<table class="ref-table"><tr><th>Plant Class</th><th>Value</th><th>Zombie Class</th><th>Value</th></tr>';
            const maxLen = Math.max(BFN_CLASSES.Plants.length, BFN_CLASSES.Zombies.length);
            for (let i = 0; i < maxLen; i++) {
                const p = BFN_CLASSES.Plants[i];
                const z = BFN_CLASSES.Zombies[i];
                html += '<tr><td>' + (p?p.name:'') + '</td><td>' + (p?p.mask:'') + '</td><td>' + (z?z.name:'') + '</td><td>' + (z?z.mask:'') + '</td></tr>';
            }
            html += '</table>';
            html += '<h4>AI Character Sets</h4><table class="ref-table"><tr><th>Set Name</th><th>Plant ID</th><th>Zombie ID</th></tr>';
            Object.keys(BFN_AI_SETS.Plants).forEach(name => {
                html += '<tr><td>' + name + '</td><td><code>' + BFN_AI_SETS.Plants[name] + '</code></td><td><code>' + (BFN_AI_SETS.Zombies[name]||'None') + '</code></td></tr>';
            });
            html += '</table>';
        } else if (cat.mods) {
            html += '<h4>' + cat.name + '</h4><table class="ref-table"><tr><th>Setting</th><th>Name</th><th>Description</th><th>Type</th></tr>';
            cat.mods.forEach(m => {
                html += '<tr><td><code>' + m.key + '</code></td><td>' + m.name + '</td><td>' + m.desc + '</td><td>' + m.type + '</td></tr>';
            });
            html += '</table>';
        }
    });
    html += '</div></div>';
    return html;
}

function docPlaylists() {
    return '<div class="docs-section active"><h3>Playlist Reference</h3>'
        + '<div class="info-card"><h5>Setup</h5><p>Create a <code>Playlists</code> folder in your game\'s directory. Any <code>.json</code> file in that folder will be detected by the launcher.</p></div>'
        + '<h4>Global Settings</h4>'
        + '<table class="ref-table"><tr><th>Key</th><th>Type</th><th>Description</th></tr>'
        + '<tr><td><code>RoundsPerSetup</code></td><td>Integer</td><td>How many rounds of the same setup before rotating</td></tr>'
        + '<tr><td><code>IsMixed</code></td><td>Boolean</td><td>If true, levels and modes are randomized instead of following order</td></tr>'
        + '<tr><td><code>Loadscreen_GamemodeNameOverride</code></td><td>String</td><td>Override the mode name on load screen (all entries)</td></tr>'
        + '<tr><td><code>Loadscreen_LevelNameOverride</code></td><td>String</td><td>Override the level name on load screen (all entries)</td></tr>'
        + '<tr><td><code>Loadscreen_LevelDescriptionOverride</code></td><td>String</td><td>Override the level description (all entries)</td></tr>'
        + '<tr><td><code>Loadscreen_UIAssetPathOverride</code></td><td>String</td><td>Override the loading screen image (all entries)</td></tr>'
        + '</table>'
        + '<h4>Ordered Rotation (IsMixed = false)</h4>'
        + '<p>Each entry in <code>PlaylistRotation</code> has:</p>'
        + '<table class="ref-table"><tr><th>Key</th><th>Required</th><th>Description</th></tr>'
        + '<tr><td><code>LevelName</code></td><td>Yes</td><td>Full level path: <code>Levels/Level_Rush_Snow/Level_Rush_Snow</code> (GW1 uses its own paths)</td></tr>'
        + '<tr><td><code>GameMode</code></td><td>Yes</td><td>The game mode ID</td></tr>'
        + '<tr><td><code>StartPoint</code></td><td>BFN only</td><td>Required for BFN playlists</td></tr>'
        + '<tr><td><code>TOD</code></td><td>No</td><td>Day or Night</td></tr>'
        + '<tr><td><code>SettingsToApply</code></td><td>No</td><td>Pipe-separated settings: <code>GameMode.CrazyOption2 true|GameMode.CrazyOption3 true</code></td></tr>'
        + '<tr><td><code>Loadscreen_*</code></td><td>No</td><td>Per-entry loadscreen overrides (GamemodeName, LevelName, LevelDescription, UIAssetPath)</td></tr>'
        + '</table>'
        + '<div class="info-card" style="margin-top:8px;"><h5>Tip: Resetting Settings</h5><p>If you enable settings like <code>GameMode.CrazyOption8 true</code> on one entry, add <code>GameMode.CrazyOption8 false</code> to the next entry\'s SettingsToApply to turn it off.</p></div>'
        + '<h4>Mixed / Randomized (IsMixed = true)</h4>'
        + '<table class="ref-table"><tr><th>Key</th><th>Description</th></tr>'
        + '<tr><td><code>AvailableModes</code></td><td>Array of mode IDs that will be randomly picked</td></tr>'
        + '<tr><td><code>AvailableLevelsForModes</code></td><td>Object mapping each mode to an array of level paths</td></tr>'
        + '<tr><td><code>AvailableTODForLevels</code></td><td>(Optional) Object mapping level paths to [Day, Night] arrays</td></tr>'
        + '</table>'
        + '<h4>Example (Ordered)</h4>'
        + '<pre>{\n  "RoundsPerSetup": 2,\n  "IsMixed": false,\n  "PlaylistRotation": [\n    {\n      "LevelName": "Levels/Level_Rush_Snow/Level_Rush_Snow",\n      "GameMode": "GnGLarge0",\n      "TOD": "Night"\n    },\n    {\n      "LevelName": "Levels/Level_Coop_Egypt/Level_Coop_Egypt",\n      "GameMode": "TeamVanquishLarge0",\n      "SettingsToApply": "GameMode.CrazyOption2 true"\n    }\n  ]\n}</pre>'
        + '</div>';
}

function filterDoc(prefix, key, btn) {
    btn.parentElement.querySelectorAll('.game-filter-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    document.querySelectorAll('.doc-filter-section').forEach(s => { if (s.id.startsWith(prefix + '-')) s.style.display = 'none'; });
    document.getElementById(prefix + '-' + key).style.display = '';
}

window.addEventListener('DOMContentLoaded', () => {
    populateLevelPicker(true);
    populateModePicker();
    renderPickerOptions('levelPicker');
