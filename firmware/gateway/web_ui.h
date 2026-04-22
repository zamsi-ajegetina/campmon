#pragma once

// ── Embedded HTML for the Gateway Control Web Page ──────────────
// Served at http://<gateway-ip>/
// Allows user to: view last sensor readings + toggle LEDs per node.

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
  h1 { text-align: center; color: #e94560; margin-bottom: 24px; font-size: 1.6rem; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 16px; }
  .card {
    background: #16213e; border-radius: 12px; padding: 18px;
    border: 1px solid #0f3460; box-shadow: 0 4px 12px rgba(0,0,0,0.4);
  }
  .card h2 { font-size: 1rem; color: #e94560; margin-bottom: 12px; }
  .sensor-row { display: flex; justify-content: space-between; margin-bottom: 6px; font-size: 0.85rem; }
  .val { color: #00d4ff; font-weight: bold; }
  .badge {
    display: inline-block; font-size: 0.7rem; padding: 2px 8px;
    border-radius: 10px; margin-bottom: 12px;
  }
  .online  { background: #1b5e20; color: #a5d6a7; }
  .offline { background: #b71c1c; color: #ef9a9a; }
  .btn {
    display: block; width: 100%; padding: 10px;
    border: none; border-radius: 8px; cursor: pointer;
    font-size: 0.9rem; font-weight: bold; transition: 0.2s;
  }
  .btn-on  { background: #e94560; color: #fff; }
  .btn-off { background: #0f3460; color: #e0e0e0; }
  .btn:hover { opacity: 0.85; }
  footer { text-align: center; color: #555; margin-top: 30px; font-size: 0.75rem; }
</style>
</head>
<body>
<h1>Campus IoT Gateway</h1>
<div class="grid" id="nodes"></div>
<footer>Ashesi University &mdash; CSIS IoT &mdash; Auto-refresh every 10s</footer>

<script>
const NODE_LABELS = {
  1: "Fab Lab",
  2: "MPR",
  3: "Engineering Block",
  4: "Mech Workshop",
  5: "PNRB"
};

function buildCard(n) {
  return `
  <div class="card" id="card-${n}">
    <h2>Node ${n} &mdash; ${NODE_LABELS[n] || ""}</h2>
    <span class="badge offline" id="status-${n}">Offline</span>
    <div class="sensor-row"><span>Temperature</span><span class="val" id="t-${n}">--</span></div>
    <div class="sensor-row"><span>Humidity</span><span class="val" id="h-${n}">--</span></div>
    <div class="sensor-row"><span>Light</span><span class="val" id="l-${n}">--</span></div>
    <button class="btn btn-on"  onclick="sendCmd(${n},'ON')">LED ON</button>
    <button class="btn btn-off" onclick="sendCmd(${n},'OFF')" style="margin-top:6px">LED OFF</button>
  </div>`;
}

// Render cards
const container = document.getElementById("nodes");
for (let i = 1; i <= 5; i++) container.innerHTML += buildCard(i);

// Fetch latest data from gateway
async function refresh() {
  try {
    const r = await fetch("/data");
    const data = await r.json();
    data.forEach(d => {
      const status = document.getElementById("status-" + d.node_id);
      if (status) {
        status.textContent = d.online ? "Online" : "Offline";
        status.className = "badge " + (d.online ? "online" : "offline");
        document.getElementById("t-" + d.node_id).textContent =
          d.temperature !== null ? d.temperature.toFixed(1) + " °C" : "--";
        document.getElementById("h-" + d.node_id).textContent =
          d.humidity !== null ? d.humidity.toFixed(1) + " %" : "--";
        document.getElementById("l-" + d.node_id).textContent =
          d.light !== null ? d.light : "--";
      }
    });
  } catch(e) { console.error(e); }
}

async function sendCmd(nodeId, action) {
  await fetch("/control", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "node=" + nodeId + "&cmd=" + action
  });
}

refresh();
setInterval(refresh, 10000);
</script>
</body>
</html>
)rawliteral";
