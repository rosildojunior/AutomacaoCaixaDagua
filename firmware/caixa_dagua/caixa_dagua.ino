#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>

// ---------- CONFIGURAR ----------
const char* WIFI_SSID = "SUA_REDE";
const char* WIFI_PASS = "SUA_SENHA";
// Shelly Gen1: "/relay/0?turn=" | Tasmota: "/cm?cmnd=Power%20"  (on/off)
const char* RELE_IP   = "192.168.0.50";
const bool  TASMOTA   = false;

const float DIST_VAZIA_CM = 120;  // sensor até o fundo
const float DIST_CHEIA_CM = 30;   // sensor até a água com caixa cheia (>=25)
const int   NIVEL_LIGA    = 25;   // %
const int   NIVEL_DESLIGA = 98;   // %
const unsigned long TEMPO_MAX_MS = 60UL * 60 * 1000;
// --------------------------------

#define TRIG 5
#define ECHO 18
TFT_eSPI tft;

bool bombaLigada = false;
unsigned long inicioBomba = 0;
int falhas = 0, nivelAnt = -2;

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

bool comandoRele(bool ligar) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = String("http://") + RELE_IP +
      (TASMOTA ? "/cm?cmnd=Power%20" : "/relay/0?turn=") + (ligar ? "on" : "off");
  HTTPClient http; http.setTimeout(3000); http.begin(url);
  int code = http.GET(); http.end();
  return code == 200;
}

void desenhar(int nivel, const char* msg) {
  if (nivel == nivelAnt && !msg) return;
  nivelAnt = nivel;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2);
  tft.drawString("CAIXA D'AGUA", 60, 8);
  tft.drawRect(40, 40, 120, 160, TFT_WHITE);              // tanque
  if (nivel >= 0) {
    int h = 156 * nivel / 100;
    uint16_t cor = nivel < NIVEL_LIGA ? TFT_RED : nivel < 50 ? TFT_ORANGE : TFT_CYAN;
    tft.fillRect(42, 198 - h, 116, h, cor);
    tft.setTextSize(4); tft.drawString(String(nivel) + "%", 190, 90);
  }
  tft.setTextSize(2);
  tft.setTextColor(bombaLigada ? TFT_GREEN : TFT_LIGHTGREY);
  tft.drawString(bombaLigada ? "BOMBA: LIGADA" : "BOMBA: DESLIGADA", 20, 212);
  if (msg) { tft.setTextColor(TFT_RED); tft.drawString(msg, 190, 150); }
}

void setup() {
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);
  tft.init(); tft.setRotation(1);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  desenhar(-1, "WiFi...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();

  float d = lerDistancia();
  if (d < 0) {                                  // sensor falhou
    if (++falhas >= 3) {
      if (bombaLigada && comandoRele(false)) bombaLigada = false;
      nivelAnt = -2; desenhar(-1, "ERRO");
    }
  } else {
    falhas = 0;
    int nivel = constrain((int)((DIST_VAZIA_CM - d) * 100 /
                                (DIST_VAZIA_CM - DIST_CHEIA_CM)), 0, 100);

    bool deveLigar = bombaLigada;
    if (!bombaLigada && nivel <= NIVEL_LIGA)   deveLigar = true;
    if (bombaLigada && nivel >= NIVEL_DESLIGA) deveLigar = false;
    if (bombaLigada && millis() - inicioBomba > TEMPO_MAX_MS) deveLigar = false;

    if (deveLigar != bombaLigada && comandoRele(deveLigar)) {
      bombaLigada = deveLigar;
      if (deveLigar) inicioBomba = millis();
      nivelAnt = -2;                            // força redesenho
    }
    desenhar(nivel, nullptr);
  }
  delay(3000);
}
