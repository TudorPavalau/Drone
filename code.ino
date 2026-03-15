#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <ESP32Servo.h> 

// --- HARDWARE ---
#define PIN_SDA 33
#define PIN_SCL 32
#define MPU_ADDR 0x68

// --- HARTA MOTOARELOR ---
#define PIN_M_FATA    26  
#define PIN_M_DREAPTA 27  
#define PIN_M_SPATE   14  
#define PIN_M_STANGA  25  

Servo mFata, mDreapta, mSpate, mStanga;

// --- SETARI STABILIZARE ---
float P_GAIN = 2.0; 

// --- WIFI ---
const char* ssid = "Drona_ESP32";
const char* password = "parola1234";
WebServer server(80);

// --- VARIABILE ---
bool sistemPornit = false;
int procentTinta = 0;
int procentCurent = 0;

float inclinareX = 0.0; 
float inclinareY = 0.0; 
String textDirectie = "STABIL";

// Variabile display
int rpmFata = 0, rpmDreapta = 0, rpmSpate = 0, rpmStanga = 0;
unsigned long ultimulUpdate = 0;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Drona Calibrata</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; text-align: center; background: #222; color: #fff; }
    .box { border: 1px solid #555; padding: 10px; margin: 10px auto; width: 95%; max-width: 400px; border-radius: 8px; background: #333; }
    button { padding: 15px; width: 40%; margin: 5px; font-size: 1rem; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }
    .b-on { background: #28a745; color: white; }
    .b-off { background: #dc3545; color: white; }
    .b-set { background: #007bff; color: white; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; }
    .m-val { color: #00e676; font-weight: bold; }
    #status-box { font-size: 1.2rem; color: #ffeb3b; font-weight: bold; margin: 10px 0; }
  </style>
</head>
<body>
  <h2>DRONA SAFE MODE</h2>
  <div class="box">
    <div id="status-box">STABIL</div>
    <div style="font-size: 0.9rem; color: #ccc;">X: <span id="x">0</span> | Y: <span id="y">0</span></div>
  </div>
  <div class="box">
    <h3>TURATIE MOTOARE</h3>
    <div class="grid">
      <div>FATA: <span id="mf" class="m-val">0</span></div>
      <div>SPATE: <span id="ms" class="m-val">0</span></div>
      <div>STANGA: <span id="mst" class="m-val">0</span></div>
      <div>DREAPTA: <span id="mdr" class="m-val">0</span></div>
    </div>
    <p>Throttle: <span id="thr">0</span>%</p>
  </div>
  <div class="box">
    <button class="b-set" onclick="send('sus')">SUS +5%</button>
    <button class="b-set" onclick="send('jos')">JOS -5%</button>
    <br>
    <button class="b-on" onclick="send('on')">ARMEAZA</button>
    <button class="b-off" onclick="send('off')">STOP</button>
  </div>
<script>
  function send(act) { fetch("/cmd?act=" + act); }
  setInterval(() => {
    fetch("/data").then(r => r.json()).then(o => {
      document.getElementById("thr").innerText = o.p;
      document.getElementById("x").innerText = o.x;
      document.getElementById("y").innerText = o.y;
      document.getElementById("status-box").innerText = o.d;
      document.getElementById("mf").innerText = o.mf;
      document.getElementById("ms").innerText = o.ms;
      document.getElementById("mst").innerText = o.mst;
      document.getElementById("mdr").innerText = o.mdr;
    });
  }, 200);
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  mFata.attach(PIN_M_FATA, 1000, 2000);
  mDreapta.attach(PIN_M_DREAPTA, 1000, 2000);
  mSpate.attach(PIN_M_SPATE, 1000, 2000);
  mStanga.attach(PIN_M_STANGA, 1000, 2000);

  stopMotoare();
  delay(3000); 

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); 
  Wire.write(0); 
  Wire.endTransmission(true);

  WiFi.softAP(ssid, password);
  
  server.on("/", []() { server.send(200, "text/html", index_html); });
  
  server.on("/cmd", []() {
    String act = server.arg("act");
    if (act == "on") { sistemPornit = true; procentTinta = 10; } 
    if (act == "off") { sistemPornit = false; procentTinta = 0; procentCurent = 0; }
    if (act == "sus" && sistemPornit) { procentTinta += 5; if(procentTinta > 70) procentTinta = 70; }
    if (act == "jos" && sistemPornit) { procentTinta -= 5; if(procentTinta < 0) procentTinta = 0; }
    server.send(200, "text/plain", "OK");
  });

  server.on("/data", []() {
    String json = "{";
    json += "\"p\":" + String(procentCurent) + ",";
    json += "\"x\":\"" + String(inclinareX, 1) + "\",";
    json += "\"y\":\"" + String(inclinareY, 1) + "\",";
    json += "\"d\":\"" + textDirectie + "\",";
    json += "\"mf\":" + String(rpmFata) + ",";
    json += "\"ms\":" + String(rpmSpate) + ",";
    json += "\"mst\":" + String(rpmStanga) + ",";
    json += "\"mdr\":" + String(rpmDreapta);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  server.handleClient();
  
  if (millis() - ultimulUpdate > 20) { 
    ultimulUpdate = millis();
    citesteSenzor();

    if (sistemPornit) {
      if (procentCurent < procentTinta) procentCurent++;
      if (procentCurent > procentTinta) procentCurent--;
    } else {
      procentCurent = 0;
    }

    calculeazaSiAplicaMotoare();
  }
}

void citesteSenzor() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 4, true);

  if (Wire.available() == 4) {
    int16_t rawX = (Wire.read() << 8 | Wire.read());
    int16_t rawY = (Wire.read() << 8 | Wire.read());
    
    float newX = rawX / 16384.0 * 90.0;
    float newY = rawY / 16384.0 * 90.0;

    inclinareX = (inclinareX * 0.8) + (newX * 0.2);
    inclinareY = (inclinareY * 0.8) + (newY * 0.2);
    
    if(inclinareX > 5) textDirectie = "INCLINAT SPATE";
    else if(inclinareX < -5) textDirectie = "INCLINAT FATA";
    else if(inclinareY > 5) textDirectie = "INCLINAT STANGA";
    else if(inclinareY < -5) textDirectie = "INCLINAT DREAPTA";
    else textDirectie = "STABIL";
  }
}

void calculeazaSiAplicaMotoare() {
  if (!sistemPornit || procentCurent == 0) {
    stopMotoare();
    return;
  }

  int throttle = map(procentCurent, 0, 100, 1000, 1800);

  float pitchFix = inclinareX * P_GAIN; 
  float rollFix = inclinareY * P_GAIN;  

  int valFata    = throttle - pitchFix;
  int valSpate   = throttle + pitchFix;
  int valStanga  = throttle + rollFix; 
  int valDreapta = throttle - rollFix; 

  valFata    = constrain(valFata, 1100, 2000);
  valSpate   = constrain(valSpate, 1100, 2000);
  valStanga  = constrain(valStanga, 1100, 2000);
  valDreapta = constrain(valDreapta, 1100, 2000);

  mFata.writeMicroseconds(valFata);
  mSpate.writeMicroseconds(valSpate);
  mStanga.writeMicroseconds(valStanga);
  mDreapta.writeMicroseconds(valDreapta);

  rpmFata = valFata; 
  rpmSpate = valSpate; 
  rpmStanga = valStanga; 
  rpmDreapta = valDreapta;
}

void stopMotoare() {
  mFata.writeMicroseconds(1000);
  mSpate.writeMicroseconds(1000);
  mStanga.writeMicroseconds(1000);
  mDreapta.writeMicroseconds(1000);
  rpmFata=1000; 
  rpmSpate=1000; 
  rpmStanga=1000; 
  rpmDreapta=1000;
}