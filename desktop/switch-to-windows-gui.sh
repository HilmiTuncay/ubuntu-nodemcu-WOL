#!/bin/bash
# Windows'a Geç - GUI Scripti
# Masaüstünden çalıştırılır, onay alır ve Windows'a geçer

# zenity yüklü mü kontrol et
if ! command -v zenity &> /dev/null; then
    # zenity yoksa basit xmessage kullan
    if command -v xmessage &> /dev/null; then
        xmessage -buttons "Evet:0,Hayır:1" "Windows'a geçmek istiyor musunuz?"
        if [ $? -eq 0 ]; then
            pkexec /usr/local/bin/reboot-to-windows.sh
        fi
    else
        # Hiçbiri yoksa direkt geç
        pkexec /usr/local/bin/reboot-to-windows.sh
    fi
    exit 0
fi

# zenity ile onay penceresi
zenity --question \
    --title="Windows'a Geç" \
    --text="Bilgisayar Windows ile yeniden başlatılacak.\n\nDevam etmek istiyor musunuz?" \
    --ok-label="Evet, Windows'a Geç" \
    --cancel-label="İptal" \
    --icon-name=system-restart \
    --width=300

if [ $? -eq 0 ]; then
    # Kullanıcı onayladı
    zenity --info \
        --title="Windows'a Geçiliyor" \
        --text="Sistem Windows ile yeniden başlatılıyor..." \
        --timeout=3 \
        --width=250 &

    # Root yetkisi ile reboot
    pkexec /usr/local/bin/reboot-to-windows.sh
fi
