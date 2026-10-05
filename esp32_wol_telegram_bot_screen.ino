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

const unsigned long BOT_MTBS = 5000;                   // Checagem do Telegram (5 seg)
const unsigned long RESTRART_MTBS = 1000UL * 3600 * 4; // Reinicia a cada 4 horas

WiFiClientSecure secured_client;
WiFiUDP UDP;
WakeOnLan WOL(UDP);

UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;

// --- DISPLAY: CABECALHO FIXO + LOG COM ROLAGEM ---

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
  WOL.sendMagicPacket(MAC_ADDR);
  delay(300);
}

void sendWolHard()
{
  oledLog("Acionando Hard WoL");
  digitalWrite(HARD_WOL_PIN, HIGH);
  delay(500);
  digitalWrite(HARD_WOL_PIN, LOW);
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
      continue;
    }

    if (bot.messages[i].type == "callback_query")
    {
      bot.answerCallbackQuery(bot.messages[i].query_id);
    }

    if (from_name == "") from_name = "Visitante";

    oledLog("RX: " + text);

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

      bool isOnline = Ping.ping(PC_IP, 3);

      if (isOnline)
      {
        reply(chat_id, "✅ *O PC está LIGADO (ONLINE)!*", "PC ONLINE", "Markdown");
      }
      else
      {
        reply(chat_id, "❌ *O PC está DESLIGADO (OFFLINE).*", "PC OFFLINE", "Markdown");
      }
    }
    else
    {
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

  // Mostra no display quando o portal de configuracao abrir
  wm.setAPCallback([](WiFiManager *w) {
    oledLog("Config WiFi:");
    oledLog("AP ESP32_WoL_Bot");
    oledLog("IP 192.168.4.1");
  });

  oledLog("Conectando WiFi...");

  // Se nao conectar, cria o Access Point "ESP32_WoL_Bot"
  bool res = wm.autoConnect("ESP32_WoL_Bot");

  if (!res)
  {
    oledLog("WiFi falhou. Reiniciando");
    delay(1000);
    ESP.restart();
  }

  oledLog("WiFi OK");
  oledHeader = "IP:" + WiFi.localIP().toString();
  oledRedraw();

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

  oledLog("Sincronizando NTP...");
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
    delay(500);
    ESP.restart();
  }
  delay(100);
}
