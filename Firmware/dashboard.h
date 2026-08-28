/**
 * @file      dashboard.h
 * @brief     PROGMEM HTML/CSS/JS payload for ESPAsyncWebServer
 */

#ifndef DASHBOARD_H
#define DASHBOARD_H
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>100W Power Hub Dashboard</title>
  <style>
    :root {
      --bg-color: #0b0c10;
      --panel-bg: #1f2833;
      --text-main: #c5c6c7;
      --accent: #66fcf1;
      --accent-dim: #45a29e;
      --danger: #ff003c;
    }
    body {
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg-color);
      color: var(--text-main);
      margin: 0;
      padding: 20px;
      text-align: center;
    }
    h1 {
      color: var(--accent);
      letter-spacing: 2px;
      text-transform: uppercase;
      border-bottom: 2px solid var(--accent-dim);
      display: inline-block;
      padding-bottom: 10px;
      margin-bottom: 40px;
    }
    .dashboard-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 25px;
      max-width: 1200px;
      margin: 0 auto;
    }
    .card {
      background-color: var(--panel-bg);
      border: 1px solid #333;
      border-radius: 8px;
      padding: 25px;
      box-shadow: 0 10px 20px rgba(0,0,0,0.5);
      text-align: left;
    }
    .card-header {
      font-size: 1.2rem;
      color: white;
      border-bottom: 1px solid #444;
      padding-bottom: 10px;
      margin-bottom: 20px;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .data-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 15px;
      font-size: 1.1rem;
    }
    .primary-metric-spacer {
      margin-bottom: 25px;
      padding-bottom: 20px;
      border-bottom: 1px dashed #444;
    }
    .metric {
      font-size: 2.2rem;
      color: var(--accent);
      font-weight: bold;
      text-shadow: 0 0 8px rgba(102, 252, 241, 0.3);
    }
    .unit {
      font-size: 1rem;
      color: #888;
      margin-left: 5px;
    }
    .control-panel {
      grid-column: 1 / -1; 
      text-align: center;
      margin-top: 20px;
    }
    .btn-kill {
      background-color: transparent;
      color: var(--danger);
      border: 2px solid var(--danger);
      padding: 20px 50px;
      font-size: 1.5rem;
      font-weight: bold;
      border-radius: 6px;
      cursor: pointer;
      text-transform: uppercase;
      letter-spacing: 2px;
      transition: all 0.2s ease-in-out;
    }
    .btn-kill:hover {
      background-color: var(--danger);
      color: white;
      box-shadow: 0 0 20px rgba(255, 0, 60, 0.6);
    }
    .btn-kill:active {
      transform: scale(0.95);
    }
  </style>
</head>
<body>

  <h1>System Telemetry</h1>

  <div class="dashboard-grid">
    
    <div class="card">
      <div class="card-header">Global Status</div>
      <div class="data-row primary-metric-spacer">
        <span>Total Output Power</span>
        <div><span class="metric" id="wTotal">0.0</span><span class="unit">W</span></div>
      </div>
      <div class="data-row">
        <span>Hardware Status</span>
        <div style="color: #00ff00; font-weight: bold;" id="sysStatus">ONLINE</div>
      </div>
      <div class="data-row">
        <span>Network Uplink</span>
        <div style="color: #ff003c; font-weight: bold;" id="netStatus">CONNECTING...</div>
      </div>
    </div>

    <div class="card">
      <div class="card-header">12V Output Line</div>
      <div class="data-row">
        <span>Bus Voltage</span>
        <div><span class="metric" id="v12">0.0</span><span class="unit">V</span></div>
      </div>
      <div class="data-row">
        <span>Load Current</span>
        <div><span class="metric" id="a12">0.00</span><span class="unit">A</span></div>
      </div>
      <div class="data-row">
        <span>Power Draw</span>
        <div><span class="metric" id="w12">0.0</span><span class="unit">W</span></div>
      </div>
    </div>

    <div class="card">
      <div class="card-header">9V Output Line</div>
      <div class="data-row">
        <span>Bus Voltage</span>
        <div><span class="metric" id="v9">0.0</span><span class="unit">V</span></div>
      </div>
      <div class="data-row">
        <span>Load Current</span>
        <div><span class="metric" id="a9">0.00</span><span class="unit">A</span></div>
      </div>
      <div class="data-row">
        <span>Power Draw</span>
        <div><span class="metric" id="w9">0.0</span><span class="unit">W</span></div>
      </div>
    </div>

    <div class="card control-panel">
      <div class="card-header" style="border:none; margin-bottom: 10px;">Safety Override</div>
      <button class="btn-kill" onclick="toggleKillSwitch()">Initiate Hardware Isolate</button>
      <p style="color: #666; font-size: 0.9rem; margin-top: 15px;">Disconnects P-Channel MOSFETs from main load.</p>
    </div>

  </div>

  <script>
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;
    
    window.addEventListener('load', onLoad);
    
    function initWebSocket() {
      websocket = new WebSocket(gateway);
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onerror   = onError;
      websocket.onmessage = onMessage;
    }
    
    function onLoad(event) { 
      initWebSocket(); 
    }

    function onOpen(event) {
      document.getElementById('netStatus').innerHTML = "STABLE (LOCAL)";
      document.getElementById('netStatus').style.color = "#00ff00";
    }
    
    function onClose(event) {
      document.getElementById('netStatus').innerHTML = "DISCONNECTED";
      document.getElementById('netStatus').style.color = "#ff003c";
      setTimeout(initWebSocket, 2000); // Auto-reconnect polling
    }

    function onError(event) {
      document.getElementById('netStatus').innerHTML = "ERROR";
      document.getElementById('netStatus').style.color = "#ff003c";
    }
    
    function onMessage(event) {
      var data = JSON.parse(event.data);
      
      document.getElementById('v12').innerHTML = data.v12.toFixed(1);
      document.getElementById('a12').innerHTML = data.a12.toFixed(2);
      document.getElementById('w12').innerHTML = data.w12.toFixed(1);
      
      document.getElementById('v9').innerHTML = data.v9.toFixed(1);
      document.getElementById('a9').innerHTML = (data.a9 !== undefined) ? data.a9.toFixed(2) : "0.00"; // Fallback for legacy firmware payloads
      document.getElementById('w9').innerHTML = data.w9.toFixed(1);

      var totalPower = data.w12 + data.w9;
      document.getElementById('wTotal').innerHTML = totalPower.toFixed(1);
    }
    
    function toggleKillSwitch() {
      if (websocket.readyState === WebSocket.OPEN) {
        websocket.send("KILL");
      }
      var statusText = document.getElementById('sysStatus');
      if (statusText.innerHTML === "ONLINE") {
        statusText.innerHTML = "ISOLATED";
        statusText.style.color = "#ff003c";
      } else {
        statusText.innerHTML = "ONLINE";
        statusText.style.color = "#00ff00";
      }
    }
  </script>
</body>
</html>
)rawliteral";

#endif