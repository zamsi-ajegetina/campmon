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
  .btn-on  { background: #0f3460; color: #e0e0e0; }
  .btn-off { background: #0f3460; color: #e0e0e0; }
  .btn-on.active  { background: #e94560; color: #fff; }
  .btn-off.active { background: #263238; color: #fff; }
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
  1: "A &mdash; Mech Workshop",
  2: "B &mdash; PNRB",
  3: "C &mdash; Eng Block",
  4: "D &mdash; Fab Lab"
};

function buildCard(n) {
  return `
  <div class="card" id="card-${n}">
    <h2>Node ${NODE_LABELS[n] || n}</h2>
    <span class="badge offline" id="status-${n}">Offline</span>
    <div class="sensor-row"><span>Light (LDR)</span><span class="val" id="l-${n}">--</span></div>
    <div style="display:flex;gap:6px;margin-top:10px">
      <button class="btn btn-on"  id="btn-on-${n}"  onclick="sendCmd(${n},'ON')">LED ON</button>
      <button class="btn btn-off" id="btn-off-${n}" onclick="sendCmd(${n},'OFF')">LED OFF</button>
    </div>
  </div>`;
}

const container = document.getElementById("nodes");
for (let i = 1; i <= 4; i++) container.innerHTML += buildCard(i);

async function refresh() {
  try {
    const r = await fetch("/data");
    const data = await r.json();
    data.forEach(d => {
      const status = document.getElementById("status-" + d.node_id);
      if (!status) return;
      status.textContent = d.online ? "Online" : "Offline";
      status.className = "badge " + (d.online ? "online" : "offline");
      document.getElementById("l-" + d.node_id).textContent =
        d.light !== undefined ? d.light : "--";
      const btnOn  = document.getElementById("btn-on-"  + d.node_id);
      const btnOff = document.getElementById("btn-off-" + d.node_id);
      if (d.led) {
        btnOn.classList.add("active");
        btnOff.classList.remove("active");
      } else {
        btnOff.classList.add("active");
        btnOn.classList.remove("active");
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
  refresh();
}

refresh();
setInterval(refresh, 10000);
</script>
</body>
</html>
)rawliteral";
