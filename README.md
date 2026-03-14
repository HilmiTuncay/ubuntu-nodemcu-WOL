# Ubuntu NodeMCU WOL

Telegram üzerinden bilgisayarı uzaktan açma, kapatma ve Ubuntu/Windows arasında geçiş yapma sistemi.

## Özellikler

- **WOL (Wake-on-LAN):** Kapalı bilgisayarı uzaktan aç
- **Dual Boot Geçişi:** Ubuntu↔Windows geçişi Telegram'dan tek komutla
- **Uzaktan Kapatma:** Ubuntu'yu Telegram'dan kapat
- **Boot Bildirimi:** Ubuntu açılınca Telegram'dan bildirim gelir
- **Aktivite Kontrolü:** PC boşta kalınca otomatik bildirim (30 dk)
- **OTA Güncelleme:** NodeMCU'yu kablosuz güncelle
- **Masaüstü Uygulaması:** "Windows'a Geç" butonu

## Gereksinimler

### Donanım
- NodeMCU (ESP8266)
- Dual boot PC (Ubuntu + Windows)
- WOL destekli anakart

### Yazılım
- Ubuntu 20.04+ (PC)
- Arduino IDE veya arduino-cli (NodeMCU programlama için)

## Kurulum

### 1. Telegram Bot Oluştur

1. Telegram'da [@BotFather](https://t.me/BotFather) ile konuş
2. `/newbot` komutu ile yeni bot oluştur
3. Bot token'ını kaydet
4. [@userinfobot](https://t.me/userinfobot) ile Chat ID'ni öğren

### 2. PC Bilgilerini Topla

```bash
# MAC adresi (WOL için)
ip link show | grep ether

# Ubuntu IP adresi
hostname -I
```

### 3. Ubuntu Kurulumu

```bash
# Repo'yu klonla
git clone https://github.com/HilmiTuncay/ubuntu-nodemcu-WOL.git
cd ubuntu-nodemcu-WOL

# Kurulumu çalıştır
sudo bash setup.sh
```

Bu script şunları yapar:
- GRUB'ı yapılandırır (1. Ubuntu, 2. Windows)
- HTTP server'ı kurar (port 8888)
- Systemd servisini etkinleştirir
- Masaüstüne "Windows'a Geç" kısayolu ekler

### 4. NodeMCU Kurulumu

#### Arduino CLI ile (Önerilen)

```bash
# Arduino CLI kur
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# ESP8266 board ekle
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266

# Kütüphaneleri kur
arduino-cli lib install "UniversalTelegramBot" "ArduinoJson"

# Kodu düzenle - WiFi ve Telegram bilgilerini gir
nano nodemcu/nodemcu-pc-control.ino

# Derle ve yükle
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 nodemcu/nodemcu-pc-control.ino
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 nodemcu/nodemcu-pc-control.ino
```

#### Arduino IDE ile

1. Arduino IDE'yi aç
2. File → Preferences → Additional Boards Manager URLs:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Tools → Board → Boards Manager → "esp8266" ara ve yükle
4. Sketch → Include Library → Manage Libraries:
   - `UniversalTelegramBot` yükle
   - `ArduinoJson` yükle
5. `nodemcu/nodemcu-pc-control.ino` dosyasını aç
6. WiFi ve Telegram bilgilerini düzenle
7. Tools → Board → NodeMCU 1.0
8. Upload

### 5. NodeMCU Yapılandırması

`nodemcu/nodemcu-pc-control.ino` dosyasında şunları düzenle:

```cpp
// WiFi
const char* ssid = "WIFI_ADI";
const char* password = "WIFI_SIFRESI";

// Telegram
#define BOT_TOKEN "BOT_TOKEN_BURAYA"
#define CHAT_ID "CHAT_ID_BURAYA"

// PC MAC adresi (ip link show | grep ether)
byte target_mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// IP adresleri
IPAddress ipWindows(192, 168, 1, 233);
IPAddress ipUbuntu(192, 168, 1, 234);
IPAddress local_IP(192, 168, 1, 199);  // NodeMCU IP
```

## Kullanım

### Telegram Komutları

| Komut | Açıklama |
|-------|----------|
| `/ac` | Ubuntu'yu aç (WOL) |
| `/windowsac` | Windows'u aç (WOL + otomatik geçiş) |
| `/kapat` | Ubuntu'yu kapat |
| `/durum` | PC durumunu kontrol et |
| `/ota` | OTA güncelleme bilgisi |
| `/start` | Komut listesi |

### OTA Güncelleme

NodeMCU'yu kablosuz güncellemek için:

1. Tarayıcıda `http://192.168.1.199/update` adresine git
2. Kullanıcı: `admin`, Şifre: `1234`
3. `.bin` dosyasını yükle

Bin dosyası oluşturmak için:
```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 --output-dir build nodemcu/nodemcu-pc-control.ino
# build/nodemcu-pc-control.ino.bin dosyasını yükle
```

### Masaüstü Uygulaması

Kurulumdan sonra masaüstünde "Windows'a Geç" ikonu görünür. Tıklayınca onay alır ve Windows'a geçer.

## Mimari

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│  Telegram   │◄───►│   NodeMCU    │◄───►│  Ubuntu PC  │
│    Bot      │     │  ESP8266     │     │  :8888      │
└─────────────┘     │              │     └──────┬──────┘
                    │  • WOL       │            │
                    │  • HTTP      │            ▼
                    │  • OTA       │     ┌─────────────┐
                    └──────────────┘     │    GRUB     │
                                         │  Windows    │
                                         └─────────────┘
```

## Dosya Yapısı

```
ubuntu-nodemcu-WOL/
├── README.md
├── requirements.txt
├── setup.sh                 # Ana kurulum scripti
├── nodemcu/
│   └── nodemcu-pc-control.ino
├── ubuntu/
│   ├── windows-switch-server.py  # HTTP server + boot bildirimi
│   ├── windows-switch.service
│   ├── reboot-to-windows.sh
│   └── 11_windows           # GRUB entry
└── desktop/
    ├── windows-switch.desktop
    └── switch-to-windows-gui.sh
```

## Sorun Giderme

### Servis çalışmıyor
```bash
sudo systemctl status windows-switch.service
sudo journalctl -u windows-switch.service -f
```

### Port 8888 dinlenmiyor
```bash
ss -tlnp | grep 8888
```

### GRUB sırası yanlış
```bash
sudo update-grub
grep -E "menuentry" /boot/grub/grub.cfg
```

### NodeMCU bağlanmıyor
- WiFi bilgilerini kontrol et
- Serial monitörden logları oku (115200 baud)

### WOL çalışmıyor
- BIOS'ta WoL etkin mi?
- Ethernet kablosu bağlı mı? (WiFi ile WoL çalışmaz)

### Boot bildirimi gelmiyor
```bash
sudo systemctl status boot-notify.service
sudo journalctl -u boot-notify.service
# Manuel test:
sudo /usr/local/bin/boot-notify.sh
```

## Lisans

MIT

## Katkıda Bulunma

Pull request'ler memnuniyetle karşılanır.
