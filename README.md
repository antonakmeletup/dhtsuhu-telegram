# 📟 Running Text P10 + DHT11 dengan Kontrol Telegram (ESP32 Dual Core)

Proyek ini adalah sistem *Running Text* menggunakan panel LED Matrix P10 (HUB75) berbasis ESP32, dilengkapi dengan sensor Suhu & Kelembapan (DHT11), dan dapat dikendalikan dari jarak jauh melalui **Telegram Bot**.

Kode ini telah dioptimalkan dengan sistem **Dual Core** pada ESP32, sehingga animasi teks berjalan sangat mulus di Core 1, sementara komunikasi jaringan (WiFi & Telegram) dan pembacaan sensor berjalan di Core 0.

## ✨ Fitur Utama

1. **Dual Core Processing**: Animasi teks berjalan tanpa *lag/stuttering* meskipun ESP32 sedang mengunduh pesan dari server Telegram (yang biasanya memakan waktu TLS handshake 2-4 detik).
2. **Double Buffering & Rendering Optimation**: Teks tidak berkedip (*flicker*) saat berjalan. Sistem hanya menggambar karakter yang benar-benar terlihat di layar sehingga hemat memori dan prosesor.
3. **Penyimpanan Permanen (EEPROM/Preferences)**: Pengaturan teks, warna, kecepatan, kecerahan, dan ukuran akan tersimpan otomatis. Saat ESP32 mati atau *restart*, pengaturan terakhir tidak akan hilang.
4. **Auto-Color Temperature**: Bisa mengatur warna secara otomatis berdasarkan suhu (Biru jika dingin, Hijau jika normal, Merah jika panas).
5. **Manajemen Daya Torsi Rendah**: Daya pemancar WiFi diturunkan (`WIFI_POWER_11dBm`) dan fitur *modem sleep* diaktifkan agar ESP32 tidak cepat panas.
6. **Variabel Dinamis**: Bisa menyisipkan data sensor langsung ke dalam teks menggunakan format `{S}` (Suhu) dan `{H}` (Kelembapan).

## 🛠️ Alat dan Bahan

* **Board:** ESP32 DevKit V1 (atau varian ESP32 lainnya)
* **Panel Matrix:** P10 (HUB75) 32x16. (Bawaan kode diatur untuk 2 panel berjejer, resolusi total 64x16).
* **Sensor:** DHT11 (atau DHT22 dengan sedikit penyesuaian di kode).
* **Kabel Jumper:** Secukupnya (Female to Female).
* **Power Supply (Adaptor):** 5V / minimal 3A khusus untuk menyuplai panel P10.

⚠️ **CATATAN PENTING DAYA:**
Jangan pernah mengambil daya untuk panel P10 dari pin `5V` atau `VIN` ESP32! Gunakan adaptor 5V terpisah untuk panel, dan pastikan **GND (Ground)** dari adaptor dihubungkan ke **GND ESP32**.

## 📚 Library Arduino IDE yang Dibutuhkan

Pastikan Anda sudah menginstal library berikut melalui *Library Manager* Arduino IDE:

1. `ESP32 HUB75 LED MATRIX PANEL DMA Display` oleh mrfaptastic
2. `Adafruit DHT sensor library` oleh Adafruit
3. `Adafruit Unified Sensor` oleh Adafruit
4. `UniversalTelegramBot` oleh Brian Lough
5. `ArduinoJson` oleh Benoit Blanchon (versi 6.x)

## 🚀 Cara Penggunaan (Setup)

1. **Buat Bot Telegram:**
* Buka Telegram dan cari akun **@BotFather**.
* Ketik `/newbot`, ikuti langkahnya, lalu simpan **Bot Token** yang diberikan.
* (Opsional) Dapatkan *Chat ID* Anda melalui **@userinfobot** jika ingin membatasi akses kontrol.


2. **Edit Konfigurasi di Kode:**
Buka *source code*, temukan bagian ini, dan isi dengan data Anda:
```cpp
const char* WIFI_SSID = "NAMA_WIFI_ANDA";
const char* WIFI_PASS = "PASSWORD_WIFI_ANDA";
#define BOT_TOKEN "TOKEN_BOT_DARI_BOTFATHER_ANDA"
#define ALLOWED_CHAT_ID "" // Isi dengan Chat ID Anda, biarkan kosong agar siapa saja bisa akses

```


3. **Wiring (Pengkabelan):**
* **DHT11:** Pin Data ke `Pin 27` ESP32.


| Label di PCB Panel | Sinyal Data | Pin ESP32 DevKit V1 | Keterangan Kode |
| --- | --- | --- | --- |
| **DR1** | Data Red Upper | **GPIO 23** | `#define R1_PIN 23` |
| **DG1** | Data Green Upper | **GPIO 21** | `#define G1_PIN 21` |
| **DB1** | Data Blue Upper | **GPIO 22** | `#define B1_PIN 22` |
| **DR2** | Data Red Lower | **GPIO 4** | `#define R2_PIN 4` |
| **DG2** | Data Green Lower | **GPIO 19** | `#define G2_PIN 19` |
| **DB2** | Data Blue Lower | **GPIO 15** | `#define B2_PIN 15` |
| **A** | Address A | **GPIO 5** | `#define A_PIN 5` |
| **B** | Address B | **GPIO 33** | `#define B_PIN 33` |
| **C** | Address C | **GPIO 18** | `#define C_PIN 18` |
| **CLK** | Clock | **GPIO 26** | `#define CLK_PIN 26` |
| **STB** | Strobe / Latch | **GPIO 32** | `#define LAT_PIN 32` |
| **OE** | Output Enable | **GPIO 25** | `#define OE_PIN 25` |
| **GND** | Ground | **GND ESP32** | Hubungkan ke GND ESP32 & Power Supply |

---

### Catatan Tambahan:

* **STB (Strobe)** pada panel Anda berfungsi sama dengan **LAT (Latch)** dalam konfigurasi kode.
* **DR, DG, DB** adalah penamaan lain dari **R, G, B** (Data Red, Data Green, Data Blue).
* Seluruh pin bertanda **GND** di sisi kanan PCB panel dapat dihubungkan bersama ke **GND ESP32** dan **GND Power Supply 5V**.




4. **Upload ke ESP32:**
Pilih board ESP32, port yang sesuai, lalu klik **Upload**.

## 🤖 Daftar Command (Perintah) Telegram

Kirimkan perintah ini ke Bot Telegram yang telah Anda buat:

| Perintah | Deskripsi / Cara Pakai |
| --- | --- |
| `/start` atau `/help` | Menampilkan menu bantuan dan daftar perintah. |
| `/teks <isi>` | Mengganti tulisan. Gunakan `{S}` untuk suhu, `{H}` untuk kelembapan. <br>

<br>Contoh: `/teks Suhu saat ini {S} dan Kelembapan {H}` |
| `/warna <nama/hex>` | Mengubah warna teks. Pilihan: `merah`, `hijau`, `biru`, `kuning`, `cyan`, `magenta`, `putih`, `oranye`, `ungu`, `auto`. Bisa juga format hex.<br>

<br>Contoh: `/warna kuning` atau `/warna #00FFCC` |
| `/kecepatan <1-10>` | Mengatur kecepatan gerak teks (1 paling lambat, 10 paling cepat).<br>

<br>Contoh: `/kecepatan 8` |
| `/terang <1-100>` | Mengatur intensitas cahaya/kecerahan panel (persen).<br>

<br>Contoh: `/terang 40` |
| `/ukuran <1-2>` | Mengubah ukuran huruf (1 untuk font kecil, 2 untuk font besar).<br>

<br>Contoh: `/ukuran 2` |
| `/suhu` | Bot akan membalas dengan status suhu dan kelembapan saat ini. |
| `/status` | Mengecek seluruh status saat ini (teks aktif, warna, kecepatan, IP Address WiFi, sisa RAM, dll). |

---------------------------------------------------------------------------------------------------------------------

BY: Claude And Gemini
