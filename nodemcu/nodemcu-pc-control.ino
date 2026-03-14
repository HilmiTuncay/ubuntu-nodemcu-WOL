/*
 * NodeMCU PC Control v2.0
 * https://github.com/HilmiTuncay/ubuntu-nodemcu-WOL
 *
 * Özellikler:
 * - Telegram bot ile uzaktan PC kontrolü
 * - WOL (Wake-on-LAN) ile PC açma
 * - Ubuntu/Windows geçişi
 * - Ubuntu kapatma
 * - Aktivite kontrolü ve "açık unuttun" bildirimi
 * - OTA güncelleme desteği
 *
 * Telegram Komutları:
 * /ac         - Ubuntu aç (WOL)
 * /windowsac  - Windows aç
 * /kapat      - Ubuntu kapat
 * /durum      - PC durumunu kontrol et
 * /ota        - OTA güncelleme URL'si
 * /start      - Komut listesi
 *
 * OTA Güncelleme: http://192.168.1.199/update (admin/1234)
 */

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

// =====================================================
// ==================== AYARLAR ========================
// =====================================================

// WiFi Ayarları - KENDİ BİLGİLERİNİ GİR
const char* ssid = "WIFI_SSID";           // WiFi adı
const char* password = "WIFI_PASSWORD";    // WiFi şifresi

// Telegram Bot Ayarları - @BotFather'dan al
#define BOT_TOKEN "YOUR_BOT_TOKEN"         // Bot token
#define CHAT_ID "YOUR_CHAT_ID"             // Chat ID (@userinfobot'dan al)

// PC MAC Adresi (WOL için) - PC'nin MAC adresini gir
// Örnek: 08:BF:B8:1B:3F:99 → {0x08, 0xBF, 0xB8, 0x1B, 0x3F, 0x99}
byte target_mac[] = {0x08, 0xBF, 0xB8, 0x1B, 0x3F, 0x99};

// IP Adresleri - Kendi ağına göre ayarla
IPAddress ipWindows(192, 168, 1, 233);     // Windows IP
IPAddress ipUbuntu(192, 168, 1, 234);      // Ubuntu IP

// NodeMCU Sabit IP
IPAddress local_IP(192, 168, 1, 199);      // NodeMCU IP
IPAddress gateway(192, 168, 1, 1);         // Router IP
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

// OTA Ayarları
const char* ota_username = "admin";
const char* ota_password = "1234";

// Aktivite Kontrol Ayarları
const unsigned long IDLE_THRESHOLD_MINUTES = 30;      // 30 dk boşta = uyarı
const unsigned long REMINDER_INTERVAL_MS = 900000;    // 15 dk'da bir tekrar uyar
const unsigned long ACTIVITY_CHECK_INTERVAL = 300000; // 5 dk'da bir kontrol

// =====================================================
// ================ GLOBAL DEĞİŞKENLER =================
// =====================================================

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);
WiFiUDP UDP;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

unsigned long lastTimeBotRan = 0;
unsigned long lastActivityCheck = 0;
unsigned long lastIdleWarning = 0;
bool pcWasOn = false;

// Windows boot için
bool waitingForWindowsBoot = false;
unsigned long windowsBootStartTime = 0;

// =====================================================
// ====================== SETUP ========================
// =====================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== NodeMCU PC Control v2.0 ===");

  // WiFi yapılandırması (sabit IP)
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local_IP, gateway, subnet, dns)) {
    Serial.println("Sabit IP ayarlanamadi!");
  }

  WiFi.begin(ssid, password);
  secureClient.setInsecure();

  Serial.print("WiFi'ye baglaniliyor");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Baglandi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi baglantisi basarisiz!");
    ESP.restart();
  }

  // OTA HTTP Server
  httpUpdater.setup(&httpServer, "/update", ota_username, ota_password);
  httpServer.on("/", handleRoot);
  httpServer.begin();
  Serial.println("OTA Server: http://" + WiFi.localIP().toString() + "/update");

  // Telegram bildirim
  String startMsg = "🤖 NodeMCU v2.0 Online!\n";
  startMsg += "IP: " + WiFi.localIP().toString() + "\n";
  startMsg += "OTA: http://" + WiFi.localIP().toString() + "/update\n\n";
  startMsg += "Komutlar:\n";
  startMsg += "/ac - Ubuntu ac\n";
  startMsg += "/windowsac - Windows ac\n";
  startMsg += "/kapat - Ubuntu kapat\n";
  startMsg += "/durum - Durum kontrol";
  bot.sendMessage(CHAT_ID, startMsg, "");
}

// =====================================================
// ======================= LOOP ========================
// =====================================================

void loop() {
  // OTA sunucusu
  httpServer.handleClient();

  // Telegram bot kontrolü (her 1 saniye)
  if (millis() - lastTimeBotRan > 1000) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  // Windows boot sürecini kontrol et
  checkWindowsBootProcess();

  // Aktivite kontrolü (her 5 dakika)
  if (millis() - lastActivityCheck > ACTIVITY_CHECK_INTERVAL) {
    checkActivityAndNotify();
    lastActivityCheck = millis();
  }
}

// =====================================================
// =================== WEB SAYFASI =====================
// =====================================================

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>NodeMCU PC Control</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;padding:20px;background:#1a1a2e;color:#eee;margin:0;}";
  html += "h1{color:#e94560;margin-bottom:20px;}";
  html += ".info{background:#16213e;padding:15px;border-radius:8px;margin:10px 0;}";
  html += "a{color:#0f3460;background:#e94560;padding:10px 20px;border-radius:5px;text-decoration:none;display:inline-block;margin-top:15px;}";
  html += "a:hover{background:#ff6b6b;}";
  html += "</style></head><body>";
  html += "<h1>NodeMCU PC Control v2.0</h1>";
  html += "<div class='info'>";
  html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Uptime:</strong> " + String(millis() / 1000) + " saniye</p>";
  html += "<p><strong>WiFi RSSI:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "</div>";
  html += "<a href='/update'>OTA Guncelleme</a>";
  html += "</body></html>";
  httpServer.send(200, "text/html", html);
}

// =====================================================
// ======================= WOL =========================
// =====================================================

void sendWOL() {
  byte preamble[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  UDP.beginPacket("255.255.255.255", 9);
  UDP.write(preamble, 6);
  for (int i = 0; i < 16; i++) {
    UDP.write(target_mac, 6);
  }
  UDP.endPacket();
}

// =====================================================
// ================== PORT KONTROL =====================
// =====================================================

bool checkPort(IPAddress ip, int port) {
  WiFiClient wifiClient;
  wifiClient.setTimeout(2000);
  if (wifiClient.connect(ip, port)) {
    wifiClient.stop();
    return true;
  }
  return false;
}

// =====================================================
// ================ PC DURUM KONTROL ===================
// =====================================================

String checkPCStatus() {
  if (checkPort(ipWindows, 3389)) {
    return "✅ PC Acik (Windows - RDP)";
  }
  else if (checkPort(ipUbuntu, 22)) {
    return "🐧 PC Acik (Ubuntu - SSH)";
  }
  return "⚫ PC Kapali";
}

bool isPCOn() {
  return checkPort(ipWindows, 3389) || checkPort(ipUbuntu, 22);
}

// =====================================================
// ================= HTTP İSTEKLERİ ====================
// =====================================================

bool sendHttpCommand(String endpoint) {
  WiFiClient httpClient;
  HTTPClient http;

  String url = "http://" + ipUbuntu.toString() + ":8888" + endpoint;
  http.begin(httpClient, url);
  http.setTimeout(5000);
  int httpCode = http.GET();
  http.end();

  return (httpCode == 200);
}

int getIdleTime() {
  WiFiClient httpClient;
  HTTPClient http;

  String url = "http://" + ipUbuntu.toString() + ":8888/idle-time";
  http.begin(httpClient, url);
  http.setTimeout(5000);
  int httpCode = http.GET();

  int idleMinutes = -1;
  if (httpCode == 200) {
    idleMinutes = http.getString().toInt();
  }
  http.end();

  return idleMinutes;
}

// =====================================================
// =============== AKTİVİTE KONTROLÜ ===================
// =====================================================

void checkActivityAndNotify() {
  bool pcOn = isPCOn();

  // PC açıksa idle kontrolü yap
  if (pcOn && checkPort(ipUbuntu, 8888)) {
    int idleMinutes = getIdleTime();
    Serial.print("Idle time: ");
    Serial.print(idleMinutes);
    Serial.println(" dk");

    if (idleMinutes >= (int)IDLE_THRESHOLD_MINUTES) {
      // Uyarı gönderilmeli mi?
      if (millis() - lastIdleWarning > REMINDER_INTERVAL_MS) {
        String msg = "⚠️ PC " + String(idleMinutes) + " dakikadir bosta!\n";
        msg += "Kapatmak icin /kapat yazin.";
        bot.sendMessage(CHAT_ID, msg, "");
        lastIdleWarning = millis();
      }
    } else {
      // Aktif kullanım - uyarı sayacını sıfırla
      lastIdleWarning = 0;
    }
  }

  // PC kapandıysa uyarı sayacını sıfırla
  if (!pcOn && pcWasOn) {
    lastIdleWarning = 0;
  }

  pcWasOn = pcOn;
}

// =====================================================
// =============== TELEGRAM KOMUTLARI ==================
// =====================================================

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    text.toLowerCase();

    // Sadece yetkili kullanıcı
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "❌ Yetkisiz erisim!", "");
      continue;
    }

    Serial.print("Komut: ");
    Serial.println(text);

    // /ac - Ubuntu aç
    if (text == "/ac") {
      sendWOL();
      bot.sendMessage(chat_id, "🚀 WOL gonderildi!\nPC aciliyor (Ubuntu)...", "");
    }

    // /windowsac - Windows aç
    else if (text == "/windowsac") {
      bot.sendMessage(chat_id, "🚀 Windows acilis baslatildi!\n⏳ Ubuntu bekleniyor...", "");
      sendWOL();
      waitingForWindowsBoot = true;
      windowsBootStartTime = millis();
    }

    // /kapat - Ubuntu kapat
    else if (text == "/kapat") {
      if (checkPort(ipUbuntu, 8888)) {
        if (sendHttpCommand("/shutdown")) {
          bot.sendMessage(chat_id, "🔴 Ubuntu kapatiliyor...", "");
          lastIdleWarning = 0;
        } else {
          bot.sendMessage(chat_id, "❌ Kapatma komutu gonderilemedi!", "");
        }
      } else {
        bot.sendMessage(chat_id, "⚠️ Ubuntu calismiyor veya servis hazir degil.", "");
      }
    }

    // /durum - Durum kontrol
    else if (text == "/durum") {
      bot.sendMessage(chat_id, "⏳ Kontrol ediliyor...", "");
      String durum = checkPCStatus();

      // Eğer Ubuntu açıksa idle time da göster
      if (checkPort(ipUbuntu, 8888)) {
        int idle = getIdleTime();
        if (idle >= 0) {
          durum += "\n⏱️ Bosta: " + String(idle) + " dk";
        }
      }

      bot.sendMessage(chat_id, durum, "");
    }

    // /ota - OTA bilgisi
    else if (text == "/ota") {
      String msg = "📡 OTA Guncelleme\n";
      msg += "URL: http://" + WiFi.localIP().toString() + "/update\n";
      msg += "Kullanici: " + String(ota_username) + "\n";
      msg += "Sifre: " + String(ota_password);
      bot.sendMessage(chat_id, msg, "");
    }

    // /start veya /yardim
    else if (text == "/start" || text == "/yardim") {
      String msg = "🤖 NodeMCU PC Control v2.0\n\n";
      msg += "Komutlar:\n";
      msg += "/ac - Ubuntu ac (WOL)\n";
      msg += "/windowsac - Windows ac\n";
      msg += "/kapat - Ubuntu kapat\n";
      msg += "/durum - PC durumu\n";
      msg += "/ota - OTA guncelleme bilgisi\n\n";
      msg += "IP: " + WiFi.localIP().toString();
      bot.sendMessage(chat_id, msg, "");
    }
  }
}

// =====================================================
// ============== WINDOWS BOOT SÜRECİ ==================
// =====================================================

void checkWindowsBootProcess() {
  if (!waitingForWindowsBoot) return;

  unsigned long elapsed = millis() - windowsBootStartTime;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();

    if (checkPort(ipUbuntu, 8888)) {
      // Ubuntu açıldı ve server hazır
      bot.sendMessage(CHAT_ID, "🐧 Ubuntu acildi!\n⏳ Windows'a geciliyor...", "");
      delay(2000);

      if (sendHttpCommand("/reboot-windows")) {
        bot.sendMessage(CHAT_ID, "✅ Windows'a reboot komutu gonderildi!", "");
      } else {
        bot.sendMessage(CHAT_ID, "❌ Reboot komutu gonderilemedi!", "");
      }
      waitingForWindowsBoot = false;
    }
    else if (elapsed > 120000) {
      // 2 dakika timeout
      bot.sendMessage(CHAT_ID, "⚠️ Zaman asimi! Ubuntu acilamadi.", "");
      waitingForWindowsBoot = false;
    }
  }
}
