/*
 * Running Text P10 + DHT11 + Kontrol Telegram  (VERSI DUAL CORE)
 * Board  : ESP32 DevKit V1
 * Panel  : P10 (HUB75) 32x16, 2 panel = 64x16
 * Sensor : DHT11 (pin 27)
 *
 * PERBAIKAN dibanding versi sebelumnya:
 *   1. Telegram + DHT dijalankan di task terpisah pada CORE 0.
 *      Scroll jalan di CORE 1 lewat loop(), jadi handshake TLS yang
 *      lambat (2-4 detik) tidak lagi membekukan animasi teks.
 *   2. Hanya karakter yang terlihat di layar yang digambar.
 *   3. Double buffering supaya tidak berkedip.
 *   4. Daya WiFi diturunkan agar ESP32 lebih dingin.
 *
 * CATATAN PENTING SOAL DAYA:
 *   Panel P10 WAJIB pakai adaptor 5V sendiri (minimal 3A).
 *   JANGAN ambil daya panel dari pin 5V ESP32 atau dari USB.
 *   Sambungkan GND adaptor panel ke GND ESP32 (ground bersama).
 *
 * LIBRARY:
 *   ESP32 HUB75 LED MATRIX PANEL DMA (mrfaptastic), DHT sensor library,
 *   Adafruit Unified Sensor, UniversalTelegramBot, ArduinoJson
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <DHT.h>

/* ================== ISI BAGIAN INI ================== */
const char* WIFI_SSID = "NAMA_WIFI_ANDA";
const char* WIFI_PASS = "PASSWORD_WIFI_ANDA";

#define BOT_TOKEN "1234567890:AAAAbbbbCCCCddddEEEEffffGGGGhhhh"
#define ALLOWED_CHAT_ID ""     // kosong = semua orang boleh; isi chat ID untuk membatasi
/* ==================================================== */

/* ================== DHT11 ================== */
#define DHTPIN  27
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* ================== HUB75 ================== */
#define R1_PIN 23
#define G1_PIN 21
#define B1_PIN 22
#define R2_PIN 4
#define G2_PIN 19
#define B2_PIN 15

#define A_PIN 5
#define B_PIN 33
#define C_PIN 18
#define D_PIN -1
#define E_PIN -1

#define LAT_PIN 32
#define OE_PIN  25
#define CLK_PIN 26

#define PANEL_RES_X 32
#define PANEL_RES_Y 16
#define PANEL_CHAIN 2
#define LEBAR_LAYAR (PANEL_RES_X * PANEL_CHAIN)

MatrixPanel_I2S_DMA *dma_display = nullptr;

/* ================== TELEGRAM ================== */
WiFiClientSecure secured;
UniversalTelegramBot bot(BOT_TOKEN, secured);
const unsigned long JEDA_POLL = 2000;

/* ================== PENGATURAN ================== */
Preferences prefs;
SemaphoreHandle_t kunci;        // pelindung data yang dipakai dua core

String teksUser   = "KELOMPOK 5 XI TKJ 1";
int    modeWarna  = 0;          // 0 = auto, -1 = hex custom, >0 = index daftarWarna
int    kecepatan  = 6;          // 1..10
int    kecerahan  = 50;         // 1..100
int    ukuranTeks = 2;          // 1..2
uint8_t cR = 255, cG = 255, cB = 255;

struct Warna { const char* nama; uint8_t r, g, b; };
Warna daftarWarna[] = {
  { "auto",    0,   0,   0   },
  { "merah",   255, 0,   0   },
  { "hijau",   0,   255, 0   },
  { "biru",    0,   0,   255 },
  { "kuning",  255, 255, 0   },
  { "cyan",    0,   255, 255 },
  { "magenta", 255, 0,   255 },
  { "putih",   255, 255, 255 },
  { "oranye",  255, 110, 0   },
  { "ungu",    150, 0,   255 }
};
const int JUMLAH_WARNA = sizeof(daftarWarna) / sizeof(daftarWarna[0]);

/* ================== STATE ================== */
String tampil    = "";
int    lebarTeks = 0;
int    scrollX   = LEBAR_LAYAR;
unsigned long lastScroll = 0;

volatile float suhu = 0, hum = 0;
volatile bool  sensorBaru  = false;
volatile bool  gantiSetting = false;   // diset oleh core 0, dibaca core 1

/* ===================================================================
 * FUNGSI BANTU
 * =================================================================== */

int jedaScroll() {                       // 1 -> 90ms, 10 -> 8ms
  return map(constrain(kecepatan, 1, 10), 1, 10, 90, 8);
}

uint16_t warnaAktif() {
  if (modeWarna == -1) return dma_display->color565(cR, cG, cB);
  if (modeWarna == 0) {
    if (suhu < 25)      return dma_display->color565(0, 0, 255);
    else if (suhu < 30) return dma_display->color565(0, 255, 0);
    else                return dma_display->color565(255, 0, 0);
  }
  Warna w = daftarWarna[modeWarna];
  return dma_display->color565(w.r, w.g, w.b);
}

String namaWarnaAktif() {
  if (modeWarna == -1) {
    char buf[10];
    sprintf(buf, "#%02X%02X%02X", cR, cG, cB);
    return String(buf);
  }
  return String(daftarWarna[modeWarna].nama);
}

void susunTeks() {
  String t = teksUser;
  bool adaPlaceholder = (t.indexOf("{S}") >= 0 || t.indexOf("{H}") >= 0);

  t.replace("{S}", String(suhu, 1) + "C");
  t.replace("{H}", String(hum, 0) + "%");

  if (!adaPlaceholder) {
    t += "  SUHU: " + String(suhu, 1) + "C  KELEMBAPAN: " + String(hum, 0) + "%";
  }

  tampil    = " " + t + "   ";
  lebarTeks = tampil.length() * 6 * ukuranTeks;
}

/* Hanya menggambar karakter yang benar-benar terlihat di layar.
   Menggambar seluruh string tiap frame itu pemborosan besar. */
void gambar() {
  dma_display->fillScreen(0);
  dma_display->setTextWrap(false);
  dma_display->setTextSize(ukuranTeks);
  dma_display->setTextColor(warnaAktif());

  int lebarKar = 6 * ukuranTeks;
  int y = (PANEL_RES_Y - (8 * ukuranTeks)) / 2;

  int awal = 0;
  if (scrollX < 0) awal = (-scrollX) / lebarKar;
  int akhir = awal + (LEBAR_LAYAR / lebarKar) + 2;
  if (akhir > (int)tampil.length()) akhir = tampil.length();

  dma_display->setCursor(scrollX + (awal * lebarKar), y);
  for (int i = awal; i < akhir; i++) dma_display->write(tampil[i]);

  dma_display->flipDMABuffer();   // hapus baris ini jika library Anda versi lama
}

void pesanStatis(String s) {
  dma_display->fillScreen(0);
  dma_display->setTextWrap(false);
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 255, 0));
  dma_display->setCursor(1, 4);
  dma_display->print(s);
  dma_display->flipDMABuffer();
}

void simpanPengaturan() {
  prefs.begin("panel", false);
  prefs.putString("teks", teksUser);
  prefs.putInt("warna", modeWarna);
  prefs.putInt("speed", kecepatan);
  prefs.putInt("terang", kecerahan);
  prefs.putInt("ukuran", ukuranTeks);
  prefs.putUChar("cR", cR);
  prefs.putUChar("cG", cG);
  prefs.putUChar("cB", cB);
  prefs.end();
}

void muatPengaturan() {
  prefs.begin("panel", true);
  teksUser   = prefs.getString("teks", teksUser);
  modeWarna  = prefs.getInt("warna", 0);
  kecepatan  = prefs.getInt("speed", 6);
  kecerahan  = prefs.getInt("terang", 50);
  ukuranTeks = prefs.getInt("ukuran", 2);
  cR = prefs.getUChar("cR", 255);
  cG = prefs.getUChar("cG", 255);
  cB = prefs.getUChar("cB", 255);
  prefs.end();
}

/* ===================================================================
 * PERINTAH TELEGRAM (berjalan di CORE 0)
 * =================================================================== */

String teksBantuan() {
  return "PANEL P10 - SMKN 1 GEGER\n\n"
         "/teks <isi> - ganti tulisan\n"
         "   pakai {S} dan {H} untuk menyisipkan suhu/kelembapan\n"
         "   contoh: /teks SUHU {S} RH {H}\n\n"
         "/warna <nama> - merah, hijau, biru, kuning, cyan,\n"
         "   magenta, putih, oranye, ungu, auto, atau #FF8800\n\n"
         "/kecepatan <1-10>\n"
         "/terang <1-100>\n"
         "/ukuran <1-2>\n"
         "/suhu\n"
         "/status";
}

String teksStatus() {
  String s = "PENGATURAN SAAT INI\n\n";
  s += "Teks      : " + teksUser + "\n";
  s += "Warna     : " + namaWarnaAktif() + "\n";
  s += "Kecepatan : " + String(kecepatan) + "/10\n";
  s += "Kecerahan : " + String(kecerahan) + "%\n";
  s += "Ukuran    : " + String(ukuranTeks) + "\n";
  s += "Suhu      : " + String(suhu, 1) + " C\n";
  s += "Kelembapan: " + String(hum, 0) + " %\n";
  s += "RAM bebas : " + String(ESP.getFreeHeap()) + " byte\n";
  s += "IP        : " + WiFi.localIP().toString();
  return s;
}

bool parseHex(String h, uint8_t &r, uint8_t &g, uint8_t &b) {
  h.trim();
  if (h.startsWith("#")) h = h.substring(1);
  if (h.length() != 6) return false;
  for (int i = 0; i < 6; i++) if (!isHexadecimalDigit(h[i])) return false;
  long v = strtol(h.c_str(), NULL, 16);
  r = (v >> 16) & 0xFF;
  g = (v >> 8) & 0xFF;
  b = v & 0xFF;
  return true;
}

void prosesPesan(int jumlah) {
  for (int i = 0; i < jumlah; i++) {
    String chat_id = bot.messages[i].chat_id;
    String txt     = bot.messages[i].text;
    txt.trim();

    Serial.println("Pesan dari chat ID: " + chat_id + " -> " + txt);

    if (String(ALLOWED_CHAT_ID).length() > 0 && chat_id != String(ALLOWED_CHAT_ID)) {
      bot.sendMessage(chat_id, "Maaf, Anda tidak punya akses ke panel ini.", "");
      continue;
    }

    String cmd = txt, arg = "";
    int sp = txt.indexOf(' ');
    if (sp > 0) {
      cmd = txt.substring(0, sp);
      arg = txt.substring(sp + 1);
      arg.trim();
    }
    int at = cmd.indexOf('@');
    if (at > 0) cmd = cmd.substring(0, at);
    cmd.toLowerCase();

    if (cmd == "/start" || cmd == "/help") {
      bot.sendMessage(chat_id, teksBantuan(), "");
    }

    else if (cmd == "/teks") {
      if (arg.length() == 0) {
        bot.sendMessage(chat_id, "Tulis isinya. Contoh: /teks SELAMAT DATANG", "");
      } else if (arg.length() > 200) {
        bot.sendMessage(chat_id, "Teks terlalu panjang, maksimal 200 karakter.", "");
      } else {
        xSemaphoreTake(kunci, portMAX_DELAY);
        teksUser = arg;
        gantiSetting = true;
        xSemaphoreGive(kunci);
        simpanPengaturan();
        bot.sendMessage(chat_id, "Teks diubah menjadi:\n" + arg, "");
      }
    }

    else if (cmd == "/warna") {
      String a = arg; a.toLowerCase();
      bool ketemu = false;
      uint8_t r, g, b;

      xSemaphoreTake(kunci, portMAX_DELAY);
      for (int k = 0; k < JUMLAH_WARNA; k++) {
        if (a == daftarWarna[k].nama) { modeWarna = k; ketemu = true; break; }
      }
      if (!ketemu && parseHex(arg, r, g, b)) {
        cR = r; cG = g; cB = b;
        modeWarna = -1;
        ketemu = true;
      }
      xSemaphoreGive(kunci);

      if (ketemu) {
        simpanPengaturan();
        bot.sendMessage(chat_id, "Warna diubah menjadi: " + namaWarnaAktif(), "");
      } else {
        bot.sendMessage(chat_id,
          "Warna tidak dikenal.\nPilihan: merah, hijau, biru, kuning, cyan, "
          "magenta, putih, oranye, ungu, auto, atau kode hex seperti #FF8800", "");
      }
    }

    else if (cmd == "/kecepatan") {
      int v = arg.toInt();
      if (v >= 1 && v <= 10) {
        kecepatan = v;
        simpanPengaturan();
        bot.sendMessage(chat_id, "Kecepatan diatur ke " + String(v) + "/10", "");
      } else {
        bot.sendMessage(chat_id, "Masukkan angka 1 sampai 10. Contoh: /kecepatan 8", "");
      }
    }

    else if (cmd == "/terang") {
      int v = arg.toInt();
      if (v >= 1 && v <= 100) {
        kecerahan = v;
        dma_display->setBrightness8(map(kecerahan, 1, 100, 10, 255));
        simpanPengaturan();
        bot.sendMessage(chat_id, "Kecerahan diatur ke " + String(v) + "%", "");
      } else {
        bot.sendMessage(chat_id, "Masukkan angka 1 sampai 100. Contoh: /terang 60", "");
      }
    }

    else if (cmd == "/ukuran") {
      int v = arg.toInt();
      if (v == 1 || v == 2) {
        xSemaphoreTake(kunci, portMAX_DELAY);
        ukuranTeks = v;
        gantiSetting = true;
        xSemaphoreGive(kunci);
        simpanPengaturan();
        bot.sendMessage(chat_id, "Ukuran font diatur ke " + String(v), "");
      } else {
        bot.sendMessage(chat_id, "Ukuran hanya 1 atau 2. Contoh: /ukuran 2", "");
      }
    }

    else if (cmd == "/suhu") {
      bot.sendMessage(chat_id,
        "Suhu: " + String(suhu, 1) + " C\nKelembapan: " + String(hum, 0) + " %", "");
    }

    else if (cmd == "/status") {
      bot.sendMessage(chat_id, teksStatus(), "");
    }

    else {
      bot.sendMessage(chat_id, "Perintah tidak dikenal. Ketik /help untuk bantuan.", "");
    }
  }
}

/* ===================================================================
 * TASK JARINGAN + SENSOR  ->  CORE 0
 * =================================================================== */

void taskJaringan(void *pv) {
  unsigned long lastPoll = 0, lastBaca = 0, lastRetry = 0;

  for (;;) {
    /* --- DHT11 --- */
    if (millis() - lastBaca > 3000) {
      lastBaca = millis();
      float s = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(s) && !isnan(h)) {
        suhu = s;
        hum  = h;
        sensorBaru = true;
      }
    }

    /* --- WiFi --- */
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - lastRetry > 15000) {
        lastRetry = millis();
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
      vTaskDelay(500 / portTICK_PERIOD_MS);
      continue;
    }

    /* --- Telegram --- */
    if (millis() - lastPoll > JEDA_POLL) {
      lastPoll = millis();
      int jumlah = bot.getUpdates(bot.last_message_received + 1);
      while (jumlah) {
        prosesPesan(jumlah);
        jumlah = bot.getUpdates(bot.last_message_received + 1);
      }
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);   // beri napas ke scheduler
  }
}

/* ===================================================================
 * SETUP
 * =================================================================== */

void setup() {
  Serial.begin(115200);
  Serial.println("\nSMKN 1 GEGER - Panel P10 Telegram (dual core)");

  kunci = xSemaphoreCreateMutex();
  muatPengaturan();
  dht.begin();

  HUB75_I2S_CFG::i2s_pins _pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
  };

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
  mxconfig.i2sspeed    = HUB75_I2S_CFG::HZ_8M;   // 8M lebih ramah ke WiFi
  mxconfig.double_buff = true;                   // anti kedip

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(map(kecerahan, 1, 100, 10, 255));
  dma_display->clearScreen();

  /* --- WiFi --- */
  pesanStatis("WIFI...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                       // modem sleep, bikin lebih adem
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Menghubungkan WiFi");

  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) {
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setTxPower(WIFI_POWER_11dBm);       // turunkan daya pancar -> lebih dingin
    Serial.println("\nTerhubung. IP: " + WiFi.localIP().toString());
    pesanStatis("OK");
  } else {
    Serial.println("\nWiFi gagal, panel tetap jalan tanpa Telegram.");
    pesanStatis("NO WIFI");
  }
  delay(1200);

  secured.setInsecure();
  bot.longPoll = 0;

  float s = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(s)) suhu = s;
  if (!isnan(h)) hum  = h;

  susunTeks();
  scrollX    = LEBAR_LAYAR;
  lastScroll = millis();

  /* Task jaringan dipasang di CORE 0, bersama stack WiFi.
     loop() sendiri berjalan di CORE 1 dan hanya mengurus scroll. */
  xTaskCreatePinnedToCore(taskJaringan, "jaringan", 10000, NULL, 1, NULL, 0);
}

/* ===================================================================
 * LOOP  ->  CORE 1, hanya menggerakkan teks
 * =================================================================== */

void loop() {
  if (millis() - lastScroll < (unsigned long)jedaScroll()) {
    vTaskDelay(1);
    return;
  }
  lastScroll = millis();

  xSemaphoreTake(kunci, portMAX_DELAY);

  if (gantiSetting) {          // teks / ukuran baru dari Telegram
    susunTeks();
    scrollX = LEBAR_LAYAR;
    gantiSetting = false;
  }

  gambar();
  scrollX--;

  if (scrollX < -lebarTeks) {  // satu putaran selesai
    scrollX = LEBAR_LAYAR;
    if (sensorBaru) {          // perbarui angka suhu di sini agar tidak melompat
      susunTeks();
      sensorBaru = false;
    }
  }

  xSemaphoreGive(kunci);
}
