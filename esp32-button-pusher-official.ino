#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "secrets.h"

// IO stuff
const int WIFI_LED_PIN = 2;
const int N = 6;
const int pins[N] = {14, 27, 26, 25, 33, 32};

// Servo angle parameters (microseconds)
const int MIN_ANGLE = 500;
const int MAX_ANGLE = 2500;
const int MID_ANGLE = 1500;

// Button press characterization
const int NEUTRAL = MAX_ANGLE;
const int PRESS = 2055; // ~40 deg
const int PRESS_ERROR_OFFSET[N] = {-50, -25, 0, 50, -50, 0};
int PRESS_CORRECTED[N]; // initialized in setup
const int PRESS_DELAY = 300; // ms 

Servo servos[N];
bool state[N] = {0}; // irl button latch states
bool busy = false; // true while executing command

WebServer server(80);

// Helper fns
void pressButton(int i) {
    // Serial.println("Some mf command received (in pressButton)");
    servos[i].attach(pins[i]);
    servos[i].writeMicroseconds(PRESS_CORRECTED[i]);
    delay(PRESS_DELAY);
    servos[i].writeMicroseconds(NEUTRAL);
    delay(PRESS_DELAY);
    servos[i].detach();
}

void execute(const bool desired[N]) {
    // Serial.println("Some mf command received (in execute)");
    for(int i = 0; i<N; i++) {
        // Serial.printf("idx %d  cur=%d  des=%d  xor=%d\n",
        //               i,state[i],desired[i],state[i]^desired[i]);
        if(state[i]^desired[i]) {
            pressButton(i);
            state[i] = desired[i];
        }
    }
}

// ---------- http ----------
const char HTML[] PROGMEM = R"raw(
<!doctype html><html><head><meta charset=utf-8>
<title>Speaker Selector</title><style>
body{font-family:sans-serif}
.b {
  padding: 8px 12px;
  height: auto;
  min-height: 40px;
  margin: 4px;
  border: none;
  color: #fff;
  font-size: 16px;
  white-space: nowrap;
} .on{background:#2a9d8f}.off{background:#555}
.busy{background:#e76f51}
</style></head><body>
<h2>zones</h2><div id=buttons></div>
<h3>presets</h3>
<select id=preset>
<option value="">choose…</option>
<option value="host">host mode</option>
<option value="alloff">all off</option>
<option value="allon">all on</option>
</select>
<script>
const N=6; let s=[0,0,0,0,0,0], busy=false;

const roomNames = ["Kitchen", "Basement", "Master Bed", "Loft", "Patio", "Living Room"];
function draw(){
  const d=document.getElementById('buttons'); d.innerHTML='';
  for(let i=0;i<N;i++){
    const btn=document.createElement('button');
    btn.className='b '+(busy?'busy':(s[i]?'on':'off'));
    btn.textContent=`${roomNames[i]} (${s[i] ? "ON" : "OFF"})`;
    btn.disabled=busy;
    btn.onclick=()=>send(desiredArr(i));
    d.appendChild(btn);
  }
}

function desiredArr(idx){
  const d=[...s]; d[idx]=!d[idx]; return d;
}

async function send(arr){
  if(busy) return;
  busy=true; draw();
  await fetch('/set',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({d:arr.map(x=>x?1:0)})});
}

async function poll(){
  try{
    const r=await fetch('/stat'); const j=await r.json();
    busy=j.b; s=j.s;
  }catch{}
  draw();
}
setInterval(poll,2000); poll();

document.getElementById('preset').onchange=e=>{
  const v=e.target.value; if(!v) return;
  let d=[...s];
  if(v==='host')  d=[1,1,0,0,0,0];
  if(v==='alloff')d=[0,0,0,0,0,0];
  if(v==='allon') d=[1,1,1,1,1,1];
  send(d); e.target.value='';
};
</script></body></html>)raw";

void root()     { server.send_P(200,"text/html",HTML); }
void status()   {
  String j="{\"b\":"; j += (busy?"true":"false"); j += ",\"s\":[";
  for(int i=0;i<N;i++){ j += state[i]?"1":"0"; if(i<N-1) j+=','; }
  j += "]}"; server.send(200,"application/json",j);
}
void setState(){
  if(busy){ server.send(409,"text/plain","busy"); return; }
  if(!server.hasArg("plain")){ server.send(400,"text/plain","no body"); return; }
  String body=server.arg("plain");
  bool d[N]; int idx=body.indexOf('[')+1;
  for(int i=0;i<N;i++){
    int nxt=body.indexOf(i==N-1?']':',',idx);
    d[i]=(body.substring(idx,nxt).indexOf('1')!=-1); idx=nxt+1;
  }
  busy=true; server.send(202,"text/plain","ok");
  execute(d); busy=false;
}

// Setup and Loop
void setup() {
    pinMode(WIFI_LED_PIN, OUTPUT); digitalWrite(WIFI_LED_PIN, LOW);
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi...");

    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }

    Serial.println("\nConnected to WiFi!");
    digitalWrite(WIFI_LED_PIN, HIGH);
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    for (int i = 0; i < N; i++) {
        PRESS_CORRECTED[i] = PRESS + PRESS_ERROR_OFFSET[i];
    }

    server.on("/", root);
    server.on("/stat", status);
    server.on("/set", HTTP_POST, setState);

    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient(); // keep checking for web requests

    if (WiFi.status() != WL_CONNECTED) {
        digitalWrite(WIFI_LED_PIN, LOW);
    } else {
        digitalWrite(WIFI_LED_PIN, HIGH);
    }
}