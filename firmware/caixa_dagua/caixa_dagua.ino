// Automação da Caixa d'Água - firmware v2 (ESP32)
// Sensor ultrassônico + relé Wi-Fi + API HTTP para a IHM do PC (+ TFT local opcional)
//
// Bibliotecas: TFT_eSPI (só se USAR_TFT_LOCAL = 1). O resto vem no pacote ESP32.

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ---------- CONFIGURAR ----------
#define USAR_TFT_LOCAL 1          // 0 = sem display neste ESP32
const char* WIFI_SSID = "SUA_REDE";
const char* WIFI_PASS = "SUA_SENHA";
const char* RELE_IP   = "192.168.0.50";   // use IP fixo (reserva no roteador)
enum TipoRele { SHELLY_GEN1, SHELLY_GEN2, TASMOTA };
const TipoRele RELE_TIPO = SHELLY_GEN1;   // Shelly 1 = GEN1 | Shelly Plus 1 = GEN2
// --------------------------------

#define TRIG 5
#define ECHO 18

#if USAR_TFT_LOCAL
#include <TFT_eSPI.h>
TFT_eSPI tft;
#endif

// ---- Configuração (salva na flash, editável pela IHM) ----
struct Config {
  float vazia = 120;   // cm do sensor até o fundo
  float cheia = 30;    // cm do sensor até a água com a caixa cheia (>= 25)
  int   liga = 25;     // % para ligar
  int   desliga = 98;  // % para desligar
  int   tmax = 60;     // minutos máximos de bomba ligada
} cfg;
Preferences prefs;
WebServer server(80);

// ---- Alarmes ----
#define ALM_SENSOR  1   // sem leitura válida
#define ALM_RELE    2   // relé não responde
#define ALM_TEMPO   4   // bomba passou do tempo máximo (travado até reconhecer)
#define ALM_CRITICO 8   // nível <= 10%
const char* ALM_ID[4]   = {"sensor", "rele", "tempo", "critico"};
const char* ALM_MSG[4]  = {"Falha do sensor", "Falha de comunicacao com o rele",
                           "Tempo maximo da bomba excedido", "Nivel critico (<=10%)"};
uint8_t alarmes = 0;

// ---- Histórico de eventos (circular, 20) ----
struct Evento { uint32_t t; uint32_t u; char m[56]; };
Evento ev[20];
uint16_t evTotal = 0;

void logEv(const String& m) {
  time_t now = time(nullptr);
  Evento& e = ev[evTotal % 20];
  e.t = now > 1700000000 ? (uint32_t)now : 0;
  e.u = millis() / 1000;
  strncpy(e.m, m.c_str(), sizeof(e.m) - 1);
  e.m[sizeof(e.m) - 1] = 0;
  evTotal++;
  Serial.println(m);
}

void setAlarme(uint8_t bit, bool on) {
  int i = 0; while ((1 << i) != bit) i++;
  if (on && !(alarmes & bit)) {
    alarmes |= bit; logEv(String("ALARME: ") + ALM_MSG[i]);
  } else if (!on && (alarmes & bit) && bit != ALM_TEMPO) {   // TEMPO só sai com reconhecimento
    alarmes &= ~bit; logEv(String("Normalizado: ") + ALM_MSG[i]);
  }
}

// ---- Estado ----
bool  modoAuto = true;
bool  desejada = false;        // estado que queremos na bomba
bool  bombaReal = true;        // último estado confirmado pelo relé (true => força OFF no boot)
bool  bombaConhecida = false;
bool  releOk = false;
int   falhasRele = 0, falhasSensor = 0;
float distancia = -1;
int   nivel = -1;              // % ou -1 se inválido
unsigned long inicioBomba = 0;

// ---------- Sensor ----------
float lerDistancia() {                     // mediana de 7 leituras
  float v[7]; int n = 0;
  for (int i = 0; i < 7; i++) {
    digitalWrite(TRIG, LOW); delayMicroseconds(4);
    digitalWrite(TRIG, HIGH); delayMicroseconds(15);
    digitalWrite(TRIG, LOW);
    long us = pulseIn(ECHO, HIGH, 40000);
    float cm = us * 0.0343 / 2;
    if (cm >= 20 && cm <= 400) v[n++] = cm;
    delay(60);
  }
  if (n < 4) return -1;
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
    if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

// ---------- Relé ----------
bool comandoRele(bool ligar) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = String("http://") + RELE_IP;
  switch (RELE_TIPO) {
    case SHELLY_GEN1: url += String("/relay/0?turn=") + (ligar ? "on" : "off"); break;
    case SHELLY_GEN2: url += String("/rpc/Switch.Set?id=0&on=") + (ligar ? "true" : "false"); break;
    case TASMOTA:     url += String("/cm?cmnd=Power%20") + (ligar ? "On" : "Off"); break;
  }
  HTTPClient http; http.setTimeout(3000); http.begin(url);
  int code = http.GET(); http.end();
  return code == 200;
}

void aplicarRele() {
  if (desejada == bombaReal && bombaConhecida) { releOk = true; return; }
  if (comandoRele(desejada)) {
    bool mudou = !bombaConhecida || bombaReal != desejada;
    bombaReal = desejada; bombaConhecida = true;
    releOk = true; falhasRele = 0;
    setAlarme(ALM_RELE, false);
    if (desejada) inicioBomba = millis();
    if (mudou) logEv(desejada ? "Bomba LIGADA" : "Bomba DESLIGADA");
  } else {
    releOk = false;
    if (++falhasRele >= 3) setAlarme(ALM_RELE, true);
  }
}

// ---------- Controle ----------
void controlar() {
  if (distancia < 0) {
    nivel = -1;
    if (++falhasSensor >= 3) setAlarme(ALM_SENSOR, true);
  } else {
    falhasSensor = 0; setAlarme(ALM_SENSOR, false);
    nivel = constrain((int)((cfg.vazia - distancia) * 100 / (cfg.vazia - cfg.cheia)), 0, 100);
    setAlarme(ALM_CRITICO, nivel <= 10);
  }

  if (bombaReal && bombaConhecida && millis() - inicioBomba > (unsigned long)cfg.tmax * 60000UL)
    setAlarme(ALM_TEMPO, true);

  if (alarmes & (ALM_SENSOR | ALM_TEMPO)) {
    desejada = false;                                  // falha segura
  } else if (nivel >= 0) {
    if (modoAuto && !desejada && nivel <= cfg.liga) desejada = true;
    if (desejada && nivel >= cfg.desliga) desejada = false;   // vale também no manual
  }
  aplicarRele();
}

// ---------- API HTTP ----------
void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}
void responder(int code, const String& body) { cors(); server.send(code, "application/json", body); }
void erro(int code, const char* msg) { responder(code, String("{\"erro\":\"") + msg + "\"}"); }

String cfgJson() {
  return String("{\"vazia\":") + cfg.vazia + ",\"cheia\":" + cfg.cheia + ",\"liga\":" + cfg.liga +
         ",\"desliga\":" + cfg.desliga + ",\"tmax\":" + cfg.tmax + "}";
}

void handleStatus() {
  String j = "{";
  j += "\"nivel\":" + String(nivel) + ",\"distancia\":" + String(distancia, 1);
  j += String(",\"bomba\":") + (bombaConhecida ? (bombaReal ? "true" : "false") : "null");
  j += String(",\"modo\":\"") + (modoAuto ? "auto" : "manual") + "\"";
  j += String(",\"rele_ok\":") + (releOk ? "true" : "false");
  j += ",\"bomba_seg\":" + String(bombaReal && bombaConhecida ? (millis() - inicioBomba) / 1000 : 0);
  j += ",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptime\":" + String(millis() / 1000);
  j += ",\"agora\":" + String((uint32_t)(time(nullptr) > 1700000000 ? time(nullptr) : 0));
  j += ",\"alarmes\":[";
  bool first = true;
  for (int i = 0; i < 4; i++) if (alarmes & (1 << i)) { if (!first) j += ","; j += String("\"") + ALM_ID[i] + "\""; first = false; }
  j += "],\"cfg\":" + cfgJson() + ",\"eventos\":[";
  int n = evTotal < 20 ? evTotal : 20;
  for (int k = 0; k < n; k++) {
    Evento& e = ev[(evTotal - 1 - k) % 20];
    if (k) j += ",";
    j += "{\"t\":" + String(e.t) + ",\"u\":" + String(e.u) + ",\"m\":\"" + e.m + "\"}";
  }
  j += "]}";
  responder(200, j);
}

void handleConfigGet() { responder(200, cfgJson()); }

void handleConfigSet() {
  Config n = cfg;
  if (server.hasArg("liga"))    n.liga = server.arg("liga").toInt();
  if (server.hasArg("desliga")) n.desliga = server.arg("desliga").toInt();
  if (server.hasArg("vazia"))   n.vazia = server.arg("vazia").toFloat();
  if (server.hasArg("cheia"))   n.cheia = server.arg("cheia").toFloat();
  if (server.hasArg("tmax"))    n.tmax = server.arg("tmax").toInt();
  if (n.liga < 1 || n.desliga > 100 || n.desliga - n.liga < 5) return erro(400, "liga/desliga invalidos (min. 5 pontos de histerese)");
  if (n.cheia < 25 || n.vazia < n.cheia + 10)                  return erro(400, "calibracao invalida (cheia>=25 e vazia>cheia+10)");
  if (n.tmax < 1 || n.tmax > 240)                              return erro(400, "tempo maximo invalido (1-240 min)");
  cfg = n;
  prefs.putFloat("vazia", cfg.vazia); prefs.putFloat("cheia", cfg.cheia);
  prefs.putInt("liga", cfg.liga); prefs.putInt("desliga", cfg.desliga); prefs.putInt("tmax", cfg.tmax);
  logEv("Configuracao alterada pela IHM");
  responder(200, cfgJson());
}

void handleMode() {
  String m = server.arg("m");
  if (m != "auto" && m != "manual") return erro(400, "m deve ser auto ou manual");
  modoAuto = (m == "auto");
  logEv(modoAuto ? "Modo AUTOMATICO" : "Modo MANUAL");
  responder(200, "{\"ok\":true}");
}

void handlePump() {
  if (modoAuto) return erro(409, "mude para o modo manual");
  String c = server.arg("cmd");
  if (c == "on") {
    if (alarmes & (ALM_SENSOR | ALM_TEMPO)) return erro(409, "bloqueado por alarme ativo");
    if (nivel < 0 || nivel >= cfg.desliga)  return erro(409, "nivel ja esta alto");
    desejada = true;
  } else if (c == "off") {
    desejada = false;
  } else return erro(400, "cmd deve ser on ou off");
  aplicarRele();
  responder(200, "{\"ok\":true}");
}

void handleAck() {
  if (alarmes & ALM_TEMPO) { alarmes &= ~ALM_TEMPO; logEv("Alarme de tempo reconhecido"); }
  responder(200, "{\"ok\":true}");
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) { cors(); server.send(204); return; }
  erro(404, "nao encontrado");
}

// ---------- TFT local ----------
#if USAR_TFT_LOCAL
void desenhar() {
  static int nAnt = -9, bAnt = -9; static uint8_t aAnt = 255;
  int b = bombaConhecida ? bombaReal : -1;
  if (nivel == nAnt && b == bAnt && alarmes == aAnt) return;
  nAnt = nivel; bAnt = b; aAnt = alarmes;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2);
  tft.drawString("CAIXA D'AGUA", 60, 8);
  tft.drawRect(40, 40, 120, 160, TFT_WHITE);
  if (nivel >= 0) {
    int h = 156 * nivel / 100;
    uint16_t cor = nivel < cfg.liga ? TFT_RED : nivel < 50 ? TFT_ORANGE : TFT_CYAN;
    tft.fillRect(42, 198 - h, 116, h, cor);
    tft.setTextSize(4); tft.drawString(String(nivel) + "%", 190, 90);
  }
  tft.setTextSize(2);
  tft.setTextColor(b == 1 ? TFT_GREEN : TFT_LIGHTGREY);
  tft.drawString(b == 1 ? "BOMBA: LIGADA" : "BOMBA: DESLIGADA", 20, 212);
  if (alarmes) { tft.setTextColor(TFT_RED); tft.drawString("ALARME", 190, 150); }
}
#else
void desenhar() {}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  prefs.begin("caixa", false);
  cfg.vazia = prefs.getFloat("vazia", cfg.vazia);   cfg.cheia = prefs.getFloat("cheia", cfg.cheia);
  cfg.liga = prefs.getInt("liga", cfg.liga);        cfg.desliga = prefs.getInt("desliga", cfg.desliga);
  cfg.tmax = prefs.getInt("tmax", cfg.tmax);
#if USAR_TFT_LOCAL
  tft.init(); tft.setRotation(1);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(-3 * 3600, 0, "pool.ntp.org");         // horário de Brasília
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigSet);
  server.on("/api/mode", HTTP_POST, handleMode);
  server.on("/api/pump", HTTP_POST, handlePump);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.onNotFound(handleNotFound);
  server.begin();
  logEv("Sistema iniciado");
}

void loop() {
  static unsigned long tCiclo = 0, tWifi = 0;
  server.handleClient();
  if (millis() - tWifi > 10000) { tWifi = millis(); if (WiFi.status() != WL_CONNECTED) WiFi.reconnect(); }
  if (millis() - tCiclo >= 3000) {
    tCiclo = millis();
    distancia = lerDistancia();
    controlar();
    desenhar();
  }
  delay(2);
}
