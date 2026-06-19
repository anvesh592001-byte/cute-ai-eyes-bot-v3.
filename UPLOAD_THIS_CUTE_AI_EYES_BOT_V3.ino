/*
  CUTE AI EYES BOT V2
  Hardware: ESP32 Dev Module + 128x64 SSD1306 I2C OLED

  The OLED displays ONLY expressive animated eyes.
  The phone web page displays questions and AI responses.

  Before uploading:
  Add your Groq key and optional Supabase settings below.
  Home Wi-Fi is configured through the OLED system menu, never in code.
*/

#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define BUZZER_PIN 27

// ---------- CHANGE THESE CLOUD SETTINGS ----------
const char* GROQ_API_KEY   = "PASTE_YOUR_GROQ_API_KEY_HERE";
const char* SUPABASE_URL   = "https://YOUR_PROJECT.supabase.co";
const char* SUPABASE_KEY   = "PASTE_SUPABASE_ANON_KEY_HERE";

// This model name can be changed if your API provider changes its model list.
const char* AI_MODEL = "llama-3.1-8b-instant";

// Phone connects to this ESP32 access point.
const char* BOT_AP_SSID = "Cute-AI-Bot";
const char* BOT_AP_PASS = "12345678";
// -------------------------------------------

const char* AI_URL = "https://api.groq.com/openai/v1/chat/completions";

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);
Preferences preferences;

enum EyeExpression {
  EYE_IDLE,
  EYE_LISTENING,
  EYE_THINKING,
  EYE_SEARCHING,
  EYE_HAPPY,
  EYE_CONFUSED,
  EYE_ERROR,
  EYE_SLEEPING,
  EYE_SUCCESS,
  EYE_LOVE,
  EYE_WINK,
  EYE_SAD,
  EYE_EXCITED,
  EYE_SURPRISED,
  EYE_OFF
};

enum EyeTheme { THEME_SOFTBOT, THEME_SPARKLE, THEME_KAWAII, THEME_FOCUS, THEME_PIXEL, THEME_DEVIL, THEME_COUNT };
enum ScreenMode { MODE_EYES, MODE_ANSWER, MODE_MENU, MODE_WIFI_MENU, MODE_WIFI, MODE_WEATHER, MODE_SNAKE, MODE_PONG, MODE_LOVE_STORY, MODE_BIRTHDAY };

EyeTheme eyeTheme = THEME_SOFTBOT;
ScreenMode screenMode = MODE_EYES;
int menuSelection = 0;
const char* MENU_ITEMS[] = {"ASK AI", "WIFI SETUP", "WEATHER", "FACE STYLE", "SNAKE", "PONG", "LOVE STORY", "BIRTHDAY SONG", "ODE TO JOY"};
const int MENU_COUNT = 9;

EyeExpression expression = EYE_IDLE;
EyeExpression previousExpression = EYE_IDLE;

// AI responses are displayed on the OLED as readable pages.
bool showingAnswer = false;
int answerPage = 0;
const int ANSWER_CHARS_PER_LINE = 21;
const int ANSWER_LINES_PER_PAGE = 6;

// Smooth eye animation values.
float eyeX = 0;
float eyeY = 0;
float targetEyeX = 0;
float targetEyeY = 0;
float eyeOpen = 1.0;
float targetEyeOpen = 1.0;

bool blinking = false;
unsigned long blinkStarted = 0;
unsigned long nextBlink = 0;
unsigned long expressionUntil = 0;
unsigned long lastEyeFrame = 0;

String lastQuestion = "";
String lastAnswer = "Hello! Ask me something.";
bool aiBusy = false;
bool wifiConnecting = false;
unsigned long wifiConnectStarted = 0;
String savedWifiSSID = "";
String savedWifiPass = "";
String oledInput = "";
String wifiNetworks[12];
int wifiNetworkCount = 0;
int wifiSelection = 0;
int wifiMenuSelection = 0;
const char* WIFI_MENU_ITEMS[] = {"CHANGE NETWORK", "CONNECTION INFO", "DISCONNECT", "FORGET SAVED WIFI"};
const int WIFI_MENU_COUNT = 4;

// Short conversation memory. Supabase stores the permanent copy.
String memoryQuestion[4];
String memoryAnswer[4];
int memoryCount = 0;

// Vijayawada weather.
float weatherTemp = 0;
float weatherWind = 0;
int weatherCode = -1;
String weatherLabel = "Not loaded";

// Snake and Pong.
int snakeX[48], snakeY[48], snakeLength=4, snakeDX=1, snakeDY=0, snakeFoodX=20, snakeFoodY=6, snakeScore=0;
bool snakeGameOver=false;
int pongPaddle=24,pongCpu=24,pongBallX=64,pongBallY=32,pongDX=2,pongDY=1,pongScore=0;
int storyPage=0;
unsigned long storyStarted=0;
unsigned long lastGameFrame = 0;
bool birthdayPlaying=false;
int birthdayNote=0;
unsigned long birthdayNextNote=0;
int birthdayFrame=0;
int tuneMode=0;
const char* tuneTitle="BIRTHDAY";

#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_AS4 466
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
int birthdayMelody[] = {NOTE_G4,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_C5,NOTE_AS4,NOTE_G4,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_D5,NOTE_C5,NOTE_G4,NOTE_G4,NOTE_G5,NOTE_E5,NOTE_C5,NOTE_AS4,NOTE_A4,NOTE_F5,NOTE_F5,NOTE_E5,NOTE_C5,NOTE_D5,NOTE_C5};
int birthdayDurations[] = {8,8,4,4,4,2,8,8,4,4,4,2,8,8,4,4,4,4,2,8,8,4,4,4,2};
const int BIRTHDAY_NOTES = sizeof(birthdayMelody)/sizeof(birthdayMelody[0]);

// Famous public-domain melody that sounds clear on a small passive buzzer.
int odeMelody[] = {NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4};
int odeDurations[] = {4,4,4,4,4,4,4,4,4,4,4,4,3,8,2,4,4,4,4,4,4,4,4,4,4,4,4,3,8,2};
const int ODE_NOTES = sizeof(odeMelody)/sizeof(odeMelody[0]);

// Mobile-friendly retro control panel stored in ESP32 flash.
const char WEB_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Cute AI Eyes Bot</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;touch-action:manipulation}
:root{--bg:#080b12;--panel:#101723;--line:#324760;--cyan:#72e6ff;--pink:#ff9adf;--green:#8dffb0}
body{margin:0;background:radial-gradient(circle at top,#17253a,var(--bg) 52%);color:#dcecff;font-family:monospace;min-height:100vh;padding:12px}
.app{max-width:620px;margin:auto}
.title{border:2px solid var(--cyan);background:#0a101a;border-radius:12px;padding:13px;box-shadow:0 0 18px #36cfff44;margin-bottom:10px}
.title b{color:var(--pink);font-size:19px}.status{color:var(--green);font-size:12px;margin-top:5px}
.chat{height:110px;overflow-y:auto;white-space:pre-wrap;background:#05080d;border:2px solid var(--line);border-radius:12px;padding:12px;line-height:1.45;margin-bottom:9px}
.user{color:var(--cyan)}.bot{color:#fff}.error{color:#ff8e9b}
.ask{display:flex;gap:7px;margin-bottom:9px}
input{flex:1;min-width:0;background:#080d15;color:#fff;border:2px solid var(--line);border-radius:10px;padding:13px;font-family:monospace;font-size:15px;outline:none}
button{touch-action:manipulation;border:1px solid #42617e;background:#131e2d;color:#dff8ff;border-radius:9px;padding:11px 7px;font-family:monospace;font-weight:bold}
button:active{transform:scale(.96);background:#28516b}
.send{background:#21495a;border-color:var(--cyan);color:var(--cyan)}
.section{font-size:11px;color:#829ab3;margin:12px 2px 6px}
.nav{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.nav button{min-height:48px;font-size:15px}
.tabs{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-bottom:9px}
.tab{border-color:var(--pink);color:var(--pink)}
.page{display:none}.page.on{display:block}
.gamepad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.gamepad button{min-height:76px;font-size:18px;touch-action:none;user-select:none}
.gamepad .wide{grid-column:span 3}
.expressions{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.expressions button{font-size:11px}
.keyboard{display:flex;flex-direction:column;gap:5px}
.oledbox{margin-top:12px;padding:10px;border:2px solid var(--line);border-radius:12px;background:#090e17}
.row{display:flex;gap:4px}
.key{flex:1;min-width:0;padding:10px 2px;font-size:12px}
.space{flex:5}.wide{flex:2}.pink{color:var(--pink)}
@media(min-width:700px){.chat{height:280px}}
</style>
</head>
<body>
<div class="app">
 <div class="title"><b>[ CUTE AI EYES BOT ]</b><div class="status" id="status">connecting...</div></div>
 <div class="tabs">
  <button class="tab" onclick="tab('chatPage')">CHAT</button>
  <button class="tab" onclick="tab('controlPage')">CONTROL</button>
  <button class="tab" onclick="tab('gamePage')">GAMEPAD</button>
 </div>
 <div class="page on" id="chatPage">
 <div class="chat" id="chat"><span class="bot">Answers appear on the OLED. Use LEFT/RIGHT or UP/DOWN to change pages. BACK returns to eyes.</span></div>
 <div class="ask">
  <input id="question" maxlength="350" placeholder="Type your question...">
  <button class="send" onclick="askAI()">SEND</button>
 </div>
 </div>

 <div class="page on" id="controlPage">
 <div class="section">NAVIGATION / BOT CONTROL</div>
 <div class="nav">
  <button data-key="left">&lt; LEFT</button>
  <button data-key="up">^ UP</button>
  <button data-key="right">RIGHT &gt;</button>
  <button data-key="back">BACK</button>
  <button data-key="enter" class="send">ENTER</button>
  <button data-key="down">v DOWN</button>
 </div>
 </div>

 <div class="page" id="gamePage">
  <div class="section">BIG GAMEPAD</div>
  <div class="gamepad">
   <button></button><button data-key="up">UP</button><button></button>
   <button data-key="left">LEFT</button><button class="send" data-key="enter">ENTER</button><button data-key="right">RIGHT</button>
   <button></button><button data-key="down">DOWN</button><button></button>
   <button class="wide pink" data-key="back">BACK / EXIT</button>
  </div>
 </div>

 <div class="page on" id="extrasPage">
 <div class="section">EXPRESSION CHANGER</div>
 <div class="expressions">
  <button onclick="face('idle')">IDLE</button><button onclick="face('listening')">LISTEN</button><button onclick="face('thinking')">THINK</button>
  <button onclick="face('searching')">SEARCH</button><button onclick="face('happy')">HAPPY</button><button onclick="face('confused')">CONFUSED</button>
  <button onclick="face('error')">ERROR</button><button onclick="face('sleeping')">SLEEP</button><button onclick="face('success')">SUCCESS</button>
  <button onclick="face('love')">LOVE</button><button onclick="face('wink')">WINK</button><button onclick="face('sad')">SAD</button>
  <button onclick="face('excited')">EXCITED</button><button onclick="face('surprised')">SURPRISE</button><button onclick="face('idle')">RESET</button>
 </div>
 <div class="section">FACE THEMES</div>
 <div class="expressions">
  <button onclick="theme(0)">SOFTBOT</button><button onclick="theme(1)">SPARKLE</button><button onclick="theme(2)">KAWAII</button>
  <button onclick="theme(3)">FOCUS</button><button onclick="theme(4)">PIXEL</button><button onclick="theme(5)">DEVIL</button>
 </div>

 <div class="section">VIRTUAL KEYBOARD</div>
 <div class="keyboard">
  <div class="row" id="r1"></div><div class="row" id="r2"></div><div class="row" id="r3"></div>
  <div class="row">
   <button class="key wide pink" onclick="backspace()">BKSP</button>
   <button class="key space" onclick="keyPress(' ')">SPACE</button>
   <button class="key wide send" onclick="askAI()">ENTER</button>
  </div>
 </div>
 <div class="section">OLED SYSTEM</div>
 <div class="oledbox">
  <button style="width:100%" data-key="menu">OPEN OLED MENU</button>
  <button style="width:100%;margin-top:6px" data-key="wifi">WIFI OPTIONS</button>
  <button style="width:100%;margin-top:6px" data-key="scanwifi">SCAN WIFI NOW</button>
  <input id="oledText" placeholder="Password/text for current OLED screen">
  <button style="width:100%;margin-top:6px" onclick="sendOledText()">SEND TEXT TO OLED</button>
  <div id="wifiStatus" class="status">Wi-Fi setup is inside the OLED menu.</div>
 </div>
 </div>
</div>
<script>
const q=document.getElementById('question'),chat=document.getElementById('chat'),statusBox=document.getElementById('status');
function tab(id){document.querySelectorAll('.page').forEach(p=>p.classList.remove('on'));document.getElementById(id).classList.add('on');if(id!='chatPage')document.getElementById('extrasPage').classList.add('on')}
function add(cls,label,text){let d=document.createElement('div');d.className=cls;d.textContent=label+text;chat.appendChild(d);chat.scrollTop=chat.scrollHeight}
function askAI(){
 let text=q.value.trim();if(!text)return;
 add('user','YOU> ',text);q.value='';statusBox.textContent='thinking...';
 fetch('/ask',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent(text)})
 .then(r=>r.text().then(t=>({ok:r.ok,text:t}))).then(x=>{add(x.ok?'bot':'error',x.ok?'BOT> ':'ERROR> ',x.text);statusBox.textContent=x.ok?'answer shown on OLED':'error'})
 .catch(()=>{add('error','ERROR> ','Connection lost');statusBox.textContent='disconnected'});
}
function fresh(url){return url+(url.includes('?')?'&':'?')+'_='+Date.now()}
function face(f){fetch(fresh('/face?name='+f),{cache:'no-store'}).then(()=>statusBox.textContent='expression: '+f)}
function theme(n){fetch(fresh('/theme?id='+n),{cache:'no-store'}).then(r=>r.text()).then(t=>statusBox.textContent=t)}
function control(c,quiet=false){
 statusBox.textContent='control: '+c;
 fetch(fresh('/control?key='+c),{cache:'no-store',keepalive:true}).then(r=>quiet?'':r.text()).then(t=>{if(t)statusBox.textContent=t}).catch(()=>statusBox.textContent='CONTROL FAILED');
}
let lastTap=0;
function fastControl(c){
 let now=Date.now(); if(now-lastTap<35)return; lastTap=now;
 if(navigator.vibrate)navigator.vibrate(8);
 fetch(fresh('/control?key='+c+'&fast=1'),{cache:'no-store'}).catch(()=>{});
 statusBox.textContent='control: '+c;
}
document.querySelectorAll('[data-key]').forEach(b=>{
 let key=b.dataset.key,hold=null,repeat=(key==='up'||key==='down'||key==='left'||key==='right');
 b.addEventListener('pointerdown',e=>{e.preventDefault();fastControl(key);if(repeat)hold=setInterval(()=>fastControl(key),70)});
 ['pointerup','pointerleave','pointercancel'].forEach(ev=>b.addEventListener(ev,()=>{if(hold)clearInterval(hold);hold=null}));
});
function keyPress(k){q.value+=k;q.focus()}
function backspace(){q.value=q.value.slice(0,-1);q.focus()}
function sendOledText(){fetch('/oled/text',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'text='+encodeURIComponent(oledText.value)}).then(r=>r.text()).then(t=>wifiStatus.textContent=t)}
setInterval(()=>fetch('/wifi/status').then(r=>r.text()).then(t=>wifiStatus.textContent=t),7000);
function keys(id,letters){let r=document.getElementById(id);letters.split('').forEach(k=>{let b=document.createElement('button');b.className='key';b.textContent=k;b.onclick=()=>keyPress(k);r.appendChild(b)})}
keys('r1','QWERTYUIOP');keys('r2','ASDFGHJKL');keys('r3','ZXCVBNM.,?');
q.addEventListener('keydown',e=>{if(e.key==='Enter')askAI()});
setInterval(()=>fetch('/ping').then(r=>r.text()).then(t=>statusBox.textContent=t).catch(()=>statusBox.textContent='disconnected'),9000);
</script>
</body>
</html>
)rawliteral";

const char* expressionName(EyeExpression value) {
  switch (value) {
    case EYE_IDLE: return "idle";
    case EYE_LISTENING: return "listening";
    case EYE_THINKING: return "thinking";
    case EYE_SEARCHING: return "searching";
    case EYE_HAPPY: return "happy";
    case EYE_CONFUSED: return "confused";
    case EYE_ERROR: return "error";
    case EYE_SLEEPING: return "sleeping";
    case EYE_SUCCESS: return "success";
    case EYE_LOVE: return "love";
    case EYE_WINK: return "wink";
    case EYE_SAD: return "sad";
    case EYE_EXCITED: return "excited";
    case EYE_SURPRISED: return "surprised";
    default: return "off";
  }
}

void sfxTone(int frequency,int ms){
  if(frequency<=0)return;
  tone(BUZZER_PIN,frequency,ms);
}

void sfxMove(){sfxTone(900,25);}
void sfxConfirm(){sfxTone(1200,70);}
void sfxBack(){sfxTone(550,55);}
void sfxError(){sfxTone(180,160);}
void sfxSuccess(){tone(BUZZER_PIN,1300,90);}
void sfxGame(){sfxTone(1500,35);}

void setExpression(EyeExpression next, unsigned long durationMs = 0) {
  if (next != EYE_OFF) previousExpression = next;
  expression = next;
  expressionUntil = durationMs ? millis() + durationMs : 0;

  targetEyeX = 0;
  targetEyeY = 0;
  targetEyeOpen = 1.0;

  if (next == EYE_LISTENING) targetEyeY = -3;
  else if (next == EYE_THINKING) { targetEyeX = 8; targetEyeY = -6; }
  else if (next == EYE_SEARCHING) targetEyeX = -8;
  else if (next == EYE_CONFUSED) { targetEyeX = 5; targetEyeOpen = 0.75; }
  else if (next == EYE_SLEEPING) targetEyeOpen = 0.08;
  else if (next == EYE_ERROR) targetEyeOpen = 0.65;
  else if (next == EYE_LOVE) targetEyeY = -2;
  else if (next == EYE_WINK) targetEyeOpen = 1.0;
  else if (next == EYE_SAD) { targetEyeY = 4; targetEyeOpen = 0.82; }
  else if (next == EYE_EXCITED) targetEyeOpen = 1.15;
  else if (next == EYE_SURPRISED) targetEyeOpen = 1.2;
}

void drawWifiEyes(bool connected) {
  oled.clearDisplay();
  if (connected) {
    // Minimal V3 eyes plus small Wi-Fi arcs.
    oled.fillRoundRect(31,20,24,24,6,WHITE);
    oled.fillRoundRect(73,20,24,24,6,WHITE);
    oled.drawCircle(64,52,2,WHITE);
    oled.drawLine(58,47,70,47,WHITE);
    oled.drawLine(54,42,74,42,WHITE);
  } else {
    // Minimal searching eyes with a scanning line.
    int scan = (millis()/16)%SCREEN_WIDTH;
    oled.fillRoundRect(31,20,24,24,6,WHITE);
    oled.fillRoundRect(73,20,24,24,6,WHITE);
    oled.drawLine(scan,8,scan,13,WHITE);
  }
  oled.display();
}

void drawTinyHeart(int x,int y){
  oled.fillCircle(x,y,2,WHITE);
  oled.fillCircle(x+4,y,2,WHITE);
  oled.fillTriangle(x-2,y+1,x+6,y+1,x+2,y+7,WHITE);
}

void drawSparkle(int x,int y){
  oled.drawPixel(x,y,WHITE);
  oled.drawLine(x-3,y,x+3,y,WHITE);
  oled.drawLine(x,y-3,x,y+3,WHITE);
}

void drawV3FaceShell(){
  // V3.1: no robot border, no head outline. The OLED black background is the face.
  if(eyeTheme==THEME_SPARKLE){drawSparkle(15,12);drawSparkle(112,51);}
  else if(eyeTheme==THEME_KAWAII){drawTinyHeart(12,10);drawTinyHeart(110,10);}
  else if(eyeTheme==THEME_FOCUS){oled.drawLine(35,13,94,13,WHITE);}
  else if(eyeTheme==THEME_PIXEL){for(int x=32;x<98;x+=7)oled.drawPixel(x,12,WHITE);}
}

void drawSoftEye(int cx,int cy,int w,int h,bool leftEye){
  int driftX=(int)eyeX/3;
  int driftY=(int)eyeY/3;
  cx+=driftX;cy+=driftY;

  bool sleepy=(expression==EYE_SLEEPING || h<3);
  bool winkRight=(!leftEye && expression==EYE_WINK);

  if(expression==EYE_LOVE && leftEye){drawTinyHeart(cx-8,cy-8);return;}
  if(expression==EYE_LOVE && !leftEye){drawTinyHeart(cx-7,cy-7);return;}

  if(eyeTheme==THEME_DEVIL){
    if(leftEye)oled.fillTriangle(cx-15,cy-7,cx+14,cy-1,cx+13,cy+8,WHITE);
    else oled.fillTriangle(cx+15,cy-7,cx-14,cy-1,cx-13,cy+8,WHITE);
    return;
  }

  if(sleepy){
    oled.drawLine(cx-15,cy,cx+15,cy,WHITE);
    oled.drawLine(cx-10,cy+2,cx+10,cy+2,WHITE);
    return;
  }

  if(leftEye){
    int size=(expression==EYE_SURPRISED||expression==EYE_EXCITED)?23:20;
    if(expression==EYE_SAD)size=17;
    if(eyeTheme==THEME_PIXEL){
      oled.fillRect(cx-size/2,cy-size/2,size,size,WHITE);
    }else{
      oled.drawRoundRect(cx-size/2-3,cy-size/2-3,size+6,size+6,6,WHITE);
      oled.drawRoundRect(cx-size/2-1,cy-size/2-1,size+2,size+2,5,WHITE);
      oled.fillRoundRect(cx-size/2,cy-size/2,size,size,5,WHITE);
    }
  }else{
    int size=(expression==EYE_SURPRISED||expression==EYE_EXCITED)?23:20;
    if(expression==EYE_SAD)size=17;
    if(winkRight){
      int ew=30,eh=5;
      oled.fillRoundRect(cx-ew/2,cy-eh/2,ew,eh,eh/2+1,WHITE);
      oled.drawRoundRect(cx-ew/2-2,cy-eh/2-2,ew+4,eh+4,eh/2+2,WHITE);
    }else if(eyeTheme==THEME_PIXEL){
      oled.fillRect(cx-size/2,cy-size/2,size,size,WHITE);
    }else{
      oled.drawRoundRect(cx-size/2-3,cy-size/2-3,size+6,size+6,6,WHITE);
      oled.drawRoundRect(cx-size/2-1,cy-size/2-1,size+2,size+2,5,WHITE);
      oled.fillRoundRect(cx-size/2,cy-size/2,size,size,5,WHITE);
    }
  }

  if(expression==EYE_ERROR){
    if(leftEye)oled.drawLine(cx-13,cy-15,cx+12,cy-9,WHITE);
    else oled.drawLine(cx-12,cy-9,cx+13,cy-15,WHITE);
  }else if(expression==EYE_CONFUSED){
    if(leftEye)oled.drawLine(cx-12,cy-14,cx+11,cy-14,WHITE);
    else oled.drawLine(cx-11,cy-9,cx+12,cy-16,WHITE);
  }else if(expression==EYE_SAD){
    if(leftEye)oled.drawLine(cx-13,cy-9,cx+11,cy-16,WHITE);
    else oled.drawLine(cx-11,cy-16,cx+13,cy-9,WHITE);
  }
}

void drawV3Mouth(){
  int pulse=(millis()/120)%4;
  if(expression==EYE_SLEEPING){
    oled.setCursor(58,45);oled.print("z");
  }else if(expression==EYE_SURPRISED){
    oled.drawCircle(64,45,3,WHITE);
  }else if(expression==EYE_SAD || expression==EYE_ERROR){
    oled.drawLine(58,48,64,45,WHITE);oled.drawLine(64,45,70,48,WHITE);
  }else if(expression==EYE_CONFUSED){
    oled.drawLine(58,46,70,46,WHITE);oled.drawPixel(72,45,WHITE);
  }else if(expression==EYE_EXCITED){
    oled.fillRoundRect(56,43,16,6,3,WHITE);
    oled.fillRect(58,43+pulse,12,1,BLACK);
  }else{
    oled.fillRoundRect(57,44,14,5,3,WHITE);
    oled.fillRect(59,44,10,2,BLACK);
  }
}

void updateAndDrawEyes() {
  if (showingAnswer) return;
  if (expression == EYE_OFF) {
    oled.clearDisplay();oled.display();return;
  }
  if (expressionUntil && millis() > expressionUntil) setExpression(EYE_IDLE);

  if (!blinking && millis() > nextBlink && expression != EYE_SLEEPING && expression != EYE_SURPRISED) {
    blinking = true;blinkStarted = millis();
  }
  if (blinking && millis() - blinkStarted > 150) {
    blinking = false;nextBlink = millis() + random(1400, 3600);
  }

  if(expression==EYE_SEARCHING)targetEyeX=((millis()/360)%2)?-8:8;
  else if(expression==EYE_THINKING)targetEyeX=((millis()/650)%2)?6:-3;
  else if(expression==EYE_EXCITED){targetEyeY=((millis()/180)%2)?-2:2;targetEyeOpen=1.12;}
  else if(expression==EYE_LOVE)targetEyeX=((millis()/500)%2)?1:-1;

  eyeX += (targetEyeX - eyeX) * 0.26;
  eyeY += (targetEyeY - eyeY) * 0.26;
  eyeOpen += (targetEyeOpen - eyeOpen) * 0.30;

  float blinkAmount=1.0;
  if(blinking){
    unsigned long part=millis()-blinkStarted;
    blinkAmount=part<75?1.0-part/75.0:(part-75)/75.0;
  }

  int h=max(1,(int)(18*eyeOpen*blinkAmount));
  int w=24;
  oled.clearDisplay();
  drawV3FaceShell();

  if(expression==EYE_SEARCHING){
    int scan=27+(millis()/18)%74;
    oled.drawLine(scan,13,scan,51,WHITE);
    oled.drawPixel(scan,9,WHITE);
  }
  if(expression==EYE_LISTENING){
    int pulse=3+(millis()/150)%4;
    oled.drawCircle(12,33,pulse,WHITE);
    oled.drawCircle(116,33,pulse,WHITE);
  }

  drawSoftEye(43,28,w,h,true);
  drawSoftEye(85,28,w,h,false);
  drawV3Mouth();

  if(expression==EYE_SUCCESS || expression==EYE_EXCITED){drawSparkle(34,16);drawSparkle(94,47);}
  if(expression==EYE_LOVE){drawTinyHeart(30,45);drawTinyHeart(91,45);}
  oled.display();
}

String cleanAnswerText(String text) {
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.replace("\t", " ");
  while (text.indexOf("  ") >= 0) text.replace("  ", " ");
  text.trim();
  return text;
}

int answerLineCount() {
  if (!lastAnswer.length()) return 1;
  int lines = 0;
  int position = 0;
  while (position < (int)lastAnswer.length()) {
    int remaining = lastAnswer.length() - position;
    int take = min(ANSWER_CHARS_PER_LINE, remaining);
    if (take < remaining) {
      int cut = lastAnswer.lastIndexOf(' ', position + take);
      if (cut >= position) take = cut - position;
    }
    if (take <= 0) take = min(ANSWER_CHARS_PER_LINE, remaining);
    position += take;
    while (position < (int)lastAnswer.length() && lastAnswer[position] == ' ') position++;
    lines++;
  }
  return max(1, lines);
}

int answerPageCount() {
  return max(1, (answerLineCount() + ANSWER_LINES_PER_PAGE - 1) / ANSWER_LINES_PER_PAGE);
}

void drawAnswerPage() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  int pageCount = answerPageCount();
  answerPage = constrain(answerPage, 0, pageCount - 1);

  int wantedFirstLine = answerPage * ANSWER_LINES_PER_PAGE;
  int lineNumber = 0;
  int drawn = 0;
  int position = 0;

  while (position < (int)lastAnswer.length() && drawn < ANSWER_LINES_PER_PAGE) {
    int remaining = lastAnswer.length() - position;
    int take = min(ANSWER_CHARS_PER_LINE, remaining);
    if (take < remaining) {
      int cut = lastAnswer.lastIndexOf(' ', position + take);
      if (cut >= position) take = cut - position;
    }
    if (take <= 0) take = min(ANSWER_CHARS_PER_LINE, remaining);

    if (lineNumber >= wantedFirstLine) {
      oled.setCursor(1, drawn * 10);
      oled.print(lastAnswer.substring(position, position + take));
      drawn++;
    }

    position += take;
    while (position < (int)lastAnswer.length() && lastAnswer[position] == ' ') position++;
    lineNumber++;
  }

  // Small page indicator in the lower-right corner.
  oled.fillRect(96, 56, 32, 8, SSD1306_BLACK);
  oled.setCursor(98, 56);
  oled.print(answerPage + 1);
  oled.print("/");
  oled.print(pageCount);
  oled.display();
}

void showAnswerOnOLED() {
  lastAnswer = cleanAnswerText(lastAnswer);
  answerPage = 0;
  showingAnswer = true;
  screenMode = MODE_ANSWER;
  drawAnswerPage();
}

void drawListScreen(const char* title, String items[], int count, int selected) {
  oled.clearDisplay();
  oled.setTextSize(1); oled.setTextColor(WHITE);
  oled.drawRoundRect(0,0,128,64,5,WHITE);
  oled.fillRoundRect(3,3,122,11,3,WHITE);
  oled.setTextColor(BLACK);oled.setCursor(7,5);oled.print(title);
  oled.setTextColor(WHITE);
  int first = constrain(selected - 2, 0, max(0,count-4));
  for (int row=0; row<4 && first+row<count; row++) {
    int index=first+row, y=18+row*11;
    if(index==selected){
      oled.fillRoundRect(4,y,120,10,3,WHITE);
      oled.setTextColor(BLACK);
      oled.setCursor(9,y+2);oled.print(">");
      oled.setCursor(18,y+2);oled.print(items[index].substring(0,17));
    }else{
      oled.setTextColor(WHITE);
      oled.setCursor(18,y+2); oled.print(items[index].substring(0,17));
    }
  }
  oled.setTextColor(WHITE);
  if(first>0)oled.fillTriangle(116,17,121,17,118,14,WHITE);
  if(first+4<count)oled.fillTriangle(116,60,121,60,118,63,WHITE);
  oled.display();
}

void drawSystemMenu() {
  String items[MENU_COUNT];
  for(int i=0;i<MENU_COUNT;i++) items[i]=MENU_ITEMS[i];
  drawListScreen("BOT MENU",items,MENU_COUNT,menuSelection);
}

void drawWifiMenu(){
  String items[WIFI_MENU_COUNT];
  for(int i=0;i<WIFI_MENU_COUNT;i++)items[i]=WIFI_MENU_ITEMS[i];
  drawListScreen("WIFI OPTIONS",items,WIFI_MENU_COUNT,wifiMenuSelection);
}

void drawWifiInfo(String title,String line1,String line2){
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);
  oled.setCursor(1,1);oled.print(title);oled.drawLine(0,10,127,10,WHITE);
  oled.setCursor(3,22);oled.print(line1.substring(0,20));
  oled.setCursor(3,35);oled.print(line2.substring(0,20));
  oled.setCursor(3,53);oled.print("BACK = OPTIONS");oled.display();
}

void showWifiConnectionInfo(){
  if(WiFi.status()==WL_CONNECTED)drawWifiInfo("WIFI CONNECTED",WiFi.SSID(),"IP "+WiFi.localIP().toString());
  else drawWifiInfo("WIFI STATUS","Not connected","Select CHANGE NETWORK");
}

void forgetSavedWifi(){
  preferences.begin("cute-ai-bot",false);
  preferences.remove("ssid");preferences.remove("pass");preferences.end();
  savedWifiSSID="";savedWifiPass="";wifiConnecting=false;WiFi.disconnect(true,false);
  drawWifiInfo("SAVED WIFI FORGOTTEN","Credentials deleted","Choose new network");
}

void scanWifiForOLED() {
  screenMode=MODE_WIFI;
  showingAnswer=false;
  wifiConnecting=false;
  oled.clearDisplay();
  oled.setTextSize(1);oled.setTextColor(WHITE);
  oled.setCursor(26,18);oled.print("SCANNING WIFI");
  oled.drawRoundRect(20,36,88,8,4,WHITE);
  oled.fillRoundRect(22,38,34,4,2,WHITE);
  oled.display();
  int foundNetworks=(int)WiFi.scanNetworks();
  if(foundNetworks<0)foundNetworks=0;
  wifiNetworkCount=min(12,foundNetworks);
  for(int i=0;i<wifiNetworkCount;i++) wifiNetworks[i]=WiFi.SSID(i);
  WiFi.scanDelete();
  wifiSelection=0;
  if(wifiNetworkCount==0){
    oled.clearDisplay(); oled.setTextSize(1);oled.setTextColor(WHITE);
    oled.setCursor(8,20); oled.print("NO NETWORKS FOUND");
    oled.setCursor(8,36); oled.print("ENTER = RESCAN");
    oled.setCursor(8,50); oled.print("BACK = OPTIONS");
    oled.display();
  } else drawListScreen("SELECT WIFI",wifiNetworks,wifiNetworkCount,wifiSelection);
}

void drawWeatherIcon(int code) {
  if(code==0 || code==1){
    oled.drawCircle(95,27,9,WHITE);
    for(int a=0;a<8;a++){float r=a*0.785;oled.drawLine(95+cos(r)*13,27+sin(r)*13,95+cos(r)*17,27+sin(r)*17,WHITE);}
  } else if(code<=3 || code==45 || code==48){
    oled.fillCircle(88,29,7,WHITE);oled.fillCircle(99,25,10,WHITE);oled.fillCircle(109,30,7,WHITE);oled.fillRect(82,30,34,7,WHITE);
  } else if(code>=95){
    oled.fillCircle(91,24,7,WHITE);oled.fillCircle(103,24,9,WHITE);oled.fillRect(85,25,28,7,WHITE);
    oled.drawLine(101,32,94,43,WHITE);oled.drawLine(94,43,102,41,WHITE);oled.drawLine(102,41,97,51,WHITE);
  } else {
    oled.fillCircle(91,23,7,WHITE);oled.fillCircle(103,23,9,WHITE);oled.fillRect(85,24,28,7,WHITE);
    for(int x=89;x<112;x+=7) oled.drawLine(x,34,x-3,42,WHITE);
  }
}

void drawWeather() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(WHITE);
  oled.setCursor(1,1); oled.print("VIJAYAWADA WEATHER");
  oled.drawLine(0,10,127,10,WHITE);
  oled.setTextSize(2); oled.setCursor(2,18); oled.print((int)weatherTemp); oled.print("C");
  oled.setTextSize(1); oled.setCursor(2,39); oled.print(weatherLabel.substring(0,18));
  oled.setCursor(2,51); oled.print("Wind "); oled.print((int)weatherWind); oled.print(" km/h");
  drawWeatherIcon(weatherCode); oled.display();
}

String weatherName(int code){
  if(code==0)return "Clear sky"; if(code<=3)return "Cloudy"; if(code==45||code==48)return "Fog";
  if(code>=95)return "Thunderstorm"; if(code>=51&&code<=67)return "Rain";
  if(code>=80&&code<=82)return "Showers"; return "Mixed weather";
}

bool fetchWeather() {
  if(WiFi.status()!=WL_CONNECTED)return false;
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  String url="https://api.open-meteo.com/v1/forecast?latitude=16.5062&longitude=80.6480&current=temperature_2m,weather_code,wind_speed_10m&timezone=Asia%2FKolkata";
  if(!http.begin(client,url))return false;
  int code=http.GET(); String body=http.getString(); http.end();
  if(code!=200)return false;
  DynamicJsonDocument doc(4096); if(deserializeJson(doc,body))return false;
  weatherTemp=doc["current"]["temperature_2m"]|0; weatherCode=doc["current"]["weather_code"]|-1; weatherWind=doc["current"]["wind_speed_10m"]|0;
  weatherLabel=weatherName(weatherCode); return true;
}

void resetSnake(){
  snakeLength=4;snakeDX=1;snakeDY=0;snakeScore=0;snakeGameOver=false;
  for(int i=0;i<snakeLength;i++){snakeX[i]=10-i;snakeY[i]=6;}
  snakeFoodX=random(1,31);snakeFoodY=random(1,13);
}

void drawSnake(){
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);oled.setCursor(1,0);oled.print("SNAKE ");oled.print(snakeScore);
  oled.drawRect(0,9,128,55,WHITE);
  for(int i=0;i<snakeLength;i++)oled.fillRect(snakeX[i]*4,snakeY[i]*4+9,4,4,WHITE);
  oled.drawRect(snakeFoodX*4+1,snakeFoodY*4+10,2,2,WHITE);
  if(snakeGameOver){oled.fillRect(24,25,80,20,BLACK);oled.drawRect(24,25,80,20,WHITE);oled.setCursor(35,29);oled.print("GAME OVER");oled.setCursor(30,37);oled.print("ENTER retry");}
  oled.display();
}

void updateSnake(){
  if(snakeGameOver)return;
  int nx=snakeX[0]+snakeDX,ny=snakeY[0]+snakeDY;
  if(nx<0||nx>31||ny<0||ny>12){snakeGameOver=true;drawSnake();return;}
  for(int i=0;i<snakeLength;i++)if(snakeX[i]==nx&&snakeY[i]==ny){snakeGameOver=true;drawSnake();return;}
  for(int i=snakeLength;i>0;i--){snakeX[i]=snakeX[i-1];snakeY[i]=snakeY[i-1];}
  snakeX[0]=nx;snakeY[0]=ny;
  if(nx==snakeFoodX&&ny==snakeFoodY){sfxGame();if(snakeLength<47)snakeLength++;snakeScore++;snakeFoodX=random(1,31);snakeFoodY=random(1,13);}
  drawSnake();
}

void resetPong(){pongPaddle=24;pongCpu=24;pongBallX=64;pongBallY=32;pongDX=random(2)?2:-2;pongDY=random(2)?1:-1;}
void drawPong(){
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);oled.setCursor(1,0);oled.print("PONG ");oled.print(pongScore);
  oled.drawRect(0,9,128,55,WHITE);for(int y=12;y<62;y+=6)oled.drawPixel(64,y,WHITE);
  oled.fillRect(3,pongPaddle,3,14,WHITE);oled.fillRect(122,pongCpu,3,14,WHITE);oled.fillRect(pongBallX,pongBallY,3,3,WHITE);oled.display();
}
void updatePong(){
  pongBallX+=pongDX;pongBallY+=pongDY;if(pongBallY<=10||pongBallY>=60)pongDY=-pongDY;
  if(pongBallX<=6&&pongBallY+2>=pongPaddle&&pongBallY<=pongPaddle+14){pongDX=2;pongScore++;sfxGame();}
  if(pongBallX>=119&&pongBallY+2>=pongCpu&&pongBallY<=pongCpu+14)pongDX=-2;
  if(pongBallY>pongCpu+8)pongCpu++;else if(pongBallY<pongCpu+5)pongCpu--;pongCpu=constrain(pongCpu,10,49);
  if(pongBallX<0||pongBallX>127){if(pongBallX<0)pongScore=0;resetPong();}drawPong();
}

void drawHeartShape(int x,int y,int size){
  oled.fillCircle(x,y,size,WHITE);oled.fillCircle(x+size*2,y,size,WHITE);
  oled.fillTriangle(x-size,y,x+size*3,y,x+size,y+size*4,WHITE);
}

void drawLoveStory(){
  const char* lines1[]={"From the moment","Your smile became","Every little moment","No matter where","There is one thing"};
  const char* lines2[]={"you entered my life","my favorite light","with you feels magic","life takes us","I always want to say"};
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);
  if(storyPage<5){
    drawHeartShape(54,8,5);oled.drawRoundRect(4,34,120,26,5,WHITE);
    oled.setCursor(8,39);oled.print(lines1[storyPage]);oled.setCursor(8,49);oled.print(lines2[storyPage]);
  }else{
    drawHeartShape(48,8,8);oled.setTextSize(2);oled.setCursor(18,45);oled.print("I love u");
  }
  oled.display();
}

void drawAILoading(const char* status){
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);
  oled.setCursor(35,3);oled.print(status);
  int phase=(millis()/90)%18;
  oled.fillRoundRect(33,21,22,22,6,WHITE);
  oled.fillRoundRect(73,21,22,22,6,WHITE);
  oled.fillRoundRect(57,46,14,5,3,WHITE);
  oled.fillRect(59,46,10,2,BLACK);
  oled.drawRoundRect(18,55,92,6,3,WHITE);
  oled.fillRoundRect(20,57,(phase*5)%88,2,1,WHITE);
  drawSparkle(114,19);
  oled.display();
}

void drawBirthdayCake(const char* l1,const char* l2){
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);
  oled.setCursor(6,4);oled.print(l1);oled.setCursor(6,14);oled.print(l2);
  oled.drawLine(29,57,99,57,WHITE);
  oled.fillRoundRect(38,41,52,13,3,WHITE);oled.fillRoundRect(44,31,40,11,3,WHITE);
  oled.fillRect(42,45,44,2,BLACK);oled.fillRect(48,34,30,2,BLACK);
  for(int i=0;i<4;i++){int x=50+i*9;oled.drawLine(x,25,x,31,WHITE);if((birthdayFrame+i)%2)oled.drawPixel(x,22,WHITE);else oled.drawLine(x-1,23,x+1,21,WHITE);}
  for(int i=0;i<10;i++){int x=(i*23+birthdayFrame*5)%128;int y=2+((i*13+birthdayFrame*3)%20);oled.drawPixel(x,y,WHITE);}
  oled.display();birthdayFrame++;
}

void startTune(int mode){
  screenMode=MODE_BIRTHDAY;
  birthdayPlaying=true;
  birthdayNote=0;
  birthdayNextNote=0;
  birthdayFrame=0;
  tuneMode=mode;
  if(mode==1)tuneTitle="ODE TO JOY";
  else tuneTitle="BIRTHDAY";
}

void startBirthdaySong(){startTune(0);}
void startFamousTune(){startTune(1);}
void restartCurrentTune(){startTune(tuneMode);}

void getCurrentTune(int*& melody,int*& durations,int& count){
  if(tuneMode==1){melody=odeMelody;durations=odeDurations;count=ODE_NOTES;}
  else{melody=birthdayMelody;durations=birthdayDurations;count=BIRTHDAY_NOTES;}
}

void drawTuneEnd(){
  oled.clearDisplay();oled.setTextColor(WHITE);
  if(tuneMode==1){
    oled.setTextSize(1);oled.setCursor(30,8);oled.print("ODE TO JOY");
    oled.setTextSize(2);oled.setCursor(25,30);oled.print("JOY!");
    oled.drawLine(11,59,117,59,WHITE);
  }else{
    oled.setTextSize(2);oled.setCursor(16,17);oled.print("HAPPY");
    oled.setCursor(2,38);oled.print("BIRTHDAY");
  }
  oled.display();
}

void updateBirthdaySong(){
  if(!birthdayPlaying)return;
  if(millis()<birthdayNextNote)return;
  int* melody;int* durations;int count;
  getCurrentTune(melody,durations,count);
  if(birthdayNote>=count){
    noTone(BUZZER_PIN);birthdayPlaying=false;
    drawTuneEnd();return;
  }
  if(tuneMode==1){
    if(birthdayNote<8)drawBirthdayCake("Ode to Joy","classic tune");
    else if(birthdayNote<15)drawBirthdayCake("Bright melody","happy bot");
    else if(birthdayNote<23)drawBirthdayCake("Joyful notes","on buzzer");
    else drawBirthdayCake("Final line","big smile");
  }else{
    if(birthdayNote<6)drawBirthdayCake("Happy birthday","to you");
    else if(birthdayNote<12)drawBirthdayCake("Happy birthday","to you");
    else if(birthdayNote<19)drawBirthdayCake("Happy birthday","dear friend");
    else drawBirthdayCake("Happy birthday","to you!");
  }
  int d=1000/durations[birthdayNote];
  tone(BUZZER_PIN,melody[birthdayNote],d);
  birthdayNextNote=millis()+d*1.35;
  birthdayNote++;
}

bool supabaseConfigured(){return String(SUPABASE_URL).indexOf("YOUR_PROJECT")<0 && String(SUPABASE_KEY).indexOf("PASTE_")<0;}

void rememberLocally(String q,String a){
  if(memoryCount<4)memoryCount++;
  for(int i=memoryCount-1;i>0;i--){memoryQuestion[i]=memoryQuestion[i-1];memoryAnswer[i]=memoryAnswer[i-1];}
  memoryQuestion[0]=q; memoryAnswer[0]=a;
}

void saveToSupabase(String q,String a){
  if(!supabaseConfigured()||WiFi.status()!=WL_CONNECTED)return;
  WiFiClientSecure client;client.setInsecure();HTTPClient http;
  String base=String(SUPABASE_URL);
  if(base.endsWith("/rest/v1/"))base.remove(base.length()-9);
  else if(base.endsWith("/rest/v1"))base.remove(base.length()-8);
  if(base.endsWith("/"))base.remove(base.length()-1);
  String url=base+"/rest/v1/chat_memory";
  if(!http.begin(client,url))return;
  http.addHeader("apikey",SUPABASE_KEY);http.addHeader("Authorization",String("Bearer ")+SUPABASE_KEY);
  http.addHeader("Content-Type","application/json");http.addHeader("Prefer","return=minimal");
  String body="{\"question\":\""+jsonEscape(q)+"\",\"answer\":\""+jsonEscape(a)+"\"}";
  http.POST(body);http.end();
}

void loadSupabaseMemory(){
  if(!supabaseConfigured()||WiFi.status()!=WL_CONNECTED)return;
  WiFiClientSecure client;client.setInsecure();HTTPClient http;
  String base=String(SUPABASE_URL);
  if(base.endsWith("/rest/v1/"))base.remove(base.length()-9);
  else if(base.endsWith("/rest/v1"))base.remove(base.length()-8);
  if(base.endsWith("/"))base.remove(base.length()-1);
  String url=base+"/rest/v1/chat_memory?select=question,answer&order=created_at.desc&limit=4";
  if(!http.begin(client,url))return;
  http.addHeader("apikey",SUPABASE_KEY);http.addHeader("Authorization",String("Bearer ")+SUPABASE_KEY);
  int status=http.GET();String body=http.getString();http.end();if(status!=200)return;
  DynamicJsonDocument doc(12288);if(deserializeJson(doc,body))return;
  memoryCount=0;for(JsonObject row:doc.as<JsonArray>()){if(memoryCount>=4)break;memoryQuestion[memoryCount]=row["question"].as<String>();memoryAnswer[memoryCount]=row["answer"].as<String>();memoryCount++;}
}

String jsonEscape(String input) {
  String output = "";
  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\\') output += "\\\\";
    else if (c == '"') output += "\\\"";
    else if (c == '\n' || c == '\r') output += " ";
    else if ((uint8_t)c >= 32) output += c;
  }
  return output;
}

String askAI(String question, bool &ok) {
  ok = false;

  if (WiFi.status() != WL_CONNECTED) return "No internet Wi-Fi. Open the OLED menu and choose WIFI SETUP.";
  if (String(GROQ_API_KEY) == "PASTE_YOUR_GROQ_API_KEY_HERE") return "Add your Groq API key near the top of the Arduino code.";

  setExpression(EYE_THINKING);
  for (int i = 0; i < 8; i++) { updateAndDrawEyes(); delay(35); }
  setExpression(EYE_SEARCHING);
  for (int i = 0; i < 8; i++) { updateAndDrawEyes(); delay(35); }

  // Keep visible feedback during slow or failed HTTPS requests.
  drawAILoading("ASKING AI...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(client, AI_URL)) return "Could not start secure AI connection.";
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  String body = "{\"model\":\"";
  body += AI_MODEL;
  body += "\",";
  body += "\"messages\":[";
  body += "{\"role\":\"system\",\"content\":\"You are a useful cute assistant with memory. Answer briefly when a short answer is enough. Give a longer complete answer only when necessary or requested. Avoid markdown unless requested.\"},";
  for(int i=memoryCount-1;i>=0;i--){
    body += "{\"role\":\"user\",\"content\":\""+jsonEscape(memoryQuestion[i])+"\"},";
    body += "{\"role\":\"assistant\",\"content\":\""+jsonEscape(memoryAnswer[i])+"\"},";
  }
  body += "{\"role\":\"user\",\"content\":\"" + jsonEscape(question) + "\"}],";
  body += "\"temperature\":0.7,\"max_tokens\":350,\"stream\":false}";

  int statusCode = http.POST(body);
  String responseBody = http.getString();
  http.end();

  if (statusCode <= 0) return "AI request timed out. Check Wi-Fi and create a new Groq API key.";

  DynamicJsonDocument document(12288);
  DeserializationError error = deserializeJson(document, responseBody);
  if (error) return "Could not read AI response JSON.";

  if (statusCode < 200 || statusCode >= 300) {
    const char* apiError = document["error"]["message"] | "Unknown API error";
    return String(apiError);
  }

  const char* content = document["choices"][0]["message"]["content"] | "";
  String answer = String(content);
  answer.trim();
  if (!answer.length()) return "The AI returned an empty response.";

  ok = true;
  return answer;
}

void startWifiConnection(String ssid, String password, bool saveCredentials) {
  savedWifiSSID = ssid;
  savedWifiPass = password;
  wifiConnecting = true;
  wifiConnectStarted = millis();
  showingAnswer = false;
  setExpression(EYE_SEARCHING);
  WiFi.begin(ssid.c_str(), password.c_str());

  if (saveCredentials) {
    preferences.begin("cute-ai-bot", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();
  }
}

void handleWifiScan() {
  int count = WiFi.scanNetworks();
  DynamicJsonDocument document(4096);
  JsonArray networks = document.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject item = networks.createNestedObject();
    item["ssid"] = WiFi.SSID(i);
    item["rssi"] = WiFi.RSSI(i);
    item["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  String output;
  serializeJson(document, output);
  WiFi.scanDelete();
  server.send(200, "application/json", output);
}

void handleWifiConnect() {
  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  if (!ssid.length()) {
    server.send(400, "text/plain", "Enter or select a Wi-Fi name.");
    return;
  }
  startWifiConnection(ssid, password, true);
  server.send(200, "text/plain", "Connecting to " + ssid + "...");
}

String wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Connected: " + WiFi.SSID() + " | IP: " + WiFi.localIP().toString();
  }
  if (wifiConnecting) return "Connecting to " + savedWifiSSID + "...";
  return "Not connected to internet Wi-Fi.";
}

EyeExpression expressionFromName(String name) {
  if (name == "listening") return EYE_LISTENING;
  if (name == "thinking") return EYE_THINKING;
  if (name == "searching") return EYE_SEARCHING;
  if (name == "happy") return EYE_HAPPY;
  if (name == "confused") return EYE_CONFUSED;
  if (name == "error") return EYE_ERROR;
  if (name == "sleeping") return EYE_SLEEPING;
  if (name == "success") return EYE_SUCCESS;
  if (name == "love") return EYE_LOVE;
  if (name == "wink") return EYE_WINK;
  if (name == "sad") return EYE_SAD;
  if (name == "excited") return EYE_EXCITED;
  if (name == "surprised") return EYE_SURPRISED;
  return EYE_IDLE;
}

void handleAsk() {
  if (!server.hasArg("q") || !server.arg("q").length()) {
    server.send(400, "text/plain", "Please enter a question.");
    return;
  }

  aiBusy = true;
  lastQuestion = server.arg("q");
  bool ok = false;
  lastAnswer = askAI(lastQuestion, ok);
  aiBusy = false;

  if (ok) {
    sfxSuccess();
    rememberLocally(lastQuestion,lastAnswer);
    saveToSupabase(lastQuestion,lastAnswer);
    showAnswerOnOLED();
    server.send(200, "text/plain; charset=utf-8", "Answer displayed on OLED. Use navigation buttons to read pages.");
  } else {
    sfxError();
    showAnswerOnOLED();
    server.send(500, "text/plain; charset=utf-8", "Error details displayed on OLED.");
  }
}

void handleFace() {
  sfxConfirm();
  screenMode=MODE_EYES;
  showingAnswer = false;
  setExpression(expressionFromName(server.arg("name")));
  server.send(200, "text/plain", "Expression changed");
}

void handleTheme(){
  sfxConfirm();
  eyeTheme=(EyeTheme)constrain(server.arg("id").toInt(),0,(int)THEME_COUNT-1);
  screenMode=MODE_EYES;showingAnswer=false;setExpression(EYE_HAPPY,1000);
  server.send(200,"text/plain","Eye pair changed");
}

void openMenu(){screenMode=MODE_MENU;showingAnswer=false;drawSystemMenu();}

void activateMenuItem(){
  sfxConfirm();
  if(menuSelection==0){screenMode=MODE_EYES;setExpression(EYE_LISTENING);}
  else if(menuSelection==1){screenMode=MODE_WIFI_MENU;wifiMenuSelection=0;drawWifiMenu();}
  else if(menuSelection==2){screenMode=MODE_WEATHER;if(fetchWeather())drawWeather();else{weatherLabel="No internet";drawWeather();}}
  else if(menuSelection==3){eyeTheme=(EyeTheme)((eyeTheme+1)%THEME_COUNT);screenMode=MODE_EYES;setExpression(EYE_HAPPY,1000);}
  else if(menuSelection==4){screenMode=MODE_SNAKE;resetSnake();drawSnake();}
  else if(menuSelection==5){screenMode=MODE_PONG;pongScore=0;resetPong();drawPong();}
  else if(menuSelection==6){screenMode=MODE_LOVE_STORY;storyPage=0;storyStarted=millis();drawLoveStory();}
  else if(menuSelection==7){startBirthdaySong();}
  else if(menuSelection==8){startFamousTune();}
}

void activateWifiMenuItem(){
  sfxConfirm();
  if(wifiMenuSelection==0)scanWifiForOLED();
  else if(wifiMenuSelection==1)showWifiConnectionInfo();
  else if(wifiMenuSelection==2){wifiConnecting=false;WiFi.disconnect(false,false);drawWifiInfo("WIFI DISCONNECTED","Saved Wi-Fi kept","ENTER to reconnect");}
  else if(wifiMenuSelection==3)forgetSavedWifi();
}

void handleOledText(){
  oledInput=server.arg("text");
  if(screenMode==MODE_WIFI && wifiNetworkCount>0){
    sfxConfirm();
    startWifiConnection(wifiNetworks[wifiSelection],oledInput,true);
    screenMode=MODE_EYES;
    server.send(200,"text/plain","Connecting to "+wifiNetworks[wifiSelection]);
  }else server.send(200,"text/plain","Text received. Open WIFI SETUP to use it as password.");
}

void handleControl() {
  String key = server.arg("key");
  bool fast = server.hasArg("fast");
  server.sendHeader("Cache-Control","no-store");
  server.sendHeader("Connection","close");
  if(key=="menu"){sfxConfirm();openMenu();server.send(200,"text/plain","OLED menu opened");return;}
  if(key=="wifi"){if(!fast)sfxConfirm();screenMode=MODE_WIFI_MENU;showingAnswer=false;wifiMenuSelection=0;drawWifiMenu();server.send(200,"text/plain","Wi-Fi options opened");return;}
  if(key=="scanwifi"){if(!fast)sfxConfirm();showingAnswer=false;scanWifiForOLED();server.send(200,"text/plain","Wi-Fi scan finished");return;}
  if (showingAnswer) {
    if (key == "right" || key == "down" || key == "enter") {
      if(!fast)sfxMove();
      answerPage = min(answerPage + 1, answerPageCount() - 1);
      drawAnswerPage();
    } else if (key == "left" || key == "up") {
      if(!fast)sfxMove();
      answerPage = max(answerPage - 1, 0);
      drawAnswerPage();
    } else if (key == "back") {
      if(!fast)sfxBack();
      showingAnswer = false;
      screenMode = MODE_EYES;
      setExpression(EYE_SUCCESS, 1400);
    }
    server.send(200, "text/plain", showingAnswer ? "OLED answer page " + String(answerPage + 1) + "/" + String(answerPageCount()) : "Returned to eyes");
    return;
  }

  if(screenMode==MODE_MENU){
    if(key=="up"){menuSelection=max(0,menuSelection-1);if(!fast)sfxMove();}
    else if(key=="down"){menuSelection=min(MENU_COUNT-1,menuSelection+1);if(!fast)sfxMove();}
    else if(key=="enter")activateMenuItem();
    else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_EYES;setExpression(EYE_IDLE);}
    if(screenMode==MODE_MENU)drawSystemMenu();
  }else if(screenMode==MODE_WIFI_MENU){
    if(key=="up"){wifiMenuSelection=max(0,wifiMenuSelection-1);if(!fast)sfxMove();}
    else if(key=="down"){wifiMenuSelection=min(WIFI_MENU_COUNT-1,wifiMenuSelection+1);if(!fast)sfxMove();}
    else if(key=="enter"){
      if(wifiMenuSelection==2 && WiFi.status()!=WL_CONNECTED && savedWifiSSID.length())startWifiConnection(savedWifiSSID,savedWifiPass,false);
      else activateWifiMenuItem();
    }else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
    if(screenMode==MODE_WIFI_MENU && (key=="up"||key=="down"))drawWifiMenu();
  }else if(screenMode==MODE_WIFI){
    if(key=="up"){wifiSelection=max(0,wifiSelection-1);if(!fast)sfxMove();}
    else if(key=="down"){wifiSelection=min(wifiNetworkCount-1,wifiSelection+1);if(!fast)sfxMove();}
    else if(key=="enter"){
      if(wifiNetworkCount<=0){if(!fast)sfxConfirm();scanWifiForOLED();}
      else{oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(WHITE);oled.setCursor(5,18);oled.print("SELECTED:");oled.setCursor(5,29);oled.print(wifiNetworks[wifiSelection].substring(0,19));oled.setCursor(5,45);oled.print("SEND PASSWORD FROM");oled.setCursor(5,54);oled.print("PHONE OLED TEXT");oled.display();}
    }else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_WIFI_MENU;drawWifiMenu();}
    if(screenMode==MODE_WIFI && key!="enter" && wifiNetworkCount>0)drawListScreen("SELECT WIFI",wifiNetworks,wifiNetworkCount,wifiSelection);
  }else if(screenMode==MODE_WEATHER){
    if(key=="enter"){if(!fast)sfxConfirm();fetchWeather();drawWeather();}else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
  }else if(screenMode==MODE_SNAKE){
    if(key=="up"&&snakeDY!=1){snakeDX=0;snakeDY=-1;}else if(key=="down"&&snakeDY!=-1){snakeDX=0;snakeDY=1;}
    else if(key=="left"&&snakeDX!=1){snakeDX=-1;snakeDY=0;}else if(key=="right"&&snakeDX!=-1){snakeDX=1;snakeDY=0;}
    else if(key=="enter"&&snakeGameOver){if(!fast)sfxConfirm();resetSnake();}else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
  }else if(screenMode==MODE_PONG){
    if(key=="up"){pongPaddle=max(10,pongPaddle-6);}else if(key=="down"){pongPaddle=min(49,pongPaddle+6);}else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
  }else if(screenMode==MODE_LOVE_STORY){
    if(key=="right"||key=="down"||key=="enter"){storyPage=min(5,storyPage+1);if(!fast)sfxMove();}
    else if(key=="left"||key=="up"){storyPage=max(0,storyPage-1);if(!fast)sfxMove();}
    else if(key=="back"){if(!fast)sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
    if(screenMode==MODE_LOVE_STORY){storyStarted=millis();drawLoveStory();}
  }else if(screenMode==MODE_BIRTHDAY){
    if(key=="enter"){sfxConfirm();restartCurrentTune();}
    else if(key=="back"){noTone(BUZZER_PIN);birthdayPlaying=false;sfxBack();screenMode=MODE_MENU;drawSystemMenu();}
  }else{
    if (key == "left") { setExpression(EYE_LISTENING, 900); targetEyeX = -8; }
    else if (key == "right") { setExpression(EYE_LISTENING, 900); targetEyeX = 8; }
    else if (key == "up") { setExpression(EYE_LISTENING, 900); targetEyeY = -6; }
    else if (key == "down") { setExpression(EYE_LISTENING, 900); targetEyeY = 6; }
    else if (key == "enter") setExpression(EYE_HAPPY, 1200);
    else if (key == "back") openMenu();
  }
  server.send(200, "text/plain", "control: " + key);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("SSD1306 OLED not found.");
      while (true) delay(100);
    }
  }

  oled.clearDisplay();
  oled.setTextSize(1);oled.setTextColor(WHITE);
  oled.drawRoundRect(31,18,24,24,7,WHITE);
  oled.drawRoundRect(33,20,20,20,6,WHITE);
  oled.fillRoundRect(35,22,16,16,5,WHITE);
  oled.drawRoundRect(73,18,24,24,7,WHITE);
  oled.drawRoundRect(75,20,20,20,6,WHITE);
  oled.fillRoundRect(77,22,16,16,5,WHITE);
  oled.fillRoundRect(57,46,14,5,3,WHITE);
  oled.setCursor(37,56);oled.print("CUTE BOT V3");
  oled.display();
  delay(1800);
  randomSeed(micros());
  nextBlink = millis() + 1200;
  setExpression(EYE_HAPPY, 1600);

  // AP_STA lets the phone connect to ESP32 while ESP32 uses home Wi-Fi for AI.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(BOT_AP_SSID, BOT_AP_PASS);

  preferences.begin("cute-ai-bot", true);
  savedWifiSSID = preferences.getString("ssid", "");
  savedWifiPass = preferences.getString("pass", "");
  preferences.end();

  if (savedWifiSSID.length()) {
    startWifiConnection(savedWifiSSID, savedWifiPass, false);
  }

  server.on("/", []() {
    server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma","no-cache");
    server.send_P(200, "text/html", WEB_PAGE);
  });
  server.on("/ask", HTTP_POST, handleAsk);
  server.on("/face", HTTP_GET, handleFace);
  server.on("/theme", HTTP_GET, handleTheme);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/oled/text", HTTP_POST, handleOledText);
  server.on("/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/wifi/status", HTTP_GET, []() { server.send(200, "text/plain", wifiStatusText()); });
  server.on("/ping", HTTP_GET, []() {
    String status = aiBusy ? "AI is working..." : "ready | ";
    status += WiFi.status() == WL_CONNECTED ? "internet connected" : "no internet";
    server.send(200, "text/plain", status);
  });
  server.begin();

  Serial.print("Phone control page: http://");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();

  static wl_status_t previousWifiStatus = WL_IDLE_STATUS;
  wl_status_t currentWifiStatus = WiFi.status();
  if (currentWifiStatus != previousWifiStatus) {
    previousWifiStatus = currentWifiStatus;
    if (currentWifiStatus == WL_CONNECTED) {
      wifiConnecting = false;
      showingAnswer = false;
      setExpression(EYE_SUCCESS, 1800);
      sfxSuccess();
      drawWifiEyes(true);
      loadSupabaseMemory();
    }
  }

  if (wifiConnecting) {
    if (currentWifiStatus == WL_CONNECTED) {
      wifiConnecting = false;
    } else if (millis() - wifiConnectStarted > 20000) {
      wifiConnecting = false;
      sfxError();
      setExpression(EYE_ERROR, 3000);
    } else if (millis() - lastEyeFrame >= 40) {
      lastEyeFrame = millis();
      drawWifiEyes(false);
    }
  }

  if (!aiBusy && millis() - lastEyeFrame >= 33) {
    lastEyeFrame = millis();
    if (wifiConnecting) {}
    else if (showingAnswer) {}
    else if(screenMode==MODE_EYES) updateAndDrawEyes();
  }

  if(screenMode==MODE_SNAKE && millis()-lastGameFrame>135){
    lastGameFrame=millis();updateSnake();
  }else if(screenMode==MODE_PONG && millis()-lastGameFrame>34){
    lastGameFrame=millis();updatePong();
  }else if(screenMode==MODE_LOVE_STORY && storyPage<5 && millis()-storyStarted>3500){
    storyStarted=millis();storyPage++;drawLoveStory();
  }else if(screenMode==MODE_BIRTHDAY){
    updateBirthdaySong();
  }

  delay(1);
}
