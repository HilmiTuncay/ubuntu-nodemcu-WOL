#!/bin/bash
#
# Ubuntu NodeMCU WOL - Kurulum Scripti
# https://github.com/HilmiTuncay/ubuntu-nodemcu-WOL
#
# Kullanım: sudo bash setup.sh
#

set -e

# Renkler
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}"
echo "╔══════════════════════════════════════════════════════════╗"
echo "║         Ubuntu NodeMCU WOL - Kurulum Scripti             ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Root kontrolü
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Bu scripti root olarak çalıştırın: sudo bash setup.sh${NC}"
    exit 1
fi

# Script dizini
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${YELLOW}[1/7]${NC} GRUB kontrol ediliyor..."

# Mevcut 11_windows varsa GRUB adımlarını atla
if [ -f /etc/grub.d/11_windows ]; then
    EXISTING_UUID=$(grep -oP 'fs-uuid --set=root \K[A-F0-9-]+' /etc/grub.d/11_windows 2>/dev/null || true)
    if [ -n "$EXISTING_UUID" ]; then
        echo -e "${GREEN}  GRUB zaten yapılandırılmış (UUID: $EXISTING_UUID) ✓${NC}"
        SKIP_GRUB=true
    fi
fi

if [ "$SKIP_GRUB" != "true" ]; then
    echo -e "${YELLOW}  Windows EFI UUID tespit ediliyor...${NC}"

    # Mevcut 11_windows veya 40_custom'dan UUID al
    WINDOWS_UUID=""
    if [ -f /etc/grub.d/11_windows ]; then
        WINDOWS_UUID=$(grep -oP 'fs-uuid --set=root \K[A-F0-9-]+' /etc/grub.d/11_windows 2>/dev/null || true)
    fi
    if [ -z "$WINDOWS_UUID" ] && [ -f /etc/grub.d/40_custom ]; then
        WINDOWS_UUID=$(grep -oP 'fs-uuid --set=root \K[A-F0-9-]+' /etc/grub.d/40_custom 2>/dev/null || true)
    fi

    # Hala bulunamadıysa EFI partition'ı tara
    if [ -z "$WINDOWS_UUID" ]; then
        for part in /dev/disk/by-uuid/*; do
            uuid=$(basename "$part")
            if blkid "$part" 2>/dev/null | grep -qi "vfat"; then
                temp_mount="/tmp/efi_check_$$"
                mkdir -p "$temp_mount"
                if mount -o ro "$part" "$temp_mount" 2>/dev/null; then
                    if [ -f "$temp_mount/EFI/Microsoft/Boot/bootmgfw.efi" ]; then
                        WINDOWS_UUID="$uuid"
                        umount "$temp_mount"
                        rmdir "$temp_mount"
                        break
                    fi
                    umount "$temp_mount"
                fi
                rmdir "$temp_mount" 2>/dev/null || true
            fi
        done
    fi

    if [ -z "$WINDOWS_UUID" ]; then
        echo -e "${RED}Windows EFI UUID bulunamadı! GRUB yapılandırması atlanıyor.${NC}"
    else
        echo -e "${GREEN}  Windows EFI UUID: $WINDOWS_UUID${NC}"

        # 11_windows oluştur
        cat > /etc/grub.d/11_windows << EOF
#!/bin/sh
exec tail -n +3 \$0
# Windows GRUB Entry

menuentry "Windows" {
    insmod part_gpt
    insmod fat
    search --no-floppy --fs-uuid --set=root $WINDOWS_UUID
    chainloader /EFI/Microsoft/Boot/bootmgfw.efi
}
EOF
        chmod +x /etc/grub.d/11_windows

        # Eski windows entry varsa kaldır
        if [ -f /etc/grub.d/40_custom ]; then
            sed -i '/menuentry.*[Ww]indows/,/^}/d' /etc/grub.d/40_custom
        fi

        # GRUB_DISABLE_SUBMENU
        if grep -q "^GRUB_DISABLE_SUBMENU" /etc/default/grub; then
            sed -i 's/^GRUB_DISABLE_SUBMENU=.*/GRUB_DISABLE_SUBMENU=y/' /etc/default/grub
        else
            echo 'GRUB_DISABLE_SUBMENU=y' >> /etc/default/grub
        fi

        update-grub
        echo -e "${GREEN}  GRUB yapılandırıldı ✓${NC}"
    fi
fi

echo -e "${YELLOW}[2/7]${NC} HTTP server kuruluyor..."

# Server dosyasını kopyala
cp "$SCRIPT_DIR/ubuntu/windows-switch-server.py" /usr/local/bin/
chmod +x /usr/local/bin/windows-switch-server.py

# Reboot scripti
cp "$SCRIPT_DIR/ubuntu/reboot-to-windows.sh" /usr/local/bin/
chmod +x /usr/local/bin/reboot-to-windows.sh

echo -e "${GREEN}  Server dosyaları kopyalandı${NC}"

echo -e "${YELLOW}[3/7]${NC} Systemd servisi kuruluyor..."

# Servis dosyası
cp "$SCRIPT_DIR/ubuntu/windows-switch.service" /etc/systemd/system/

# Servisi etkinleştir ve başlat
systemctl daemon-reload
systemctl enable windows-switch.service
systemctl restart windows-switch.service

echo -e "${GREEN}  Servis kuruldu ve başlatıldı${NC}"

echo -e "${YELLOW}[4/7]${NC} Sudoers ayarlanıyor..."

# Shutdown için şifresiz sudo izni
echo "ALL ALL=(ALL) NOPASSWD: /sbin/shutdown, /usr/local/bin/reboot-to-windows.sh" > /etc/sudoers.d/nodemcu-wol
chmod 440 /etc/sudoers.d/nodemcu-wol

echo -e "${GREEN}  Sudoers ayarlandı${NC}"

echo -e "${YELLOW}[5/7]${NC} Masaüstü uygulaması kuruluyor..."

# GUI script
cp "$SCRIPT_DIR/desktop/switch-to-windows-gui.sh" /usr/local/bin/
chmod +x /usr/local/bin/switch-to-windows-gui.sh

# Desktop dosyası - tüm kullanıcılar için
cp "$SCRIPT_DIR/desktop/windows-switch.desktop" /usr/share/applications/

# Mevcut kullanıcının masaüstüne de kopyala
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)

if [ -d "$REAL_HOME/Masaüstü" ]; then
    cp "$SCRIPT_DIR/desktop/windows-switch.desktop" "$REAL_HOME/Masaüstü/"
    chown "$REAL_USER:$REAL_USER" "$REAL_HOME/Masaüstü/windows-switch.desktop"
    chmod +x "$REAL_HOME/Masaüstü/windows-switch.desktop"
elif [ -d "$REAL_HOME/Desktop" ]; then
    cp "$SCRIPT_DIR/desktop/windows-switch.desktop" "$REAL_HOME/Desktop/"
    chown "$REAL_USER:$REAL_USER" "$REAL_HOME/Desktop/windows-switch.desktop"
    chmod +x "$REAL_HOME/Desktop/windows-switch.desktop"
fi

echo -e "${GREEN}  Masaüstü uygulaması kuruldu${NC}"

echo -e "${YELLOW}[6/7]${NC} Telegram bildirimi ayarlanıyor..."

# Zaten /etc/nodemcu-wol.env varsa atla
if [ -f /etc/nodemcu-wol.env ]; then
    echo -e "${GREEN}  Telegram ayarları zaten mevcut ✓${NC}"
elif [ -f "$SCRIPT_DIR/.env" ]; then
    cp "$SCRIPT_DIR/.env" /etc/nodemcu-wol.env
    chmod 600 /etc/nodemcu-wol.env
    echo -e "${GREEN}  .env dosyası kopyalandı ✓${NC}"
else
    echo -e "${YELLOW}  .env dosyası bulunamadı, Telegram bildirimi atlandı.${NC}"
    echo -e "${YELLOW}  Manuel eklemek için: sudo nano /etc/nodemcu-wol.env${NC}"
fi

systemctl daemon-reload
systemctl restart windows-switch.service

echo -e "${YELLOW}[7/7]${NC} Servis durumu kontrol ediliyor..."

if systemctl is-active --quiet windows-switch.service; then
    echo -e "${GREEN}  windows-switch servisi çalışıyor ✓${NC}"
else
    echo -e "${RED}  windows-switch servisi çalışmıyor! Log: journalctl -u windows-switch.service${NC}"
fi

echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════╗"
echo "║                    KURULUM TAMAMLANDI                     ║"
echo "╚══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}GRUB Sırası:${NC}"
echo "  1. Ubuntu (varsayılan)"
echo "  2. Windows (↓ tuşu ile seç)"
echo ""
echo -e "${GREEN}HTTP Endpoints (port 8888):${NC}"
echo "  /reboot-windows - Windows'a geç"
echo "  /shutdown       - Sistemi kapat"
echo "  /idle-time      - Boşta kalma süresi"
echo "  /ping           - Bağlantı testi"
echo ""
echo -e "${GREEN}Telegram:${NC}"
echo "  Boot bildirimi: Ubuntu açılınca Telegram'dan mesaj gelir"
echo ""
echo -e "${GREEN}Masaüstü:${NC}"
echo "  'Windows'a Geç' uygulaması eklendi"
echo ""
echo -e "${YELLOW}Sonraki adım: NodeMCU kodunu yükleyin (nodemcu/ klasörü)${NC}"
