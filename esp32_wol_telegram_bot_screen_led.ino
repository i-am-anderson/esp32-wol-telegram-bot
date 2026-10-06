// Debug da biblioteca do Telegram (descomente se precisar diagnosticar).
// Deve ficar ANTES do include da UniversalTelegramBot.
// #define TELEGRAM_DEBUG 1

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <WakeOnLan.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <ESPping.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ===== PREENCHA COM SEUS DADOS =====
#define BOT_TOKEN "0000000000:ABCdefghjkliop1234567890RSTUvwxyzab"
#define ALLOWED_ID "0000000000"

#define MAC_ADDR "00:00:00:00:00:00"
#define PC_IP "10.10.0.2"
// ===================================

// Pino do ESP32 ligado ao Transistor/Rele/Optoacoplador para o WoL Hard
#define HARD_WOL_PIN 4

// --- OLED (I2C: SDA = GPIO 21, SCL = GPIO 22) ---
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C   // se nao funcionar, tente 0x3D
#define OLED_SDA 21
#define OLED_SCL 22

// --- LED WS2812 (DI no GPIO 14) ---
#define LED_PIN 14
#define LED_COUNT 1
#define LED_BRIGHTNESS 40   // 0-255 (40 ja e bem visivel e poupa corrente)

const unsigned long BOT_MTBS = 5000;                   // Checagem do Telegram (5 seg)
const unsigned long RESTRART_MTBS = 1000UL * 3600 * 4; // Reinicia a cada 4 horas

WiFiClientSecure secured_client;
WiFiUDP UDP;
WakeOnLan WOL(UDP);

UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;

// =====================================================
//  LED WS2812
// =====================================================
//  Cores e significados:
//   AZUL      fixo  -> iniciando
//   AMARELO   fixo  -> conectando ao Wi-Fi
//   ROXO      fixo  -> portal de configuracao Wi-Fi aberto (AP)
//   CIANO     fixo  -> sincronizando NTP / checando ping
//   VERDE     respirando -> online e aguardando comandos
//   VERMELHO  respirando -> Wi-Fi desconectado
//   BRANCO    piscada curta -> mensagem recebida
//   AZUL      3 piscadas + 2s fixo -> Soft WoL enviado
//   LARANJA   3 piscadas + 2s fixo -> Hard WoL acionado
//   VERDE     fixo 5s -> PC ONLINE (resultado do ping)
//   VERMELHO  fixo 5s -> PC OFFLINE (resultado do ping)
//   VERMELHO  3 piscadas -> acesso negado
//   AMARELO   2 piscadas -> comando nao reconhecido
// =====================================================

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

const uint32_t C_RED    = Adafruit_NeoPixel::Color(255, 0, 0);
const uint32_t C_GREEN  = Adafruit_NeoPixel::Color(0, 255, 0);
const uint32_t C_BLUE   = Adafruit_NeoPixel::Color(0, 0, 255);
const uint32_t C_YELLOW = Adafruit_NeoPixel::Color(255, 150, 0);
const uint32_t C_ORANGE = Adafruit_NeoPixel::Color(255, 50, 0);
const uint32_t C_CYAN   = Adafruit_NeoPixel::Color(0, 255, 255);
const uint32_t C_PURPLE = Adafruit_NeoPixel::Color(150, 0, 255);
const uint32_t C_WHITE  = Adafruit_NeoPixel::Color(255, 255, 255);
const uint32_t C_OFF    = 0;

uint32_t holdColor = 0;
unsigned long holdUntil = 0;

void ledShow(uint32_t color)
{
  for (int i = 0; i < LED_COUNT; i++) led.setPixelColor(i, color);
  led.show();
}

// Pisca de forma bloqueante (usar so para eventos curtos)
void ledFlash(uint32_t color, int times, int onMs = 120, int offMs = 120)
{
  for (int i = 0; i < times; i++)
  {
    ledShow(color);
    delay(onMs);
    ledShow(C_OFF);
    if (i < times - 1) delay(offMs);
  }
}

// Mantem uma cor fixa por 'ms' milissegundos (nao bloqueante)
void ledHold(uint32_t color, unsigned long ms)
{
  holdColor = color;
  holdUntil = millis() + ms;
  ledShow(color);
}

uint32_t ledScale(uint32_t c, uint8_t k)
{
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >> 8) & 0xFF;
  uint8_t b = c & 0xFF;
  return Adafruit_NeoPixel::Color((r * k) / 255, (g * k) / 255, (b * k) / 255);
}

// Chamar a cada volta do loop: mantem a cor de "hold" ou o efeito de respiracao
void ledUpdate()
{
  if (holdUntil != 0)
  {
    if ((long)(millis() - holdUntil) < 0) return; // ainda segurando a cor
    holdUntil = 0;
  }

  uint32_t base = (WiFi.status() == WL_CONNECTED) ? C_GREEN : C_RED;

  // Respiracao suave de 4 segundos
  float phase = (millis() % 4000) / 4000.0f;
  float level = (sinf(2.0f * PI * phase - PI / 2.0f) + 1.0f) / 2.0f; // 0..1
  uint8_t k = 10 + (uint8_t)(245 * level);
  ledShow(ledScale(base, k));
}

// =====================================================
//  DISPLAY: CABECALHO FIXO + LOG COM ROLAGEM
// =====================================================

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledOk = false;

const int MAX_LINES = 7;   // 7 linhas de log + 1 de cabecalho (use 3 se o display for 128x32)
const int MAX_COLS = 21;
String logLines[MAX_LINES];
String oledHeader = "IP: ---";

// Remove acentos/emojis (a fonte padrao so tem ASCII)
String toAscii(const String &s)
{
  String out;
  for (unsigned int i = 0; i < s.length(); i++)
  {
    if ((uint8_t)s[i] < 128) out += s[i];
  }
  out.trim();
  return out;
}

void oledRedraw()
{
  if (!oledOk) return;
  display.clearDisplay();

  // Cabecalho: faixa branca com texto preto
  display.fillRect(0, 0, SCREEN_W, 8, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(1, 0);
  display.print(oledHeader);

  // Volta ao texto branco para o log
  display.setTextColor(SSD1306_WHITE);

  for (int i = 0; i < MAX_LINES; i++)
  {
    display.setCursor(0, 8 + i * 8);
    display.println(logLines[i]);
  }
  display.display();
}

void oledLog(String msg)
{
  msg = toAscii(msg);
  Serial.println(msg);

  // quebra mensagens longas em varias linhas
  while (msg.length() > 0)
  {
    String part = msg.substring(0, MAX_COLS);
    msg = msg.substring(part.length());
    for (int i = 0; i < MAX_LINES - 1; i++) logLines[i] = logLines[i + 1];
    logLines[MAX_LINES - 1] = part;
  }

  oledRedraw();
}

// Envia no Telegram (com emojis/acentos) e registra no display (texto simples)
void reply(const String &chat_id, const String &tgMsg, const String &oledMsg, const String &parse = "")
{
  bot.sendMessage(chat_id, tgMsg, parse);
  oledLog("TX: " + oledMsg);
}

// --- FUNCOES DE ACAO ---

void sendWolSoft()
{
  oledLog("Enviando Magic Packet");
  ledShow(C_BLUE);
  WOL.sendMagicPacket(MAC_ADDR);
  delay(300);
  ledFlash(C_BLUE, 3);
  ledHold(C_BLUE, 2000);
}

void sendWolHard()
{
  oledLog("Acionando Hard WoL");
  ledShow(C_ORANGE);
  digitalWrite(HARD_WOL_PIN, HIGH);
  delay(500);
  digitalWrite(HARD_WOL_PIN, LOW);
  ledFlash(C_ORANGE, 3);
  ledHold(C_ORANGE, 2000);
}

// --- TRATAMENTO DE MENSAGENS DO TELEGRAM ---

void handleNewMessages(int numNewMessages)
{
  Serial.print("Novas mensagens: ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; ++i)
  {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_id = bot.messages[i].from_id;
    String from_name = bot.messages[i].from_name;

    // Ignora estranhos em silencio (so registra no display)
    if (from_id != ALLOWED_ID)
    {
      oledLog("NEGADO: " + from_id);
      ledFlash(C_RED, 3);
      continue;
    }

    if (bot.messages[i].type == "callback_query")
    {
      bot.answerCallbackQuery(bot.messages[i].query_id);
    }

    if (from_name == "") from_name = "Visitante";

    oledLog("RX: " + text);
    ledFlash(C_WHITE, 1, 80, 0);

    if (text == "/start")
    {
      String welcome = "Olá " + from_name + "! Painel de Controle do PC.\nEscolha uma opção abaixo:";
      String inlineJson = "[[{\"text\":\"🪄 Ligar (Soft WoL)\",\"callback_data\":\"/wol_soft\"},"
                          "{\"text\":\"⚡ Ligar (Hard WoL)\",\"callback_data\":\"/wol_hard\"}],"
                          "[{\"text\":\"🔍 Status do PC\",\"callback_data\":\"/ping\"}]]";
      bot.sendMessageWithInlineKeyboard(chat_id, welcome, "", inlineJson);
      oledLog("TX: menu enviado");
    }
    else if (text == "/wol_soft")
    {
      sendWolSoft();
      reply(chat_id, "🪄 Magic Packet (Soft WoL) enviado para a rede!", "Soft WoL enviado");
    }
    else if (text == "/wol_hard")
    {
      sendWolHard();
      reply(chat_id, "⚡ Placa-mãe acionada fisicamente (Hard WoL)!", "Hard WoL acionado");
    }
    else if (text == "/ping")
    {
      reply(chat_id, "🔍 Checando status do PC, aguarde...", "Checando PC...");
      ledShow(C_CYAN); // fica ciano enquanto o ping bloqueia

      bool isOnline = Ping.ping(PC_IP, 3);

      if (isOnline)
      {
        ledHold(C_GREEN, 5000);
        reply(chat_id, "✅ *O PC está LIGADO (ONLINE)!*", "PC ONLINE", "Markdown");
      }
      else
      {
        ledHold(C_RED, 5000);
        reply(chat_id, "❌ *O PC está DESLIGADO (OFFLINE).*", "PC OFFLINE", "Markdown");
      }
    }
    else
    {
      ledFlash(C_YELLOW, 2);
      reply(chat_id, "Comando não reconhecido. Use o menu ou digite /start", "Comando invalido");
    }
  }
}

// --- SETUP E LOOP ---

void setup()
{
  Serial.begin(115200);

  pinMode(HARD_WOL_PIN, OUTPUT);
  digitalWrite(HARD_WOL_PIN, LOW);

  // --- LED ---
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  ledShow(C_BLUE); // iniciando

  // --- OLED ---
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(OLED_ADDR);
  bool oledPresent = (Wire.endTransmission() == 0);

  if (oledPresent && display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    oledOk = true;
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.display();
  }
  else
  {
    Serial.println("OLED nao encontrado, seguindo sem display");
  }
  oledLog("Iniciando...");

  // --- WIFIMANAGER ---
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // 3 min; depois reinicia e tenta de novo

  // Mostra no display e no LED quando o portal de configuracao abrir
  wm.setAPCallback([](WiFiManager *w) {
    ledShow(C_PURPLE);
    oledLog("Config WiFi:");
    oledLog("AP ESP32_WoL_Bot");
    oledLog("IP 192.168.4.1");
  });

  oledLog("Conectando WiFi...");
  ledShow(C_YELLOW);

  // Se nao conectar, cria o Access Point "ESP32_WoL_Bot"
  bool res = wm.autoConnect("ESP32_WoL_Bot");

  if (!res)
  {
    oledLog("WiFi falhou. Reiniciando");
    ledFlash(C_RED, 5);
    delay(500);
    ESP.restart();
  }

  oledLog("WiFi OK");
  oledHeader = "IP:" + WiFi.localIP().toString();
  oledRedraw();

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

  oledLog("Sincronizando NTP...");
  ledShow(C_CYAN);
  configTime(0, 0, "pool.ntp.org");
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    delay(150);
    now = time(nullptr);
  }
  oledLog("NTP OK");

  bot.sendMessage(ALLOWED_ID, "🤖 Sistema ESP32 reiniciado e online! Digite /start para o menu.", "");
  oledLog("Bot online");
  ledFlash(C_GREEN, 2, 200, 150); // pronto!
}

void loop()
{
  // Atualiza o cabecalho se o estado do Wi-Fi mudar
  static wl_status_t lastStatus = WL_CONNECTED;
  if (WiFi.status() != lastStatus)
  {
    lastStatus = WiFi.status();
    oledHeader = (lastStatus == WL_CONNECTED) ? "IP:" + WiFi.localIP().toString()
                                              : "WiFi desconectado";
    oledRedraw();
  }

  if (millis() - bot_lasttime > BOT_MTBS)
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    bot_lasttime = millis();
  }

  if (millis() > RESTRART_MTBS)
  {
    oledLog("Reinicio preventivo");
    ledFlash(C_PURPLE, 2);
    delay(500);
    ESP.restart();
  }

  ledUpdate();
  delay(20); // curto para a respiracao do LED ficar suave
}
