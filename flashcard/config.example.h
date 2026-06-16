#pragma once

// ── Sunucu adresi ──────────────────────────────────────
// server.py'nin çalıştığı bilgisayarın IP'si ve portu
#define SERVER_URL "http://192.168.x.x:5000"

// ── Wi-Fi ağları ───────────────────────────────────────
// Normal ağ:          { "SSID", "Şifre", NULL }
// WPA2-Enterprise:    { "SSID", "Şifre", "KullanıcıAdı" }
// İstediğin kadar ağ ekleyebilirsin
WiFiNetwork networks[] = {
  { "WIFI_ADI",    "WIFI_SIFRESI",  NULL          },
  { "OKUL_WIFI",   "OKUL_SIFRESI", "OKUL_NUMARAN" },
};
const int NETWORK_COUNT = 2;