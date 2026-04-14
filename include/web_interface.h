#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <title>LD2412 Security</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    :root { --bg: #0a0a0a; --card: #161616; --text: #e0e0e0; --accent: #03dac6; --warn: #cf6679; --sec: #333; }
    * { box-sizing: border-box; }
    body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 10px; padding-bottom: 50px; }
    h2 { color: var(--accent); margin: 5px 0; font-size: 1.4rem; display: flex; align-items: center; justify-content: center; gap: 10px; }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 10px; max-width: 1200px; margin: 0 auto; }
    .card { background: var(--card); padding: 15px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    
    .stat-row { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 0.9rem; border-bottom: 1px solid #222; padding-bottom: 4px; }
    .stat-val { font-weight: bold; color: #fff; }
    
    .gauge { text-align: center; margin-bottom: 15px; }
    .big-val { font-size: 2.5rem; font-weight: bold; line-height: 1; }
    .unit { font-size: 0.8rem; color: #888; }
    
    /* Sparkline */
    svg.spark { width: 100%; height: 50px; stroke-width: 2; fill: none; margin-top: 5px; }
    
    /* Icons */
    .icon { width: 16px; height: 16px; display: inline-block; vertical-align: middle; border-radius: 50%; }
    .icon.ok { background: #00ff00; box-shadow: 0 0 5px #00ff00; }
    .icon.warn { background: orange; }
    .icon.err { background: #ff0000; }
    
    /* Inputs */
    input[type=range] { width: 100%; accent-color: var(--accent); }
    input[type=text], input[type=password], input[type=number], select { background: #222; border: 1px solid #444; color: white; padding: 8px; border-radius: 4px; width: 100%; margin-top:2px; }
    .row-input { display:flex; justify-content:space-between; align-items:center; margin-bottom:5px; gap:10px; }
    
    button { width: 100%; padding: 10px; border: none; border-radius: 6px; background: #3700b3; color: white; cursor: pointer; margin-top: 5px; }
    button:hover { opacity: 0.9; }
    button.sec { background: var(--sec); }
    button.warn { background: var(--warn); color: black; font-weight: bold; }

    /* Per-gate & Bars */
    .gate-wrapper { display: flex; align-items: center; gap: 5px; margin-bottom: 4px; font-size: 0.75rem; }
    .gate-label { width: 25px; flex-shrink:0; }
    input[type=range].mov-slider { accent-color: #03dac6; }
    input[type=range].stat-slider { accent-color: #bb86fc; }
    .gate-dimmed { opacity: 0.35; }
    
    .tabs { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }
    .tab { flex: 1; min-width: 80px; padding: 8px; background: #222; text-align: center; cursor: pointer; border-radius: 6px; font-size:0.9rem; }
    .tab.active { background: var(--accent); color: black; font-weight: bold; }
    
    .hidden { display: none; }
    
    .section-title { color:#888; font-size:0.8rem; margin:15px 0 5px 0; text-transform:uppercase; border-bottom:1px solid #333; }
    
    #toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #333; padding: 10px 20px; border-radius: 20px; opacity: 0; transition: opacity 0.3s; pointer-events: none; }

    /* Timeline scroll */
    #evt_timeline_list::-webkit-scrollbar { width: 6px; }
    #evt_timeline_list::-webkit-scrollbar-thumb { background: #444; border-radius: 3px; }
    #evt_timeline_list::-webkit-scrollbar-track { background: #111; }

    /* Mobile Responsive */
    @media (max-width: 480px) {
        .grid { grid-template-columns: 1fr; gap: 8px; }
        body { padding: 5px; padding-bottom: 60px; }
        .big-val { font-size: 2rem; }
        .tabs { flex-wrap: wrap; gap: 4px; }
        .tab { min-width: 60px; font-size: 0.8rem; padding: 10px 6px; flex-grow: 1; }
        button { padding: 12px; min-height: 44px; font-size: 1rem; }
        input[type=range] { height: 30px; }
        .gate-wrapper { flex-wrap: wrap; }
        .row-input { flex-direction: column; align-items: stretch; gap: 2px; }
        .row-input span { margin-bottom: 2px; }
        input[type=text], input[type=number], select { padding: 10px; font-size: 1rem; }
    }
  </style>
</head>
<body>

  <h2>
    LD2412 <span style="font-size:0.6em; color:#666" id="fw_ver">...</span>
    <span id="sse_icon" class="icon" title="Realtime connection"></span>
    <span id="wifi_icon" class="icon" title="ETH"></span>
    <span id="mqtt_icon" class="icon" title="MQTT"></span>
  </h2>

  <div id="security_warning" style="background:#cf6679; color:black; padding:10px; border-radius:8px; margin-bottom:10px; display:none; text-align:center; font-weight:bold;">
    ⚠️ Default password admin/admin — change in Network &amp; Cloud
  </div>

  <div class="grid">
    <!-- MAIN STATUS -->
    <div class="card">
        <div class="gauge">
            <div id="state_text" style="color:#888; font-weight:bold; letter-spacing:2px; margin-bottom:5px">LOADING...</div>
            <div id="alarm_badge" style="margin-bottom:8px; font-size:0.9rem; font-weight:bold; color:#888">---</div>
            <button id="btn_arm" onclick="toggleArm()" style="width:auto; padding:8px 20px; margin-bottom:10px; background:#3700b3">ARM</button>
            <div class="big-val" id="dist_val" style="color:var(--accent)">---</div>
            <div class="unit">DISTANCE (cm)</div>
            <svg class="spark" id="graph_dist"></svg>
        </div>
        <div style="display:flex; gap:10px">
            <div style="flex:1; text-align:center">
                <div style="color:#03dac6; font-weight:bold" id="mov_val">0%</div>
                <div class="unit">MOVEMENT</div>
                <svg class="spark" id="graph_mov" style="height:30px; stroke:#03dac6"></svg>
            </div>
            <div style="flex:1; text-align:center">
                <div style="color:#bb86fc; font-weight:bold" id="stat_val">0%</div>
                <div class="unit">STATIC</div>
                <svg class="spark" id="graph_stat" style="height:30px; stroke:#bb86fc"></svg>
            </div>
        </div>
    </div>

    <!-- HEALTH & STATS -->
    <div class="card">
        <div class="stat-row"><span>Sensor Health</span><span id="h_score" class="stat-val">---%</span></div>
        <div class="stat-row"><span>UART State</span><span id="h_uart">---</span></div>
        <div class="stat-row"><span>Frame Rate</span><span id="h_fps">--- FPS</span></div>
        <div class="stat-row"><span>Communication Errors</span><span id="h_err" style="color:var(--warn)">0</span></div>
        <div class="stat-row"><span>RAM (Free/Min)</span><span id="h_heap">--- / --- KB</span></div>
        <div class="stat-row"><span>Chip Temperature</span><span id="h_temp">--- °C</span></div>
        <div class="stat-row"><span>Uptime</span><span id="h_uptime">---</span></div>
        <div style="display:flex; gap:5px; margin-top:10px; flex-wrap: wrap;">
            <button class="sec" style="flex:1; min-width:80px;" onclick="api('radar/restart', {method:'POST'})">Restart Radar</button>
            <button class="sec" style="flex:1; min-width:80px;" onclick="if(confirm('Restart ESP?')) api('restart', {method:'POST'})">Restart ESP</button>
            <button class="warn" style="flex:1; min-width:80px;" onclick="if(confirm('Really perform radar factory reset?')) api('radar/factory_reset', {method:'POST'})">Reset MW</button>
        </div>
    </div>

    <!-- CONTROLS -->
    <div class="card">
        <div class="tabs">
            <div class="tab active" onclick="tab(0)">Basic</div>
            <div class="tab" onclick="tab(1)">Security</div>
            <div class="tab" onclick="tab(2)">Gates</div>
            <div class="tab" onclick="tab(3)">Network & Cloud</div>
            <div class="tab" onclick="tab(4)">Zones</div>
            <div class="tab" onclick="tab(5)">History</div>
            <div class="tab" onclick="tab(6)">WiFi CSI</div>
        </div>

        <!-- TAB 0: BASIC -->
        <div id="tab0">
            <div class="stat-row"><span>Device Name (mDNS)</span></div>
            <div style="display:flex; gap:5px; margin-bottom:10px">
                <input type="text" id="txt_hostname" placeholder="e.g. sensor-room1">
                <button class="sec" style="width:auto; margin:0" onclick="saveHostname()">OK</button>
            </div>

            <div class="row-input">
                <span style="flex:1">Min Range (Gate)</span>
                <input type="number" id="i_min" min="0" max="13" style="width:60px" onchange="saveBasic()">
            </div>
            <div class="row-input">
                <span style="flex:1">Max Range (Gate)</span>
                <input type="number" id="i_max" min="1" max="13" style="width:60px" onchange="saveBasic()">
            </div>
            
            <div class="stat-row" style="margin-top:10px"><span>Hold Time (ms)</span></div>
            <input type="number" id="i_hold" step="1000" onchange="saveBasic()">
            
            <div class="stat-row" style="margin-top:10px"><span>Movement Sensitivity (%)</span></div>
            <input type="number" id="i_sens" min="0" max="100" onchange="saveBasic()">

            <div style="display:flex; align-items:center; gap:8px; margin-top:15px; margin-bottom:5px">
                <input type="checkbox" id="chk_led" style="width:auto" onchange="saveBasic()">
                <label for="chk_led">Enable LED (Indicator)</label>
            </div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:10px">
                <input type="checkbox" id="chk_eng" style="width:auto" onchange="toggleEng()">
                <label for="chk_eng" title="Enable detailed gate data (14 zones) and faster communication">Enable Diagnostics</label>
            </div>
            
            <button id="btn_calib" onclick="startCalib()" style="margin-top:15px">Calibrate Noise (60s)</button>
        </div>

        <!-- TAB 1: SECURITY -->
        <div id="tab1" class="hidden">
            <div class="section-title">ANTI-MASKING (Tamper by Covering)</div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_am_en" style="width:auto" onchange="saveSec()">
                <label for="chk_am_en">Enable silence alarm</label>
            </div>
            <div class="row-input">
                <span>Timeout (sec)</span>
                <input type="number" id="i_am" placeholder="300" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                ⚠️ For warehouses, cabins, server rooms <b>DISABLE</b> — silence is normal there.<br>
                For occupied spaces ENABLE — detects sensor covering.
            </p>

            <div class="section-title">LOITERING (Suspicious Lingering)</div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_loit_en" style="width:auto" onchange="saveSec()">
                <label for="chk_loit_en">Loitering notification</label>
            </div>
            <div class="row-input">
                <span>Timeout (sec)</span>
                <input type="number" id="i_loit" placeholder="15" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                Alarm when someone stands &lt;2m from sensor longer than timeout.
            </p>

            <div class="section-title">HEARTBEAT (Periodic Report)</div>
            <div class="row-input">
                <span>Interval (hours)</span>
                <input type="number" id="i_hb" placeholder="4" min="0" max="24" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                0 = disabled, 4 = every 4 hours "I'm OK" message.
            </p>

            <div class="section-title">PET IMMUNITY</div>
            <div class="row-input">
                <span>Min. movement energy</span>
                <input type="number" id="i_pet" placeholder="10" min="0" max="50" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0">
                Filters small objects (cats, dogs) with low energy &lt;2m.
            </p>

            <div class="section-title">ALARM DELAY</div>
            <div class="row-input">
                <span>Entry Delay (sec)</span>
                <input type="number" id="i_entry_dl" placeholder="30" min="0" max="300" style="width:80px" onchange="saveAlarmConfig()">
            </div>
            <div class="row-input">
                <span>Exit Delay (sec)</span>
                <input type="number" id="i_exit_dl" placeholder="30" min="0" max="300" style="width:80px" onchange="saveAlarmConfig()">
            </div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_dis_rem" style="width:auto" onchange="saveAlarmConfig()">
                <label for="chk_dis_rem">Reminder "Still DISARMED"</label>
            </div>

            <div class="section-title">ABSENCE TIMEOUT</div>
            <div class="row-input">
                <span>Unmanned Duration (sec)</span>
                <input type="number" id="i_timeout" placeholder="10" min="0" max="255" style="width:80px" onchange="saveTimeout()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                Duration after which radar reports "no presence" without detection.
            </p>

            <div class="section-title">LIGHT SENSOR (OUT pin)</div>
            <div class="row-input">
                <span>Light Function</span>
                <select id="sel_light_func" style="width:140px" onchange="saveLightConfig()">
                    <option value="0">Disabled</option>
                    <option value="1">Night mode (below threshold)</option>
                    <option value="2">Day mode (above threshold)</option>
                </select>
            </div>
            <div class="row-input">
                <span>Light Threshold (0-255)</span>
                <input type="number" id="i_light_thresh" placeholder="128" min="0" max="255" style="width:80px" onchange="saveLightConfig()">
            </div>
            <div class="row-input">
                <span>Current Light</span>
                <span id="cur_light_val" style="font-weight:bold">---</span>
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 0 0">
                OUT pin active only when light is below/above threshold.<br>
                Ideal for night security ("below threshold" mode).
            </p>
        </div>

        <!-- TAB 2: GATES -->
        <div id="tab2" class="hidden">
            <div id="range_summary" style="font-size:0.8rem; color:#888; margin-bottom:8px; text-align:center"></div>

            <div style="font-size:0.75rem; color:#888; margin-bottom:8px; line-height:1.5">
                <span style="color:#03dac6; font-weight:bold">&#9632; Movement</span> = sensitivity to moving objects &middot;
                <span style="color:#bb86fc; font-weight:bold">&#9632; Static</span> = sensitivity to stationary objects<br>
                Higher = more sensitive &middot; <span style="opacity:0.4">gray = outside min/max range</span>
            </div>

            <div style="background:#111; border-radius:8px; padding:8px; margin-bottom:8px">
                <div style="font-size:0.75rem; color:#888; margin-bottom:4px">Set all:</div>
                <div style="display:flex; gap:6px; align-items:center; flex-wrap:wrap">
                    <span style="color:#03dac6; font-size:0.75rem; width:12px">P</span>
                    <input type="range" class="mov-slider" id="g_m_all" value="50" min="0" max="100" style="flex:1; min-width:60px" oninput="$('lm_all').innerText=this.value">
                    <span id="lm_all" style="width:22px; color:#03dac6; font-size:0.75rem; text-align:right">50</span>
                    <span style="color:#bb86fc; font-size:0.75rem; width:12px; margin-left:4px">S</span>
                    <input type="range" class="stat-slider" id="g_s_all" value="30" min="0" max="100" style="flex:1; min-width:60px" oninput="$('ls_all').innerText=this.value">
                    <span id="ls_all" style="width:22px; color:#bb86fc; font-size:0.75rem; text-align:right">30</span>
                    <button class="sec" style="width:auto; padding:4px 10px; margin:0; font-size:0.75rem" onclick="setAllGates()">OK</button>
                </div>
            </div>

            <div style="display:flex; justify-content:space-between; margin-bottom:8px; gap:5px">
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('indoor')">Indoor</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('outdoor')">Outdoor</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('pet')">Pets</button>
            </div>

            <div id="gates_container" style="max-height:400px; overflow-y:auto"></div>

            <button onclick="saveGates()" style="margin-top:8px">Save Gates</button>
        </div>

        <!-- TAB 3: NETWORK & CLOUD -->
        <div id="tab3" class="hidden">
            <div class="section-title">MQTT Broker</div>
            <div style="display:flex; align-items:center; gap:8px;">
                <input type="checkbox" id="chk_mqtt_en" style="width:auto">
                <label for="chk_mqtt_en">Enable MQTT</label>
            </div>
            <input type="text" id="txt_mqtt_server" placeholder="Server IP">
            <div style="display:flex; gap:5px">
                <input type="text" id="txt_mqtt_port" placeholder="Port (1883)">
                <input type="text" id="txt_mqtt_user" placeholder="Username">
            </div>
            <input type="password" id="txt_mqtt_pass" placeholder="Password">
            <button onclick="saveMQTTConfig()" class="sec">Save MQTT</button>

            <div class="section-title">Telegram Notifications</div>
            <div style="display:flex; align-items:center; gap:8px;">
                <input type="checkbox" id="chk_tg_en" style="width:auto">
                <label for="chk_tg_en">Enable Bot</label>
            </div>
            <input type="text" id="txt_tg_token" placeholder="Bot Token">
            <input type="text" id="txt_tg_chat" placeholder="Chat ID">
            <div style="display:flex; gap:5px">
                <button onclick="saveTelegram()" class="sec">Save</button>
                <button onclick="testTelegram()" class="sec">Test</button>
            </div>
            <div class="section-title">ACCESS CREDENTIALS</div>
            <input type="text" id="txt_auth_user" placeholder="Username">
            <input type="password" id="txt_auth_pass" placeholder="New Password">
            <input type="password" id="txt_auth_pass2" placeholder="Confirm Password">
            <button onclick="saveAuth()" class="warn">Change Password</button>
        </div>

        <!-- TAB 4: ZONES -->
        <div id="tab4" class="hidden">
            <div class="label" style="margin-bottom:10px; font-size:0.8rem; color:#888">ZONE DEFINITIONS (cm)</div>
            <!-- SVG Zone Map -->
            <div style="position:relative; margin-bottom:10px">
                <svg id="zone_map" width="100%" height="48" style="display:block"></svg>
                <div style="position:absolute; bottom:2px; left:4px; font-size:0.65rem; color:#555" id="zone_map_scale"></div>
            </div>
            <div id="zones_list"></div>
            <button onclick="addZone()" class="sec" style="margin-top:10px">+ Add Zone</button>
            <button onclick="saveZones()">💾 Save Zones</button>
            <!-- Auto-learn -->
            <div style="background:#1a1a2e; border-radius:6px; padding:8px; margin-top:10px">
                <div style="display:flex; gap:6px; align-items:center; flex-wrap:wrap">
                    <select id="learn_dur" style="flex:1; min-width:120px">
                        <option value="60">1 min</option>
                        <option value="180" selected>3 min</option>
                        <option value="300">5 min</option>
                        <option value="600">10 min</option>
                        <option value="1800">30 min</option>
                        <option value="3600">60 min</option>
                        <option value="14400">4 hrs</option>
                        <option value="28800">8 hrs</option>
                    </select>
                    <button onclick="startLearn()" id="btn_learn" class="sec" style="flex:1">📡 Learn Static</button>
                </div>
                <div id="learn_status" style="margin-top:6px; font-size:0.8rem; color:#888; display:none"></div>
            </div>
        </div>

        <!-- TAB 5: EVENT TIMELINE -->
        <div id="tab5" class="hidden">
            <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; flex-wrap:wrap; gap:6px">
                <div class="section-title" style="margin:0; border:none">EVENT TIMELINE</div>
                <div style="display:flex; gap:4px; align-items:center; flex-wrap:wrap">
                    <span id="evt_total" style="font-size:0.75rem; color:#666"></span>
                    <select id="evt_filter" onchange="loadEvents()" style="width:auto; padding:4px 8px; font-size:0.8rem">
                        <option value="-1">All</option>
                        <option value="5">Alarm</option>
                        <option value="1">Movement</option>
                        <option value="2">Tamper</option>
                        <option value="4">Heartbeat</option>
                        <option value="0">System</option>
                        <option value="3">Network</option>
                    </select>
                    <button onclick="exportEvents()" class="sec" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem">CSV</button>
                    <button onclick="clearEvents()" class="warn" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem">Delete</button>
                </div>
            </div>
            <!-- Timeline visual bar (last 24h density) -->
            <svg id="evt_timeline" viewBox="0 0 288 32" preserveAspectRatio="none"
                 style="width:100%; height:32px; background:#111; border-radius:4px; margin-bottom:8px">
            </svg>
            <div style="display:flex; justify-content:space-between; font-size:0.65rem; color:#555; margin:-4px 0 8px 0">
                <span>-24h</span><span>-12h</span><span>now</span>
            </div>
            <!-- Event list -->
            <div id="evt_timeline_list" style="max-height:400px; overflow-y:auto"></div>
            <button id="evt_load_more" onclick="loadMoreEvents()" class="sec" style="display:none; margin-top:8px; font-size:0.85rem">Load more...</button>
        </div>

        <!-- TAB 6: WIFI CSI -->
        <div id="tab6" class="hidden">
            <div id="csi_compiled_warn" style="display:none; padding:10px; background:#3a1010; border-left:3px solid var(--warn); margin-bottom:12px; font-size:0.85rem">
                ⚠️ This firmware was not compiled with WiFi CSI support (missing <code>-D USE_CSI=1</code>). To enable, upload the <b>esp32_poe_csi</b> variant.
            </div>

            <div class="section-title">CSI STATUS</div>
            <div class="stat-row"><span>Active</span><span id="csi_active_val" style="font-weight:bold">—</span></div>
            <div class="stat-row"><span>WiFi SSID</span><span id="csi_ssid_val">—</span></div>
            <div class="stat-row"><span>WiFi RSSI</span><span id="csi_rssi_val">—</span></div>
            <div class="stat-row"><span>Packets/s</span><span id="csi_pps_val">—</span></div>
            <div class="stat-row"><span>Idle baseline ready</span><span id="csi_idle_val">—</span></div>

            <div class="section-title">MOTION DETECTION</div>
            <div class="stat-row">
                <span>Motion State</span>
                <span id="csi_motion_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="stat-row"><span>Composite Score</span><span id="csi_comp_val">—</span></div>
            <div class="stat-row"><span>Variance (window)</span><span id="csi_var_val">—</span></div>
            <svg id="csi_graph" viewBox="0 0 100 50" style="width:100%; height:60px; background:#1a1a1a; margin-top:5px; stroke:#03dac6"></svg>

            <div class="section-title">FUSION (Radar + CSI)</div>
            <div class="stat-row">
                <span>Fusion stav</span>
                <span id="fus_presence_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="stat-row"><span>Confidence</span><span id="fus_conf_val">—</span></div>
            <div class="stat-row"><span>Zdroj detekce</span><span id="fus_source_val">—</span></div>
            <div class="stat-row">
                <span>Fusion povoleno</span>
                <label class="switch"><input type="checkbox" id="fus_en" onchange="toggleFusion(this.checked)"><span class="slider"></span></label>
            </div>

            <div class="section-title">KONFIGURACE</div>

            <div class="stat-row">
                <span>Povoleno</span>
                <label class="switch"><input type="checkbox" id="csi_en"><span class="slider"></span></label>
            </div>

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                Detection threshold (variance): <span id="csi_thr_lbl">0.50</span>
            </label>
            <input type="range" id="csi_thr" min="0.01" max="3.0" step="0.01" value="0.5"
                   oninput="$('csi_thr_lbl').innerText=parseFloat(this.value).toFixed(2)">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                Hystereze (exit multiplier): <span id="csi_hyst_lbl">0.70</span>
            </label>
            <input type="range" id="csi_hyst" min="0.30" max="0.95" step="0.01" value="0.7"
                   oninput="$('csi_hyst_lbl').innerText=parseFloat(this.value).toFixed(2)">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                Velikost okna (vzorky): <span id="csi_win_lbl">75</span>
            </label>
            <input type="range" id="csi_win" min="10" max="200" step="5" value="75"
                   oninput="$('csi_win_lbl').innerText=this.value">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                Interval publikace (ms): <span id="csi_pub_lbl">1000</span>
            </label>
            <input type="range" id="csi_pub" min="200" max="5000" step="100" value="1000"
                   oninput="$('csi_pub_lbl').innerText=this.value">

            <button onclick="saveCSIConfig()" style="margin-top:12px">Save Configuration</button>

            <div class="section-title">AKCE</div>
            <div style="display:flex; flex-wrap:wrap; gap:6px">
                <button class="sec" style="flex:1; min-width:120px" onclick="csiCalibrate()">📐 Auto-kalibrace prahu (10s)</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiResetBaseline()">♻️ Reset idle baseline</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiReconnect()">📶 Reconnect WiFi</button>
            </div>
            <div id="csi_calib_bar" style="display:none; height:6px; background:#333; margin-top:8px; border-radius:3px; overflow:hidden">
                <div id="csi_calib_fill" style="height:100%; width:0%; background:var(--accent); transition:width 0.3s"></div>
            </div>
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <b>Auto-calibration:</b> 10 seconds variance sampling in idle, sets threshold = mean × 1.5. Use when room is empty.<br>
                <b>Reset baseline:</b> clears learned idle values (turbulence, phase). After moving the sensor.<br>
                <b>Reconnect WiFi:</b> restarts WiFi association to fix interruptions / RSSI drops.
            </div>
        </div>
    </div>

    <!-- OTA -->
    <div class="card">
        <div class="stat-row"><span>Aktualizace FW</span></div>
        <input type="file" id="fw_file" accept=".bin">
        <div id="ota_bar" style="height:5px; background:#333; margin-top:5px; width:0%; transition:width 0.2s; background:var(--accent)"></div>
        <button onclick="uploadFW()">Upload Firmware</button>
    </div>
  </div>

  <div id="toast">Saved</div>

<script>
// --- CORE ---
const $ = id => document.getElementById(id);
const api = (ep, opts={}) => {
    // ESPAsyncWebServer hasParam() only checks query params, not POST body
    // Convert URLSearchParams body to query string automatically
    if(opts.body instanceof URLSearchParams) {
        ep += (ep.includes('?') ? '&' : '?') + opts.body.toString();
        delete opts.body;
    }
    return fetch('/api/'+ep, opts).then(r => {
        if(r.ok) showToast("OK"); else showToast("Chyba");
        return r;
    });
};
function showToast(msg) { $('toast').innerText=msg; $('toast').style.opacity=1; setTimeout(()=>$('toast').style.opacity=0, 2000); }

// --- DATA STREAM ---
let histDist = new Array(60).fill(0);
let histMov = new Array(60).fill(0);
let histStat = new Array(60).fill(0);
let histCsiComp = new Array(60).fill(0);
let zones = [];
let gateResolution = 0.75, cfgMinGate = 0, cfgMaxGate = 13;

let evtSource = null;
let reconnectTimeout = null;

function connectSSE() {
    if (evtSource) {
        evtSource.close();
    }

    evtSource = new EventSource('/events');

    evtSource.addEventListener('telemetry', e => {
        const d = JSON.parse(e.data);
        updateUI(d);
        if(d.alarm_state) { alarmArmed = d.armed; updateAlarmUI(d.alarm_state); }
        if(d.gate_move && !$('tab2').classList.contains('hidden')) updateGatesUI(d);
        if(d.csi) updateCSIUI(d.csi);
    });

    evtSource.onerror = () => {
        console.log('SSE connection lost, reconnecting in 3s...');
        $('sse_icon').className = 'icon err';
        $('sse_icon').title = 'Connection lost';
        evtSource.close();
        if (reconnectTimeout) clearTimeout(reconnectTimeout);
        reconnectTimeout = setTimeout(connectSSE, 3000);
    };

    evtSource.onopen = () => {
        console.log('SSE connected');
        $('sse_icon').className = 'icon ok';
        $('sse_icon').title = 'Realtime OK';
    };
}

function init() {
    // SSE Connection with auto-reconnect
    connectSSE();
    
    // Initial Load
    fetch('/api/version').then(r=>r.text()).then(v => $('fw_ver').innerText = v);
    
    fetch('/api/health').then(r=>r.json()).then(d => {
        if(d.is_default_pass) $('security_warning').style.display = 'block';
        if(d.auth_user) $('txt_auth_user').value = d.auth_user;
        if(d.hostname) $('txt_hostname').value = d.hostname;
        updateHealth(d);
    });
    
    // Load Configs
    loadMainConfig();
    loadSecurityConfig();
    loadMQTTConfig();
    loadTelegramConfig();
    loadZones();
    loadAlarmStatus();

    initCollapsible();
    
    setInterval(() => fetch('/api/health').then(r=>r.json()).then(updateHealth), 5000);
}

function loadMainConfig() {
    fetch('/api/config').then(r=>r.json()).then(d => {
        $('i_max').value = d.max_gate;
        if(d.min_gate !== undefined) $('i_min').value = d.min_gate;
        $('i_hold').value = d.hold_time;
        if(d.led_en !== undefined) $('chk_led').checked = d.led_en;
        if(d.eng_mode !== undefined) $('chk_eng').checked = d.eng_mode;
        if(d.mov_sens && d.mov_sens.length > 0) $('i_sens').value = d.mov_sens[0]; // Display first gate sens as general
        if(d.resolution) gateResolution = d.resolution;
        if(d.min_gate !== undefined) cfgMinGate = d.min_gate;
        if(d.max_gate !== undefined) cfgMaxGate = d.max_gate;

        // Range summary
        let minDist = (cfgMinGate * gateResolution * 100).toFixed(0);
        let maxDist = (cfgMaxGate * gateResolution * 100).toFixed(0);
        $('range_summary').innerHTML = `Coverage: <b>${minDist}cm – ${maxDist}cm</b> &middot; Resolution: ${gateResolution}m/gate`;

        // Gate Sliders (no energy bars — eng mode broken on V1.26)
        renderGateSliders(d.mov_sens, d.stat_sens);
    });
}

function updateUI(d) {
    // Sparklines
    histDist.push(d.distance_mm/10); histDist.shift();
    histMov.push(d.moving_energy); histMov.shift();
    histStat.push(d.static_energy); histStat.shift();
    
    drawSpark('graph_dist', histDist, 400); // max 400cm
    drawSpark('graph_mov', histMov, 100);
    drawSpark('graph_stat', histStat, 100);
    
    // Values
    $('dist_val').innerText = (d.distance_mm/10).toFixed(0);
    $('mov_val').innerText = d.moving_energy + '%';
    $('stat_val').innerText = d.static_energy + '%';
    
    let st = "KLID";
    let stColor = "#888";
    if(d.state === "detected") { st = "DETEKCE"; stColor = "var(--accent)"; }
    else if(d.state === "hold") { st = "HOLD"; stColor = "#bb86fc"; }
    if(d.tamper) { st = "TAMPER!"; stColor = "var(--warn)"; }
    $('state_text').innerText = st;
    $('state_text').style.color = stColor;
    drawZoneMap(d.raw_stat_dist, d.raw_mov_dist);
}

function renderGateSliders(mov, stat) {
    let h = '';
    for(let i=0; i<14; i++) {
        let dist = Math.round(i * gateResolution * 100);
        let active = (i >= cfgMinGate && i <= cfgMaxGate);
        let dimClass = active ? '' : ' gate-dimmed';
        let m = mov ? mov[i] : 50;
        let s = stat ? stat[i] : 30;
        h += `<div class="gate-wrapper${dimClass}">
            <div class="gate-label" style="width:65px; white-space:nowrap">G${i} <span style="color:#666">(${dist}cm)</span></div>
            <input type="range" class="mov-slider" id="g_m_${i}" value="${m}" min="0" max="100" title="Move G${i}" oninput="$('lm_${i}').innerText=this.value" style="flex:1">
            <span id="lm_${i}" style="width:22px; text-align:right; color:#03dac6; font-size:0.75rem">${m}</span>
            <input type="range" class="stat-slider" id="g_s_${i}" value="${s}" min="0" max="100" title="Static G${i}" oninput="$('ls_${i}').innerText=this.value" style="flex:1">
            <span id="ls_${i}" style="width:22px; text-align:right; color:#bb86fc; font-size:0.75rem">${s}</span>
        </div>`;
    }
    $('gates_container').innerHTML = h;
}

function setAllGates() {
    let m = $('g_m_all').value, s = $('g_s_all').value;
    for(let i=0; i<14; i++) {
        let el_m = $(`g_m_${i}`), el_s = $(`g_s_${i}`);
        if(el_m) { el_m.value = m; $(`lm_${i}`).innerText = m; }
        if(el_s) { el_s.value = s; $(`ls_${i}`).innerText = s; }
    }
}

function updateGatesUI(d) {
    // No-op: energy bars removed (eng mode broken on V1.26 FW)
}

function updateHealth(d) {
    let ethOk = d.eth_link || (d.ethernet && d.ethernet.link_up);
    $('wifi_icon').className = "icon " + (ethOk ? "ok" : "err");
    $('mqtt_icon').className = "icon " + (d.mqtt && d.mqtt.connected ? "ok" : "err");
    $('h_score').innerText = d.health_score + "%";
    $('h_uart').innerText = d.uart_state;
    $('h_fps').innerText = d.frame_rate.toFixed(1) + " FPS";
    $('h_err').innerText = d.error_count;
    $('h_heap').innerText = (d.free_heap/1024).toFixed(1) + " / " + (d.min_heap/1024).toFixed(1) + " KB";
    if (d.chip_temp != null) $('h_temp').innerText = d.chip_temp.toFixed(1) + " °C";
    let u = d.uptime;
    $('h_uptime').innerText = Math.floor(u/3600) + "h " + Math.floor((u%3600)/60) + "m";
}

// --- GRAPHS ---
function drawSpark(id, data, max) {
    const el = $(id);
    let pts = "";
    const w = 100 / (data.length - 1);
    data.forEach((v, i) => {
        const y = 50 - (Math.min(v, max) / max * 50);
        pts += `${i * w},${y} `;
    });
    el.innerHTML = `<polyline points="${pts}" style="fill:none;stroke:inherit;stroke-width:2" />`;
}

// --- ACTIONS ---
function initCollapsible() {
    document.querySelectorAll('.section-title').forEach(el => {
        el.style.cursor = 'pointer';
        // Add icon/indicator
        el.innerHTML += ' <span style="font-size:0.8em; float:right">▼</span>';
        
        el.onclick = () => {
            let next = el.nextElementSibling;
            while(next && !next.classList.contains('section-title')) {
                next.style.display = next.style.display === 'none' ? '' : 'none';
                next = next.nextElementSibling;
            }
        };
    });
}

function tab(n) {
    ['tab0','tab1','tab2','tab3','tab4','tab5','tab6'].forEach((id, i) => {
        $(id).classList.toggle('hidden', i !== n);
        document.querySelectorAll('.tab')[i].classList.toggle('active', i === n);
    });
    // Refresh config when switching tabs
    if(n===1) { loadSecurityConfig(); loadAlarmStatus(); }
    if(n===2) loadMainConfig();
    if(n===5) loadEvents();
    if(n===6) loadCSIConfig();
}

// --- WIFI CSI ---
function loadCSIConfig() {
    fetch('/api/csi').then(r=>r.json()).then(d => {
        // Compiled-in check
        $('csi_compiled_warn').style.display = d.compiled ? 'none' : 'block';
        if (!d.compiled) return;

        // Config sliders
        $('csi_en').checked = !!d.enabled;
        if (d.threshold !== undefined) {
            $('csi_thr').value = d.threshold;
            $('csi_thr_lbl').innerText = parseFloat(d.threshold).toFixed(2);
        }
        if (d.hysteresis !== undefined) {
            $('csi_hyst').value = d.hysteresis;
            $('csi_hyst_lbl').innerText = parseFloat(d.hysteresis).toFixed(2);
        }
        if (d.window !== undefined) {
            $('csi_win').value = d.window;
            $('csi_win_lbl').innerText = d.window;
        }
        if (d.publish_ms !== undefined) {
            $('csi_pub').value = d.publish_ms;
            $('csi_pub_lbl').innerText = d.publish_ms;
        }

        // Live status
        $('csi_active_val').innerText = d.active ? 'ANO' : 'NE';
        $('csi_active_val').style.color = d.active ? 'var(--accent)' : '#888';
        $('csi_ssid_val').innerText  = d.wifi_ssid || '—';
        $('csi_rssi_val').innerText  = (d.wifi_rssi !== undefined && d.wifi_rssi !== 0) ? (d.wifi_rssi + ' dBm') : '—';
        $('csi_pps_val').innerText   = (d.pps !== undefined) ? d.pps.toFixed(1) : '—';
        $('csi_idle_val').innerText  = d.idle_ready ? 'YES' : 'NO — collecting samples';

        if (d.motion !== undefined) {
            $('csi_motion_val').innerText = d.motion ? 'POHYB' : 'KLID';
            $('csi_motion_val').style.color = d.motion ? 'var(--accent)' : '#888';
        }
        if (d.composite !== undefined) $('csi_comp_val').innerText = d.composite.toFixed(4);
        if (d.variance !== undefined) $('csi_var_val').innerText = d.variance.toFixed(4);

        // Fusion
        $('fus_en').checked = !!d.fusion_enabled;
        if (d.fusion) {
            updateFusionUI(d.fusion);
        }
    });
}

function updateCSIUI(csi) {
    // Called from SSE telemetry handler with the `csi` sub-object
    if (!csi) return;
    if ($('tab6').classList.contains('hidden')) {
        // Tab not visible — only push history so sparkline keeps animating
        histCsiComp.push(csi.composite || 0); histCsiComp.shift();
        return;
    }
    histCsiComp.push(csi.composite || 0); histCsiComp.shift();
    drawSpark('csi_graph', histCsiComp, 2.0);

    if (csi.motion !== undefined) {
        $('csi_motion_val').innerText = csi.motion ? 'POHYB' : 'KLID';
        $('csi_motion_val').style.color = csi.motion ? 'var(--accent)' : '#888';
    }
    if (csi.composite !== undefined) $('csi_comp_val').innerText = csi.composite.toFixed(4);
    if (csi.variance !== undefined)  $('csi_var_val').innerText  = csi.variance.toFixed(4);
    if (csi.pps !== undefined)       $('csi_pps_val').innerText  = csi.pps.toFixed(1);
    if (csi.rssi !== undefined && csi.rssi !== 0) $('csi_rssi_val').innerText = csi.rssi + ' dBm';

    // Calibration progress bar
    if (csi.calibrating) {
        $('csi_calib_bar').style.display = 'block';
        $('csi_calib_fill').style.width = ((csi.calib_pct || 0) * 100) + '%';
    } else {
        $('csi_calib_bar').style.display = 'none';
    }

    // Fusion live update (nested in SSE csi object)
    if (csi.fusion) updateFusionUI(csi.fusion);
}

function updateFusionUI(f) {
    if (!f) return;
    let srcLabels = {none:'—', radar:'Radar', csi:'CSI', both:'Radar + CSI'};
    $('fus_presence_val').innerText = f.presence ? 'DETEKCE' : 'KLID';
    $('fus_presence_val').style.color = f.presence ? 'var(--warn)' : '#888';
    $('fus_conf_val').innerText = (f.confidence !== undefined) ? (f.confidence * 100).toFixed(0) + '%' : '—';
    $('fus_source_val').innerText = srcLabels[f.source] || f.source || '—';
}

function toggleFusion(en) {
    let p = new URLSearchParams();
    p.append('fusion_enabled', en ? '1' : '0');
    api('csi', {method:'POST', body:p}).then(r=>r.json()).then(d => {
        showToast(en ? 'Fusion zapnut' : 'Fusion vypnut');
        if (!en) {
            $('fus_presence_val').innerText = '—';
            $('fus_conf_val').innerText = '—';
            $('fus_source_val').innerText = '—';
        }
    });
}

function saveCSIConfig() {
    let p = new URLSearchParams();
    p.append('enabled',    $('csi_en').checked ? '1' : '0');
    p.append('threshold',  $('csi_thr').value);
    p.append('hysteresis', $('csi_hyst').value);
    p.append('window',     $('csi_win').value);
    p.append('publish_ms', $('csi_pub').value);
    api('csi', {method:'POST', body:p}).then(r=>r.json()).then(d => {
        if (d.needs_restart) {
            if (confirm('Changing CSI requires ESP restart. Restart now?')) {
                api('restart', {method:'POST'});
            }
        }
    });
}

function csiCalibrate() {
    api('csi/calibrate', {method:'POST'}).then(r => {
        if (r.ok) showToast('Calibration started (10s, keep room empty)');
    });
}

function csiResetBaseline() {
    if (!confirm('Reset idle baseline? CSI will re-collect samples for a few seconds.')) return;
    api('csi/reset_baseline', {method:'POST'});
}

function csiReconnect() {
    api('csi/reconnect', {method:'POST'});
}

// --- Event Timeline ---
const EVT_TYPES = {
    0: {name:'SYS', color:'#888', icon:'⚙️'},
    1: {name:'MOV', color:'#03dac6', icon:'👤'},
    2: {name:'TMP', color:'#cf6679', icon:'🚨'},
    3: {name:'NET', color:'#bb86fc', icon:'🌐'},
    4: {name:'HB',  color:'#4caf50', icon:'💚'},
    5: {name:'SEC', color:'#ff9800', icon:'🔒'}
};
let evtOffset = 0;
let evtAllLoaded = false;

function evtTimeStr(ts) {
    if (ts > 1700000000) {
        let d = new Date(ts * 1000);
        return d.toLocaleString('cs-CZ', {day:'numeric',month:'numeric', hour:'2-digit',minute:'2-digit',second:'2-digit'});
    }
    return Math.floor(ts/3600) + "h " + Math.floor((ts%3600)/60) + "m " + (ts%60) + "s";
}

function renderTimelineBar(events) {
    // 24h density heatmap — 288 bins (5 min each)
    let now = Math.floor(Date.now() / 1000);
    let bins = new Array(288).fill(0);
    let maxBin = 1;
    let hasBins = false;
    events.forEach(e => {
        if (e.ts > 1700000000) {
            let age = now - e.ts;
            if (age >= 0 && age < 86400) {
                let bin = 287 - Math.floor(age / 300);
                if (bin >= 0 && bin < 288) { bins[bin]++; hasBins = true; }
            }
        }
    });
    if (!hasBins) { $('evt_timeline').innerHTML = '<text x="144" y="20" text-anchor="middle" fill="#444" font-size="10">Not enough data for timeline</text>'; return; }
    for (let i = 0; i < 288; i++) if (bins[i] > maxBin) maxBin = bins[i];

    let svg = '';
    for (let i = 0; i < 288; i++) {
        if (bins[i] === 0) continue;
        let h = Math.max(2, (bins[i] / maxBin) * 28);
        let a = 0.3 + 0.7 * (bins[i] / maxBin);
        svg += `<rect x="${i}" y="${32-h}" width="1" height="${h}" fill="var(--accent)" opacity="${a.toFixed(2)}"/>`;
    }
    // SEC events highlighted in red
    events.forEach(e => {
        if (e.type === 5 && e.ts > 1700000000) {
            let age = now - e.ts;
            if (age >= 0 && age < 86400) {
                let bin = 287 - Math.floor(age / 300);
                svg += `<rect x="${bin}" y="0" width="1" height="32" fill="#ff9800" opacity="0.6"/>`;
            }
        }
    });
    $('evt_timeline').innerHTML = svg;
}

function renderEventList(events, append) {
    let container = $('evt_timeline_list');
    let h = append ? '' : '';
    events.forEach(e => {
        let t = EVT_TYPES[e.type] || EVT_TYPES[0];
        let timeStr = evtTimeStr(e.ts);
        let distStr = e.dist > 0 ? `<span style="color:#888">${e.dist}cm</span>` : '';
        h += `<div style="display:flex; gap:8px; padding:6px 4px; border-left:3px solid ${t.color}; margin-bottom:2px; background:#111; border-radius:0 4px 4px 0; align-items:flex-start">
            <div style="flex-shrink:0; width:20px; text-align:center">${t.icon}</div>
            <div style="flex:1; min-width:0">
                <div style="display:flex; justify-content:space-between; gap:8px; flex-wrap:wrap">
                    <span style="font-size:0.75rem; color:#666; white-space:nowrap">${timeStr}</span>
                    <span style="font-size:0.7rem; color:${t.color}; font-weight:bold">${t.name} ${distStr}</span>
                </div>
                <div style="font-size:0.82rem; margin-top:2px; word-break:break-word">${e.msg}</div>
            </div>
        </div>`;
    });
    if (append) { container.innerHTML += h; }
    else { container.innerHTML = h || '<div style="text-align:center; padding:20px; color:#555">No events</div>'; }
}

function loadEvents() {
    evtOffset = 0;
    evtAllLoaded = false;
    let typeFilter = $('evt_filter').value;
    let url = '/api/events?limit=50&type=' + typeFilter;
    fetch(url).then(r=>r.json()).then(d => {
        let events = d.events || [];
        let total = d.total || 0;
        $('evt_total').textContent = total + ' total';
        renderTimelineBar(events);
        renderEventList(events, false);
        evtOffset = events.length;
        evtAllLoaded = events.length >= (d.count !== undefined ? total : events.length);
        $('evt_load_more').style.display = (events.length >= 50 && !evtAllLoaded) ? 'block' : 'none';
    });
}

function loadMoreEvents() {
    let typeFilter = $('evt_filter').value;
    fetch('/api/events?limit=50&offset=' + evtOffset + '&type=' + typeFilter).then(r=>r.json()).then(d => {
        let events = d.events || [];
        renderEventList(events, true);
        evtOffset += events.length;
        if (events.length < 50) evtAllLoaded = true;
        $('evt_load_more').style.display = evtAllLoaded ? 'none' : 'block';
    });
}

function exportEvents() {
    window.open('/api/events/csv', '_blank');
}

function clearEvents() {
    if(confirm("Delete entire history?")) {
        api('events/clear', {method:'POST'}).then(() => loadEvents());
    }
}

function startCalib() {
    if(confirm("Start noise calibration? (60s, do not move in front of sensor)")) {
        api('radar/calibrate', {method:'POST'});
    }
}

function saveBasic() {
    let m = $('i_max').value;
    let min = $('i_min').value;
    let h = $('i_hold').value;
    let s = $('i_sens').value;
    let l = $('chk_led').checked ? 1 : 0;
    api(`config`, {
        method: 'POST',
        body: new URLSearchParams({
            'gate': m,
            'min_gate': min,
            'hold': h,
            'mov': s,
            'led_en': l
        })
    }); 
}

function toggleEng() {
    let en = $('chk_eng').checked ? 1 : 0;
    api(`engineering`, {
        method: 'POST',
        body: new URLSearchParams({ 'enable': en })
    });
}

function loadSecurityConfig() {
    fetch('/api/security/config').then(r=>r.json()).then(d => {
        $('i_am').value = d.antimask_time || 300;
        $('chk_am_en').checked = d.antimask_enabled || false;
        $('i_loit').value = d.loiter_time || 15;
        $('chk_loit_en').checked = d.loiter_alert !== false;
        $('i_hb').value = d.heartbeat || 4;
        $('i_pet').value = d.pet_immunity || 0;
        $('i_rssi_thresh').value = d.rssi_threshold || -80;
        $('i_rssi_drop').value = d.rssi_drop || 20;
    }).catch(e => console.log('Security config not loaded'));

    // Load Light Config
    fetch('/api/radar/light').then(r=>r.json()).then(d => {
        if(d.function !== undefined) $('sel_light_func').value = d.function;
        if(d.threshold !== undefined) $('i_light_thresh').value = d.threshold;
        if(d.current_level !== undefined) $('cur_light_val').innerText = d.current_level;
    }).catch(e => console.log('Light config not loaded'));

    // Load Timeout (unmanned duration)
    fetch('/api/radar/timeout').then(r=>r.json()).then(d => {
        if(d.duration !== undefined) $('i_timeout').value = d.duration;
    }).catch(e => console.log('Timeout config not loaded'));
}

function saveLightConfig() {
    let func = $('sel_light_func').value;
    let thresh = $('i_light_thresh').value;
    api('radar/light', {
        method: 'POST',
        body: new URLSearchParams({ 'function': func, 'threshold': thresh })
    });
}

function saveTimeout() {
    let dur = $('i_timeout').value;
    api('radar/timeout', {
        method: 'POST',
        body: new URLSearchParams({ 'duration': dur })
    });
}

function saveSec() {
    let am = $('i_am').value;
    let am_en = $('chk_am_en').checked ? 1 : 0;
    let lo = $('i_loit').value;
    let lo_en = $('chk_loit_en').checked ? 1 : 0;
    let hb = $('i_hb').value;
    let pt = $('i_pet').value;
    let rt = $('i_rssi_thresh').value;
    let rd = $('i_rssi_drop').value;
    
    api(`security/config`, {
        method: 'POST',
        body: new URLSearchParams({
            'antimask': am,
            'antimask_en': am_en,
            'loiter': lo,
            'loiter_alert': lo_en,
            'heartbeat': hb,
            'pet': pt,
            'rssi_threshold': rt,
            'rssi_drop': rd
        })
    });
}

// MQTT Config
function loadMQTTConfig() {
    fetch('/api/health').then(r=>r.json()).then(d=>{
        if(d.mqtt) {
            $('chk_mqtt_en').checked = (d.mqtt.enabled !== false);
            $('txt_mqtt_server').value = d.mqtt.server || '';
            $('txt_mqtt_port').value = d.mqtt.port || '';
            $('txt_mqtt_user').value = d.mqtt.user || '';
        }
    });
}
function saveMQTTConfig() {
    let en = $('chk_mqtt_en').checked ? 1 : 0;
    let s = $('txt_mqtt_server').value;
    let p = $('txt_mqtt_port').value;
    let u = $('txt_mqtt_user').value;
    let pw = $('txt_mqtt_pass').value;
    
    api(`mqtt/config`, {
        method: 'POST',
        body: new URLSearchParams({
            'enabled': en,
            'server': s,
            'port': p,
            'user': u,
            'pass': pw
        })
    });
}

// Telegram
function loadTelegramConfig() {
    fetch('/api/telegram/config').then(r=>r.json()).then(d => {
        $('chk_tg_en').checked = d.enabled;
        $('txt_tg_token').value = d.token || '';
        $('txt_tg_chat').value = d.chat_id || '';
    });
}
function saveTelegram() {
    let en = $('chk_tg_en').checked ? 1 : 0;
    let t = $('txt_tg_token').value;
    let c = $('txt_tg_chat').value;
    api('telegram/config', {
        method: 'POST',
        body: new URLSearchParams({ 'enabled': en, 'token': t, 'chat_id': c })
    });
}
function testTelegram() {
    fetch('/api/telegram/test', {method:'POST'})
    .then(r => r.json())
    .then(d => {
        showToast(d.success ? "Telegram OK!" : "Error: " + (d.error || "Unknown"));
    })
    .catch(e => showToast("Chyba komunikace"));
}

// Zones Implementation
function loadZones() {
    fetch('/api/zones').then(r=>r.json()).then(d => { zones = d; renderZones(); }).catch(e=>zones=[]);
}
const ZONE_COLORS = ['#1a6b3a','#1a4a6b','#6b1a1a','#4a1a6b'];
function renderZones() {
    let h = '';
    zones.forEach((z, i) => {
        const ab = z.alarm_behavior ?? 0;
        h += `<div style="margin-bottom:5px; background:#222; padding:5px; border-radius:5px; border-left:3px solid ${ZONE_COLORS[ab]||'#444'}">
            <div style="display:flex; gap:5px; margin-bottom:5px">
                <input type="text" value="${z.name}" id="z_name_${i}" style="flex:2" placeholder="Name">
                <input type="number" value="${z.min}" id="z_min_${i}" style="flex:1" placeholder="Od (cm)">
                <input type="number" value="${z.max}" id="z_max_${i}" style="flex:1" placeholder="Do (cm)">
            </div>
            <div style="display:flex; gap:5px; align-items:center">
                <select id="z_lvl_${i}" style="flex:1">
                    <option value="0" ${z.level==0?'selected':''}>Log</option>
                    <option value="1" ${z.level==1?'selected':''}>Info</option>
                    <option value="2" ${z.level==2?'selected':''}>Warn</option>
                    <option value="3" ${z.level==3?'selected':''}>ALARM</option>
                </select>
                <select id="z_ab_${i}" style="flex:2" title="Zone alarm behavior">
                    <option value="0" ${ab==0?'selected':''}>⏱ Entry delay</option>
                    <option value="1" ${ab==1?'selected':''}>🚨 Immediate</option>
                    <option value="2" ${ab==2?'selected':''}>🔕 Ignorovat</option>
                    <option value="3" ${ab==3?'selected':''}>📡 Ignorovat statiku</option>
                </select>
                <input type="number" value="${z.delay||0}" id="z_del_${i}" style="flex:1" placeholder="Delay (ms)">
                <input type="checkbox" id="z_en_${i}" ${z.enabled!==false?'checked':''} style="width:auto">
                <button onclick="delZone(${i})" class="warn" style="width:auto; margin:0; padding:5px 10px">×</button>
            </div>
        </div>`;
    });
    $('zones_list').innerHTML = h;
    drawZoneMap();
}
function addZone() {
    zones.push({name: "Zone " + (zones.length+1), min: 0, max: 100, level: 0, alarm_behavior: 0, delay: 0, enabled: true});
    renderZones();
}
function delZone(i) {
    zones.splice(i, 1);
    renderZones();
}
function saveZones() {
    let newZones = [];
    zones.forEach((_, i) => {
        newZones.push({
            name: document.getElementById(`z_name_${i}`).value,
            min: parseInt(document.getElementById(`z_min_${i}`).value),
            max: parseInt(document.getElementById(`z_max_${i}`).value),
            level: parseInt(document.getElementById(`z_lvl_${i}`).value),
            alarm_behavior: parseInt(document.getElementById(`z_ab_${i}`).value),
            delay: parseInt(document.getElementById(`z_del_${i}`).value),
            enabled: document.getElementById(`z_en_${i}`).checked
        });
    });
    zones = newZones;
    fetch('/api/zones', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(zones)
    }).then(r => {
        if(r.ok) showToast("Zones saved");
        else showToast("Save error");
    });
}

// ── Zone Map ───────────────────────────────────────────────────────────────────
function drawZoneMap(statDist, movDist) {
    const svg   = $('zone_map');
    const scale = $('zone_map_scale');
    if (!svg) return;
    const W = svg.clientWidth || 300, H = 48, MAX = 1050; // 14 gates × 75cm
    let html = '';
    zones.forEach(z => {
        if (!z.enabled) return;
        const ab  = z.alarm_behavior ?? 0;
        const col = ZONE_COLORS[ab] || '#444';
        const x1  = Math.round(z.min / MAX * W);
        const x2  = Math.round(z.max / MAX * W);
        html += `<rect x="${x1}" y="4" width="${x2-x1}" height="${H-8}" fill="${col}" opacity="0.5" rx="3"/>`;
        html += `<text x="${x1+3}" y="16" font-size="9" fill="#aaa">${z.name}</text>`;
    });
    if (statDist > 0) {
        const sx = Math.round(statDist / MAX * W);
        html += `<line x1="${sx}" y1="0" y2="${H}" x2="${sx}" stroke="#bb86fc" stroke-width="2"/>`;
    }
    if (movDist > 0) {
        const mx = Math.round(movDist / MAX * W);
        html += `<line x1="${mx}" y1="0" y2="${H}" x2="${mx}" stroke="#03dac6" stroke-width="2"/>`;
    }
    svg.innerHTML = html;
    if (scale) scale.innerText = '0cm' + ' '.repeat(10) + '525cm' + ' '.repeat(10) + '1050cm';
}

// ── Auto-learn ────────────────────────────────────────────────────────────────
let learnPollTimer = null;
function startLearn() {
    const dur = $('learn_dur').value;
    api(`radar/learn-static?duration=${dur}`, { method: 'POST' }).then(r => {
        if (r.ok) {
            $('btn_learn').disabled = true;
            $('learn_status').style.display = 'block';
            $('learn_status').innerText = 'Starting...';
            learnPollTimer = setInterval(pollLearn, 3000);
        }
    });
}
function pollLearn() {
    fetch('/api/radar/learn-static').then(r=>r.json()).then(d => {
        const stat = $('learn_status');
        if (!d.active && d.progress === 100) {
            clearInterval(learnPollTimer);
            $('btn_learn').disabled = false;
            let txt = `✅ Hotovo! Top gate: ${d.top_gate} (~${d.top_cm}cm), confidence: ${d.confidence}%`;
            if (d.suggest_ready) {
                txt += ` <button onclick="applyLearnZone(${d.suggest_min_cm},${d.suggest_max_cm})" class="sec" style="padding:2px 8px; margin-left:6px">Apply</button>`;
            } else {
                txt += ' ⚠️ Not enough data.';
            }
            stat.innerHTML = txt;
        } else {
            stat.innerText = `⏳ ${d.progress}% | Static: ${d.static_freq_pct}% | Top gate: ${d.top_gate} (~${d.top_cm}cm)`;
        }
    });
}
function applyLearnZone(minCm, maxCm) {
    zones.push({name: 'Static-auto', min: minCm, max: maxCm, level: 0, alarm_behavior: 3, delay: 0, enabled: true});
    renderZones();
    $('learn_status').innerHTML += ' &nbsp;<b>→ Zone added, don't forget to save!</b>';
}

function saveGates() {
    let mov = [], stat = [];
    for(let i=0; i<14; i++) {
        mov.push(parseInt($(`g_m_${i}`).value));
        stat.push(parseInt($(`g_s_${i}`).value));
    }
    fetch('/api/radar/gates', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({mov, stat})
    }).then(r => {
        if(r.ok) showToast("Gates saved");
        else showToast("Error saving gates");
    });
}
function setPreset(t) {
    fetch('/api/preset?name=' + t, {method:'POST'}).then(r => {
        if(!r.ok) { showToast("Chyba presetu"); return; }
        showToast("Preset " + t + " applied");
        // Re-fetch config and update sliders in-place (no reload)
        fetch('/api/config').then(r=>r.json()).then(d => {
            if(d.mov_sens && d.stat_sens) renderGateSliders(d.mov_sens, d.stat_sens);
        });
    });
}
function saveHostname() { 
    let hn = $('txt_hostname').value;
    api(`config`, {
        method: 'POST',
        body: new URLSearchParams({ 'hostname': hn })
    });
}

function uploadFW() {
    let f = $('fw_file').files[0];
    if(!f) return;
    let fd = new FormData();
    fd.append('firmware', f);
    let xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update');
    xhr.upload.onprogress = e => $('ota_bar').style.width = (e.loaded/e.total*100) + "%";
    xhr.onload = () => alert("Restarting...");
    xhr.send(fd);
}

// --- ALARM ---
let alarmArmed = false;
function loadAlarmStatus() {
    fetch('/api/alarm/status').then(r=>r.json()).then(d => {
        alarmArmed = d.armed;
        updateAlarmUI(d.state);
        $('i_entry_dl').value = d.entry_delay || 30;
        $('i_exit_dl').value = d.exit_delay || 30;
        $('chk_dis_rem').checked = d.disarm_reminder !== false;
    }).catch(()=>{});
}
function updateAlarmUI(state) {
    let badge = $('alarm_badge');
    let btn = $('btn_arm');
    if(state === 'disarmed') { badge.innerText = '🔓 DISARMED'; badge.style.color='#888'; btn.innerText='ARM'; btn.style.background='#b00020'; }
    else if(state === 'arming') { badge.innerText = '⏳ ARMING...'; badge.style.color='orange'; btn.innerText='CANCEL'; btn.style.background='#3700b3'; }
    else if(state === 'armed_away') { badge.innerText = '🔒 ARMED'; badge.style.color='#00ff00'; btn.innerText='DISARM'; btn.style.background='#3700b3'; }
    else if(state === 'pending') { badge.innerText = '⚠️ PENDING'; badge.style.color='orange'; btn.innerText='DISARM'; btn.style.background='#3700b3'; }
    else if(state === 'triggered') { badge.innerText = '🚨 TRIGGERED'; badge.style.color='red'; btn.innerText='DISARM'; btn.style.background='#3700b3'; }
}
function toggleArm() {
    if(alarmArmed) {
        api('alarm/disarm', {method:'POST'}).then(()=>{ alarmArmed=false; loadAlarmStatus(); });
    } else {
        api('alarm/arm', {method:'POST'}).then(()=>{ alarmArmed=true; loadAlarmStatus(); });
    }
}
function saveAlarmConfig() {
    let ed = $('i_entry_dl').value;
    let xd = $('i_exit_dl').value;
    let dr = $('chk_dis_rem').checked ? 1 : 0;
    api('alarm/config', {
        method: 'POST',
        body: new URLSearchParams({ 'entry_delay': ed, 'exit_delay': xd, 'disarm_reminder': dr })
    });
}

function saveAuth() {
    let u = $('txt_auth_user').value;
    let p = $('txt_auth_pass').value;
    let p2 = $('txt_auth_pass2').value;
    if(!u || !p) { showToast("Enter username and password"); return; }
    if(p !== p2) { showToast("Passwords don't match"); return; }
    if(u.length < 4 || p.length < 4) { showToast("Min. 4 znaky"); return; }

    fetch(`/api/auth/config?user=${encodeURIComponent(u)}&pass=${encodeURIComponent(p)}`, {
        method: 'POST'
    }).then(r => {
        if(r.ok) { showToast("Password changed"); alert("Credentials changed. Device will restart."); }
        else r.text().then(t => showToast(t || "Chyba"));
    });
}

window.onload = init;
</script>
</body>
</html>
)rawliteral";

#endif
