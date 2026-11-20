/*
  Game Mengingat (Simon Says) - ESP32 + LED + Tombol Web Lokal
  ------------------------------------------------------------
  --------------------------- R ------------------------------
  ------------------------------------------------------------
  - ESP32 membuat WiFi sendiri (mode AP), tidak perlu router.
  - Sambungkan HP/laptop ke WiFi tersebut, lalu buka http://192.168.4.1
  - LED menyala sesuai urutan, tombol ditekan lewat halaman web.

  Wiring (SESUAIKAN):
  - LED : GPIO -> resistor 220 ohm -> anoda LED, katoda -> GND
  - GND ESP32 disambung ke jalur GND yang sama dengan katoda LED
*/

#include <WiFi.h>
#include <WebServer.h>

// ====== KONFIGURASI ======
const int NUM_LED = 4;
const int ledPins[NUM_LED] = {16, 17, 18, 19};

// WiFi: true = ESP32 jadi hotspot sendiri, false = konek ke WiFi rumah
#define USE_AP true
const char* AP_SSID = "MemoryGame";
const char* AP_PASS = "12345678";          // Password wifnya euy
const char* STA_SSID = "NAMA_WIFI_RUMAH";  // dipakai kalau USE_AP false
const char* STA_PASS = "PASSWORD_WIFI";

const int MAX_LEVEL = 50;
const unsigned long INPUT_TIMEOUT = 8000;  // batas waktu tiap tekan (ms)

// ====== VARIABEL GAME ======
WebServer server(80);

int sequence[MAX_LEVEL];
int level = 1;
int bestLevel = 0;
int inputStep = 0;
int litLed = -1;                 // LED yang sedang menyala (untuk tampilan web)
volatile int pendingPress = -1;  // tombol web yang baru ditekan
volatile bool startRequested = false;
String stateStr = "idle";        // idle | show | input | over | win

// ====== HALAMAN WEB ======
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>Game Mengingat ESP32</title>
<style>
  body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;text-align:center}
  h1{font-size:1.3rem;margin:8px 0}
  #info{display:flex;justify-content:center;gap:24px;margin:8px 0;font-size:1rem}
  #msg{min-height:1.6em;margin:10px 0;font-size:1.1rem}
  #pad{display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:360px;margin:12px auto}
  .pad{height:120px;border:0;border-radius:16px;font-size:1.6rem;font-weight:700;color:#000;
       opacity:.45;transition:opacity .08s,transform .08s;-webkit-tap-highlight-color:transparent}
  .pad.on{opacity:1;transform:scale(1.04);box-shadow:0 0 24px #fff}
  .pad:disabled{filter:grayscale(.3)}
  .pad:active:not(:disabled){opacity:1;transform:scale(1.04)}
  #start{padding:14px 32px;font-size:1.1rem;border:0;border-radius:12px;background:#fff;color:#000;font-weight:700}
</style>
</head>
<body>
<h1>Led Memory Game ESP32</h1>
<div id="info"><span>Level: <b id="lv">-</b></span><span>Skor Tertinggi: <b id="best">0</b></span></div>
<div id="msg">Menghubungkan...</div>
<div id="pad"></div>
<button id="start" onclick="startGame()">MULAI</button>
<script>
const COL=['#e53935','#43a047','#1e88e5','#fdd835'];
const pad=document.getElementById('pad');
const N=4; // samakan dengan NUM_LED di kode ESP32
for(let i=0;i<N;i++){
  const b=document.createElement('button');
  b.className='pad';b.id='b'+i;b.textContent=i+1;
  b.style.background=COL[i%COL.length];
  b.onclick=()=>fetch('/press?b='+i).catch(()=>{});
  pad.appendChild(b);
}
function startGame(){fetch('/start').catch(()=>{});}
function render(d){
  document.getElementById('lv').textContent=d.s==='idle'?'-':d.level;
  document.getElementById('best').textContent=d.best;
  const m=document.getElementById('msg'), st=document.getElementById('start');
  const canPlay=(d.s==='input');
  for(let i=0;i<N;i++){
    const b=document.getElementById('b'+i);
    b.classList.toggle('on',d.lit===i);
    b.disabled=!canPlay;
  }
  st.style.display=(d.s==='idle'||d.s==='over'||d.s==='win')?'inline-block':'none';
  st.textContent=d.s==='idle'?'MULAI':'MAIN LAGI';
  if(d.s==='idle')m.textContent='Tekan MULAI untuk bermain';
  else if(d.s==='show')m.textContent='Perhatikan urutan LED...';
  else if(d.s==='input')m.textContent='Giliranmu! ('+d.step+'/'+d.level+')';
  else if(d.s==='over')m.textContent='Game over di level '+d.level;
  else if(d.s==='win')m.textContent='Kamu menang! Level maksimal tercapai';
}
async function poll(){
  try{const r=await fetch('/state');render(await r.json());}
  catch(e){document.getElementById('msg').textContent='Terputus dari ESP32...';}
}
setInterval(poll,250);poll();
</script>
</body>
</html>
)rawliteral";

// ====== HANDLER WEB ======
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleState() {
  String j = "{\"s\":\"" + stateStr + "\",\"level\":" + String(level) +
             ",\"lit\":" + String(stateStr == "input" ? litLed : -1) + ",\"step\":" + String(inputStep + 1) +
             ",\"best\":" + String(bestLevel) + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handlePress() {
  if (server.hasArg("b") && stateStr == "input" && pendingPress == -1) {
    int b = server.arg("b").toInt();
    if (b >= 0 && b < NUM_LED) pendingPress = b;
  }
  server.send(200, "text/plain", "ok");
}

void handleStart() {
  if (stateStr == "idle" || stateStr == "over" || stateStr == "win") startRequested = true;
  server.send(200, "text/plain", "ok");
}


void waitMs(unsigned long ms) {
  unsigned long t = millis();
  while (millis() - t < ms) {
    server.handleClient();
    delay(1);
  }
}

void allLeds(bool on) {
  for (int i = 0; i < NUM_LED; i++) digitalWrite(ledPins[i], on ? HIGH : LOW);
}

void flashLed(int idx, int ms) {
  litLed = idx;
  digitalWrite(ledPins[idx], HIGH);
  waitMs(ms);
  digitalWrite(ledPins[idx], LOW);
  litLed = -1;
}

int stepDelay() {
  int d = 600 - (level - 1) * 25;
  return d < 150 ? 150 : d;
}

void showSequence() {
  waitMs(600);
  int d = stepDelay();
  for (int i = 0; i < level; i++) {
    flashLed(sequence[i], d);
    waitMs(d / 2);
  }
}

bool getPlayerInput() {
  for (inputStep = 0; inputStep < level; inputStep++) {
    unsigned long t = millis();
    while (pendingPress == -1 && millis() - t < INPUT_TIMEOUT) {
      server.handleClient();
      delay(1);
    }
    if (pendingPress == -1) return false;  // timeout

    int p = pendingPress;
    pendingPress = -1;                     

    if (p != sequence[inputStep]) return false;
  }
  return true;
}

void gameOverAnimation() {
  for (int k = 0; k < 4; k++) {
    allLeds(true);  waitMs(200);
    allLeds(false); waitMs(200);
  }
}

void winAnimation() {
  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < NUM_LED; i++) flashLed(i, 100);
    for (int i = NUM_LED - 2; i > 0; i--) flashLed(i, 100);
  }
}

void playGame() {
  level = 1;
  pendingPress = -1;
  for (int i = 0; i < MAX_LEVEL; i++) sequence[i] = random(NUM_LED);
  Serial.println("=== Game baru ===");

  while (true) {
    Serial.printf("Level %d\n", level);

    stateStr = "show";
    showSequence();

    stateStr = "input";
    pendingPress = -1;
    bool ok = getPlayerInput();

    if (!ok) {
      Serial.printf("Game over di level %d\n", level);
      if (level - 1 > bestLevel) bestLevel = level - 1;
      stateStr = "show";   // blok input & tombol Mulai selama animasi
      gameOverAnimation();
      stateStr = "over";
      return;
    }

    if (level >= MAX_LEVEL) {
      bestLevel = level;
      stateStr = "show";
      winAnimation();
      stateStr = "win";
      return;
    }

    if (level > bestLevel) bestLevel = level;
    level++;
  }
}

// ====== SETUP & LOOP ======
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_LED; i++) pinMode(ledPins[i], OUTPUT);

  randomSeed(esp_random());

#if USE_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Hotspot: ");
  Serial.println(AP_SSID);
  Serial.print("Buka: http://");
  Serial.println(WiFi.softAPIP());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nBuka: http://");
  Serial.println(WiFi.localIP());
#endif

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/press", handlePress);
  server.on("/start", handleStart);
  server.begin();

  // Animasi pembuka
  for (int i = 0; i < NUM_LED; i++) flashLed(i, 120);
}

void loop() {
  server.handleClient();
  if (startRequested) {
    startRequested = false;
    playGame();
  }
}
