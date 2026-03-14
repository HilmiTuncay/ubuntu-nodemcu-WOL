#!/usr/bin/env python3
"""
NodeMCU PC Control - Ubuntu HTTP Server
https://github.com/HilmiTuncay/ubuntu-nodemcu-WOL

Port: 8888
Endpoints:
  /reboot-windows - Windows'a reboot
  /shutdown       - Sistemi kapat
  /idle-time      - Idle süresini dakika olarak döndür
  /ping           - Basit ping kontrolü
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import subprocess
import os
import time
import json
import socket
from urllib.request import Request, urlopen
from urllib.parse import urlencode
from datetime import datetime


def get_idle_minutes():
    """
    Sistem idle süresini dakika olarak hesapla.
    Birden fazla yöntem dener.
    """
    # Yöntem 1: xprintidle (X11 idle time - ms cinsinden)
    try:
        result = subprocess.run(
            ['xprintidle'],
            capture_output=True,
            text=True,
            timeout=2,
            env={**os.environ, 'DISPLAY': ':0'}
        )
        if result.returncode == 0:
            idle_ms = int(result.stdout.strip())
            return idle_ms // 60000  # ms -> dakika
    except Exception:
        pass

    # Yöntem 2: /dev/pts ve /dev/tty aktivitesi
    try:
        latest_activity = 0
        for dev in ['/dev/tty1', '/dev/tty2', '/dev/pts/0', '/dev/pts/1']:
            try:
                stat = os.stat(dev)
                if stat.st_atime > latest_activity:
                    latest_activity = stat.st_atime
            except Exception:
                continue

        if latest_activity > 0:
            idle_seconds = time.time() - latest_activity
            return int(idle_seconds // 60)
    except Exception:
        pass

    # Yöntem 3: who komutu ile son login kontrolü
    try:
        result = subprocess.run(['who', '-u'], capture_output=True, text=True, timeout=2)
        if result.stdout.strip():
            lines = result.stdout.strip().split('\n')
            min_idle = float('inf')
            for line in lines:
                parts = line.split()
                if len(parts) >= 6:
                    idle_str = parts[5]
                    if idle_str == '.':
                        return 0  # Aktif
                    elif ':' in idle_str:
                        h, m = map(int, idle_str.split(':'))
                        idle_min = h * 60 + m
                    else:
                        try:
                            idle_min = int(idle_str)
                        except ValueError:
                            continue
                    if idle_min < min_idle:
                        min_idle = idle_min
            if min_idle != float('inf'):
                return int(min_idle)
    except Exception:
        pass

    # Yöntem 4: Aktif bağlantı kontrolü
    try:
        result = subprocess.run(['ss', '-tn'], capture_output=True, text=True, timeout=2)
        if ':22 ' in result.stdout or ':4000 ' in result.stdout:
            return 0
    except Exception:
        pass

    # Fallback
    return 0


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/reboot-windows':
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'Rebooting to Windows...')
            subprocess.Popen(['sudo', '/usr/local/bin/reboot-to-windows.sh'])

        elif self.path == '/shutdown':
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'Shutting down...')
            subprocess.Popen(['sudo', 'shutdown', '-h', 'now'])

        elif self.path == '/idle-time':
            idle_minutes = get_idle_minutes()
            self.send_response(200)
            self.end_headers()
            self.wfile.write(str(idle_minutes).encode())

        elif self.path == '/ping':
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'pong')

        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'Not found')

    def log_message(self, format, *args):
        pass  # Sessiz çalışsın


def send_boot_notification():
    """
    Ubuntu açıldığında Telegram'a bildirim gönder.
    BOT_TOKEN ve CHAT_ID environment variable'larından okunur.
    """
    bot_token = os.environ.get('BOT_TOKEN', '')
    chat_id = os.environ.get('CHAT_ID', '')

    if not bot_token or not chat_id:
        print("BOT_TOKEN/CHAT_ID ayarlanmamış, boot bildirimi atlandı.")
        return

    try:
        ip_addr = subprocess.run(
            ['hostname', '-I'], capture_output=True, text=True, timeout=5
        ).stdout.strip().split()[0]
    except Exception:
        ip_addr = 'bilinmiyor'

    hostname = socket.gethostname()
    now = datetime.now().strftime('%d.%m.%Y %H:%M:%S')

    message = (
        f"🐧 *Ubuntu Açıldı!*\n\n"
        f"🖥️ Hostname: {hostname}\n"
        f"🌐 IP: {ip_addr}\n"
        f"📅 Tarih: {now}"
    )

    try:
        url = f"https://api.telegram.org/bot{bot_token}/sendMessage"
        data = urlencode({
            'chat_id': chat_id,
            'text': message,
            'parse_mode': 'Markdown'
        }).encode()
        req = Request(url, data=data)
        urlopen(req, timeout=10)
        print("Boot bildirimi gönderildi.")
    except Exception as e:
        print(f"Boot bildirimi gönderilemedi: {e}")


if __name__ == '__main__':
    send_boot_notification()
    server = HTTPServer(('0.0.0.0', 8888), Handler)
    print("Server running on port 8888...")
    server.serve_forever()
