# Arduino MCP2515 SLCAN Adaptörü

Arduino Uno + MCP2515 CAN kontrolcüsü kullanarak, aracın CAN bus'ını Linux
SocketCAN arayüzüne (`can0`, `slcan0` vb.) bağlayan bir SLCAN (LAWICEL)
protokol implementasyonu.

## İçindekiler

- [Arduino MCP2515 SLCAN Adaptörü](#arduino-mcp2515-slcan-adaptörü)
  - [İçindekiler](#i̇çindekiler)
  - [Donanım](#donanım)
  - [Firmware](#firmware)
  - [Kurulum ve Bağlantı](#kurulum-ve-bağlantı)
    - [1. Firmware'i yükle](#1-firmwarei-yükle)
    - [2. can-utils kur](#2-can-utils-kur)
  - [socat + slcand neden gerekli](#socat--slcand-neden-gerekli)
    - [Çalıştırma](#çalıştırma)
  - [Kullanım Örnekleri](#kullanım-örnekleri)
    - [CAN trafiğini izleme](#can-trafiğini-izleme)
    - [Belirli ID aralığını filtreleme (OBD-II cevap ID'leri)](#belirli-id-aralığını-filtreleme-obd-ii-cevap-idleri)
    - [Timestamp'li log alma (SavvyCAN ile analiz için)](#timestampli-log-alma-savvycan-ile-analiz-için)
    - [OBD-II sorgu örnekleri](#obd-ii-sorgu-örnekleri)
  - [Sorun Giderme](#sorun-giderme)

---

## Donanım

- Arduino Uno
- MCP2515 CAN kontrolcü modülü (8 MHz kristal)
- CS pini: **10**
- INT pini: **2**
- SPI: standart Uno pinleri (MOSI 11, MISO 12, SCK 13)

## Firmware

Arduino tarafında çalışan `.ino` dosyası, seri port üzerinden gelen
**SLCAN (LAWICEL)** komutlarını yorumlayıp MCP2515 üzerinden CAN bus'a
aktarır, gelen CAN mesajlarını da aynı formatta seri porta geri yazar.

Desteklenen komutlar:

| Komut | Açıklama |
|-------|----------|
| `O\r` | Kanalı aç |
| `C\r` | Kanalı kapat |
| `Sn\r` | Baud rate ayarla (`n`: 0-8, örn. `S6` = 500 kbps) |
| `tIIILDD...\r` | Standart CAN frame gönder (ID, uzunluk, data) |

Seri port hızı: **115200 baud**.

---

## Kurulum ve Bağlantı

### 1. Firmware'i yükle

Arduino IDE ile `.ino` dosyasını karta yükle (Board: Arduino Uno, doğru
Port seçili olmalı).

### 2. can-utils kur

```bash
sudo apt install can-utils socat
```
Bu script `socat` ve `slcand`'i sırasıyla başlatıp `slcan0` arayüzünü
ayağa kaldırır. Detaylar için aşağıdaki bölüme bakın.

---

## socat + slcand neden gerekli

`slcand`'in doğrudan gerçek TTY cihazına (`/dev/ttyUSB0`, `/dev/ttyACM0`)
bağlanması **denendi ve güvenilir çalışmadı** — `-S` (baud rate) parametresi
ve DTR/RTS ayarları ne şekilde verilirse verilsin, `slcand` karta bağlanıp
kanalı açmasına rağmen komutlar işlenmiyordu.

**Çözüm:** `socat`, gerçek seri portu (doğru baud rate ve raw modda) açıp
bir **PTY**'ye köprüler. `slcand` gerçek donanıma değil, bu PTY'ye bağlanır

```
[Arduino/MCP2515] <--USB (gerçek TTY)--> [socat] <--PTY--> [slcand] <--> [slcan0]
```

### Çalıştırma

```bash
# 1. socat ile gerçek portu PTY'ye köprüle
socat -x -d -d pty,raw,echo=0,b115200 FILE:/dev/ttyUSB0,raw,echo=0,b115200

# Çıktıda şöyle bir satır göreceksin, PTY yolunu not al:
#   PTY is /dev/pts/3

# 2. Başka bir terminalde, slcand'i PTY'ye bağla
sudo slcand -o -c -s6 /dev/pts/3 slcan0

# 3. Arayüzü ayağa kaldır
sudo ip link set up slcan0

# 4. Test et
candump slcan0
```

## Kullanım Örnekleri

### CAN trafiğini izleme

```bash
candump slcan0
```

### Belirli ID aralığını filtreleme (OBD-II cevap ID'leri)

```bash
candump slcan0,7E8:7F8
```

### Timestamp'li log alma (SavvyCAN ile analiz için)

```bash
candump -l slcan0
```

Bu, çalışılan dizinde `candump-<tarih>_<saat>.log` dosyası oluşturur.
Bu dosya SavvyCAN'de **"CANDump"** formatı seçilerek doğrudan
açılabilir.

### OBD-II sorgu örnekleri

```bash
# Desteklenen PID'ler (Mode 01)
cansend slcan0 7DF#0201000000000000

# Motor RPM (PID 0C)
cansend slcan0 7DF#02010C0000000000

# Stored DTC (Mode 03)
cansend slcan0 7DF#0103000000000000

# Pending DTC (Mode 07)
cansend slcan0 7DF#0107000000000000

# MIL durumu / monitor status (Mode 01 PID 01)
cansend slcan0 7DF#0201010000000000
```

Cevaplar `7E8`–`7EF` aralığındaki ID'lerden gelir (araçtaki cevap veren
ECU sayısına göre değişir).

---

## Sorun Giderme

| Belirti | Olası sebep | Çözüm |
|---|---|---|
| Kanal açılıyor ama komutlara cevap yok | `slcand` doğrudan gerçek TTY'ye bağlı | `socat` + PTY yöntemini kullan |
| `candump`'ta hiçbir şey görünmüyor | Yanlış arayüz adı / kanal açılmamış | `ip link show slcan0` ile durumu kontrol et |
| MCP2515 `begin()` başarısız dönüyor | SPI kablo bağlantısı / lehim sorunu | `MCP_LOOPBACK` modunda test ederek SPI zincirini doğrula |
| Loopback testinde hiç veri dönmüyor | Firmware kartta çalışmıyor olabilir | Setup'a benzersiz bir `Serial.println()` debug satırı ekleyip doğrula |
