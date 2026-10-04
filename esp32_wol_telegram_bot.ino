// Ative o debug da biblioteca. No topo do sketch, antes do #include <UniversalTelegramBot.h>:
#define TELEGRAM_DEBUG 1
#include <WiFi.h>
#include <WiFiManager.h> // Substitui o WiFiMulti
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <WakeOnLan.h> 
#include <UniversalTelegramBot.h> 
#include <ArduinoJson.h>
#include <ESPping.h> 

// Telegram Bot Token e ID de Segurança
#define BOT_TOKEN "0000000000:ABCdefghjkliop1234567890RSTUvwxyzab"
#define ALLOWED_ID "0000000000"

// Configurações do PC Alvo
#define MAC_ADDR "00:00:00:00:00:00"
#define PC_IP "10.10.0.2" // IP fixo do seu Servidor na rede local para o PING

// Pino do ESP32 ligado ao Transístor/Relé/Optoacoplador para o WoL Hard
#define HARD_WOL_PIN 4 

const unsigned long BOT_MTBS = 5000;  // Tempo de checagem do Telegram (5 seg)
const unsigned long RESTRART_MTBS = 1000 * 3600 * 4; // Reinicia a cada 4 horas

WiFiClientSecure secured_client;
WiFiUDP UDP;
WakeOnLan WOL(UDP);

UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;

// --- FUNÇÕES DE AÇÃO ---

void sendWolSoft()
{
  Serial.println("Enviando Magic Packet...");
  WOL.sendMagicPacket(MAC_ADDR);
  delay(300);
}

void sendWolHard()
{
  Serial.println("Acionando pino de energia (Hard WoL)...");
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

    if (from_id != ALLOWED_ID) 
    {
      bot.sendMessage(chat_id, "Acesso Negado.", "");
      continue;
    }

    if (bot.messages[i].type == "callback_query")
    {
      bot.answerCallbackQuery(bot.messages[i].query_id);
    }

    if (from_name == "") from_name = "Visitante";

    if (text == "/start")
    {
      String welcome = "Olá " + from_name + "! Painel de Controle do PC.\nEscolha uma opção abaixo:";
      String inlineJson = "[[{\"text\":\"🪄 Ligar (Soft WoL)\",\"callback_data\":\"/wol_soft\"},"
                          "{\"text\":\"⚡ Ligar (Hard WoL)\",\"callback_data\":\"/wol_hard\"}],"
                          "[{\"text\":\"🔍 Status do PC\",\"callback_data\":\"/ping\"}]]";
      bot.sendMessageWithInlineKeyboard(chat_id, welcome, "", inlineJson);
    }
    else if (text == "/wol_soft")
    {
      sendWolSoft();
      bot.sendMessage(chat_id, "🪄 Magic Packet (Soft WoL) enviado para a rede!", "");
    }
    else if (text == "/wol_hard")
    {
      sendWolHard();
      bot.sendMessage(chat_id, "⚡ Placa-mãe acionada fisicamente (Hard WoL)!", "");
    }
    else if (text == "/ping")
    {
      bot.sendMessage(chat_id, "🔍 Checando status do PC, aguarde...", "");
      
      bool isOnline = Ping.ping(PC_IP, 3); 
      
      if (isOnline) {
        bot.sendMessage(chat_id, "✅ **O PC está LIGADO (ONLINE)!**", "Markdown");
      } else {
        bot.sendMessage(chat_id, "❌ **O PC está DESLIGADO (OFFLINE).**", "Markdown");
      }
    }
    else
    {
      bot.sendMessage(chat_id, "Comando não reconhecido. Use o menu ou digite /start", "");
    }
  }
}

// --- SETUP E LOOP PADRÃO ---

void setup()
{
  Serial.begin(9600);
  
  pinMode(HARD_WOL_PIN, OUTPUT);
  digitalWrite(HARD_WOL_PIN, LOW); 

  // --- INÍCIO DO WIFIMANAGER ---
  WiFiManager wm;
  
  Serial.println("Conectando ao WiFi via WiFiManager...");
  
  // autoConnect tenta conectar nas redes salvas. 
  // Se falhar, cria o Access Point com o nome "ESP32_WoL_Bot"
  bool res = wm.autoConnect("ESP32_WoL_Bot");

  if(!res) {
    Serial.println("Falha ao conectar nas redes salvas e timeout atingido. Reiniciando...");
    ESP.restart();
  } 
  
  Serial.println("WiFi Conectado com sucesso!");
  Serial.print("IP recebido: ");
  Serial.println(WiFi.localIP());
  // --- FIM DO WIFIMANAGER ---

  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

  Serial.print("Sincronizando relógio via NTP...");
  configTime(0, 0, "pool.ntp.org");
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    Serial.print(".");
    delay(150);
    now = time(nullptr);
  }
  Serial.println(" OK");
  
  bot.sendMessage(ALLOWED_ID, "🤖 Sistema ESP32 reiniciado e online! Digite /start para o menu.", "");
}

void loop()
{
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
    Serial.println("Reiniciando preventivamente...");
    ESP.restart();
  }
  delay(100);
}
