#include <WiFi.h>
#include <WebServer.h>

// ===== Wi-Fi =====
const char* ssid     = "**********";
const char* password = "*************";

// Use ESP32 Serial2 on GPIO16 (RX2) / GPIO17 (TX2)
// Match Mega baud (your Mega prints at 115200)
#define ESP32_RX2 16
#define ESP32_TX2 17
const uint32_t MEGA_BAUD = 115200;

// ===== Web server =====
WebServer server(80);

// ===== Log ring buffer =====
static const size_t MAX_LINES = 200;
String logBuf[MAX_LINES];
size_t head = 0;      // next write index
size_t countLines = 0; // number of valid lines

String pending; // accumulates chars until '\n'

// Append a line into circular buffer
void pushLine(const String& line) {
  logBuf[head] = line;
  head = (head + 1) % MAX_LINES;
  if (countLines < MAX_LINES) countLines++;
}

// Build all lines in chronological order (oldest → newest)
String dumpAll() {
  String out;
  out.reserve(8192);
  size_t start = (countLines == MAX_LINES) ? head : 0;
  size_t n = countLines;
  for (size_t i = 0; i < n; ++i) {
    size_t idx = (start + i) % MAX_LINES;
    out += logBuf[idx];
    if (!out.endsWith("\n")) out += "\n";
  }
  return out;
}

const char* INDEX_HTML = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Mega Telemetry</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; margin: 0; }
    header { padding: 12px 16px; background:#111; color:#fff; }
    main { padding: 12px 16px; }
    .card { border:1px solid #ddd; border-radius:12px; padding:12px; max-width:900px; margin:auto; }
    #log { white-space: pre-wrap; font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 13px; max-height: 70vh; overflow:auto; }
    .row { display:flex; gap:8px; align-items:center; margin-bottom:8px; }
    button { padding:8px 12px; border:1px solid #ccc; border-radius:8px; background:#f5f5f5; cursor:pointer; }
    button:hover { background:#eee; }
    .pill { display:inline-block; padding:2px 8px; border-radius:999px; background:#e5e7eb; font-size:12px; }
  </style>
</head>
<body>
  <header>
    <strong>ESP32 Web Dashboard</strong>
  </header>
  <main>
    <div class="card">
      <div class="row">
        <span class="pill" id="status">connecting…</span>
        <button onclick="clearLog()">Clear</button>
      </div>
      <div id="log"></div>
    </div>
  </main>
<script>
let logEl = document.getElementById('log');
let statusEl = document.getElementById('status');
let autoscroll = true;

// Auto-scroll if user is at bottom
function maybeAutoScroll() {
  if (!autoscroll) return;
  logEl.scrollTop = logEl.scrollHeight;
}
logEl.addEventListener('scroll', () => {
  const nearBottom = (logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight) < 40;
  autoscroll = nearBottom;
});

function clearLog() {
  fetch('/clear', {method: 'POST'}).then(r => r.text()).then(t => {
    logEl.textContent = '';
  });
}

async function poll() {
  try {
    const r = await fetch('/logs');
    if (!r.ok) throw new Error('HTTP '+r.status);
    const text = await r.text();
    logEl.textContent = text;
    statusEl.textContent = 'live';
    maybeAutoScroll();
  } catch (e) {
    statusEl.textContent = 'disconnected';
  } finally {
    setTimeout(poll, 1000);
  }
}
poll();
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", INDEX_HTML);
}
void handleLogs() {
  server.send(200, "text/plain; charset=utf-8", dumpAll());
}
void handleClear() {
  countLines = 0; head = 0; pending = "";
  server.send(200, "text/plain", "cleared");
}

void setup() {
  // Optional USB serial for debug
  Serial.begin(115200);
  delay(200);

  // UART from Mega on Serial2 (GPIO16=RX, GPIO17=TX)
  Serial2.begin(MEGA_BAUD, SERIAL_8N1, ESP32_RX2, ESP32_TX2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("WiFi connecting to %s", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400); Serial.print(".");
  }
  Serial.printf("\nWiFi OK: %s  IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/clear", HTTP_POST, handleClear);
  server.begin();
  Serial.println("HTTP server started on http://" + WiFi.localIP().toString());
}

void loop() {
  // Pump web server
  server.handleClient();

  // Pull bytes from Mega (via Serial2), accumulate lines
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\r') continue; // normalize
    if (c == '\n') {
      if (pending.length() > 0) {
        pushLine(pending);
        pending = "";
      } else {
        // blank line, still record for spacing
        pushLine("");
      }
    } else {
      // guard against runaway lines
      if (pending.length() < 512) pending += c;
      else { /* drop overly long line */ }
    }
  }
}
