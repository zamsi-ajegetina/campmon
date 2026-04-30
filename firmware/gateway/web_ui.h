#pragma once

// Embedded HTML for the Gateway Control Web Page — served at http://<gateway-ip>/
// Shows: gateway online indicator, per-node status, LDR reading, LED control.
// No temperature or humidity — field nodes carry LDR only.

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Campus IoT Gateway</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee; padding: 20px; }

  header {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 8px;
  }
  header h1 { color: #e94560; font-size: 1.4rem; }
  .gw-pill {
    display: flex; align-items: center; gap: 6px;
    background: #1b5e20; color: #a5d6a7;
    font-size: 0.75rem; font-weight: bold;
    padding: 4px 12px; border-radius: 20px;
  }
  .gw-dot {
    width: 8px; height: 8px; border-radius: 50%; background: #66bb6a;
    animation: pulse 2s ease-in-out infinite;
  }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }

  .section-label {
    font-size: 0.72rem; text-transform: uppercase; letter-spacing: 1.5px;
    color: #555; margin: 20px 0 12px;
  }

  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(230px, 1fr)); gap: 16px; }

  .card {
    background: #16213e; border-radius: 12px; padding: 18px;
    border: 2px solid #0f3460;
    box-shadow: 0 4px 12px rgba(0,0,0,0.4);
    transition: border-color 0.3s, opacity 0.3s;
  }
  .card.is-offline { border-color: #5c1a1a; opacity: 0.65; }

  .card-head {
    display: flex; justify-content: space-between; align-items: flex-start;
    margin-bottom: 14px;
  }
  .node-name { font-size: 0.95rem; color: #e94560; font-weight: bold; }
  .node-via  { font-size: 0.68rem; color: #555; margin-top: 3px; }

  .badge {
    font-size: 0.68rem; font-weight: bold;
    padding: 3px 10px; border-radius: 20px; white-space: nowrap;
  }
  .badge.online  { background: #1b5e20; color: #a5d6a7; }
  .badge.offline { background: #5c1a1a; color: #ef9a9a; }

  .ldr-block { margin-bottom: 16px; }
  .ldr-label { font-size: 0.7rem; color: #666; margin-bottom: 2px; }
  .ldr-value { font-size: 1.8rem; font-weight: bold; color: #00d4ff; line-height: 1; }
  .ldr-unit  { font-size: 0.7rem; color: #555; }

  .led-status {
    display: flex; align-items: center; gap: 6px;
    margin-bottom: 14px;
  }
  .led-dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: #263238; border: 1px solid #37474f;
    transition: background 0.3s, box-shadow 0.3s;
  }
  .led-dot.is-on {
    background: #ffeb3b;
    box-shadow: 0 0 6px 2px rgba(255,235,59,0.6);
  }
  .led-label { font-size: 0.72rem; color: #666; }
  .led-state { font-size: 0.72rem; font-weight: bold; color: #555; }
  .led-state.is-on { color: #ffeb3b; }

  .btn-row { display: flex; gap: 6px; }
  .btn {
    flex: 1; padding: 9px 4px; border: none; border-radius: 8px;
    cursor: pointer; font-size: 0.82rem; font-weight: bold;
    background: #0d1b38; color: #555; transition: 0.15s;
  }
  .btn:hover { opacity: 0.8; }
  .btn.led-on  { background: #e94560; color: #fff; }
  .btn.led-off { background: #263238; color: #ccc; }

  footer { text-align: center; color: #3a3a5c; margin-top: 30px; font-size: 0.72rem; }
</style>
</head>
<body>

<header>
  <h1>Campus IoT Gateway</h1>
  <div class="gw-pill"><div class="gw-dot"></div>Gateway Online</div>
</header>

<div class="section-label">Field Nodes</div>
<div class="grid" id="nodes"></div>
<footer>Ashesi University &mdash; CSIS IoT &mdash; Auto-refresh every 10 s</footer>

<script>
const NODES = [
  { id: 1, name: "A &mdash; Mech Workshop", via: "LoRa" },
  { id: 2, name: "B &mdash; PNRB",          via: "nRF24 &rarr; Node C relay" },
  { id: 3, name: "C &mdash; Eng Block",      via: "nRF24" },
  { id: 4, name: "D &mdash; Fab Lab",        via: "nRF24 direct" }
];

function buildCard(n) {
  return `
  <div class="card is-offline" id="card-${n.id}">
    <div class="card-head">
      <div>
        <div class="node-name">Node ${n.name}</div>
        <div class="node-via">${n.via}</div>
      </div>
      <span class="badge offline" id="badge-${n.id}">Offline</span>
    </div>
    <div class="ldr-block">
      <div class="ldr-label">Light (LDR)</div>
      <span class="ldr-value" id="ldr-${n.id}">--</span>
      <span class="ldr-unit">raw ADC</span>
    </div>
    <div class="led-status">
      <div class="led-dot" id="led-dot-${n.id}"></div>
      <span class="led-label">LED</span>
      <span class="led-state" id="led-state-${n.id}">OFF</span>
    </div>
    <div class="btn-row">
      <button class="btn" id="btn-on-${n.id}"  onclick="sendCmd(${n.id},'ON')">LED ON</button>
      <button class="btn led-off" id="btn-off-${n.id}" onclick="sendCmd(${n.id},'OFF')">LED OFF</button>
    </div>
  </div>`;
}

const container = document.getElementById("nodes");
NODES.forEach(n => container.innerHTML += buildCard(n));

async function refresh() {
  try {
    const data = await fetch("/data").then(r => r.json());
    data.forEach(d => {
      const id     = d.node_id;
      const card     = document.getElementById("card-"     + id);
      const badge    = document.getElementById("badge-"    + id);
      const ldr      = document.getElementById("ldr-"      + id);
      const ledDot   = document.getElementById("led-dot-"  + id);
      const ledState = document.getElementById("led-state-"+ id);
      const btnOn    = document.getElementById("btn-on-"   + id);
      const btnOff   = document.getElementById("btn-off-"  + id);
      if (!card) return;

      const online = d.online;
      card.className  = "card" + (online ? "" : " is-offline");
      badge.textContent = online ? "Online" : "Offline";
      badge.className   = "badge " + (online ? "online" : "offline");
      ldr.textContent   = online ? d.light : "--";

      ledDot.className   = "led-dot"   + (d.led ? " is-on" : "");
      ledState.textContent = d.led ? "ON" : "OFF";
      ledState.className = "led-state" + (d.led ? " is-on" : "");

      btnOn.className  = "btn" + (d.led ? " led-on" : "");
      btnOff.className = "btn" + (d.led ? "" : " led-off");
    });
  } catch(e) { console.error(e); }
}

async function sendCmd(id, action) {
  await fetch("/control", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "node=" + id + "&cmd=" + action
  });
  refresh();
}

refresh();
setInterval(refresh, 10000);
</script>
</body>
</html>
)rawliteral";
