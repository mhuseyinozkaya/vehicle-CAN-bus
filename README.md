# Arduino MCP2515 SLCAN Adaptörü

Arduino Uno + MCP2515 ile aracın CAN bus'ını Linux SocketCAN arayüzüne
(`slcan0`) bağlayan SLCAN (LAWICEL) protokol implementasyonu.

`candump`, `cansend`, `cansniffer`, SavvyCAN, python-can gibi bütün
standart SocketCAN araçlarıyla doğrudan çalışır.

```
[Araç CAN bus] <--> [MCP2515] <--SPI--> [Arduino] <--USB--> [socat] <--PTY--> [slcand] <--> slcan0
```

---

## İçindekiler

- [Güvenlik uyarısı](#güvenlik-uyarısı)
- [Donanım](#donanım)
- [Hızlı başlangıç](#hızlı-başlangıç)
- [Analiz araçları](#analiz-araçları)
- [Yapılandırma](#yapılandırma)
- [Desteklenen SLCAN komutları](#desteklenen-slcan-komutları)
- [socat + slcand neden gerekli](#socat--slcand-neden-gerekli)
- [Kullanım örnekleri](#kullanım-örnekleri)
- [Performans ve sınırlar](#performans-ve-sınırlar)
- [Geliştirme](#geliştirme)
- [Sorun giderme](#sorun-giderme)
- [Proje yapısı](#proje-yapısı)
- [Dokümantasyon](#dokümantasyon)
- [Lisans](#lisans)

---

## Güvenlik uyarısı

Bir araç CAN bus'ına bağlanmak pasif bir işlem değildir. Normal modda
çalışan bir CAN düğümü, dinlediği her mesajı **ACK'ler** — yani sadece
"izliyorum" derken bile hattı elektriksel olarak sürer. Yanlış bit hızı
veya yanlış kristal ayarıyla bağlanmak, aracın gerçek modüllerinin hata
sayaçlarını yükseltip bus-off durumuna sürükleyebilir.

Bu firmware üç ayrı katmanda koruma sağlar:

1. **Varsayılan listen-only.** `L` komutu kanalı dinleme modunda açar;
   MCP2515 bu modda ACK bile göndermez. `slcand` varsayılan olarak `O`
   (normal mod) kullanır, bu yüzden sadece okuma yapacaksanız
   `SLCAN_READ_ONLY` seçeneğini kullanın.
2. **Derleme zamanı kilidi.** `config.h` içinde `SLCAN_READ_ONLY 1`
   yaparsanız `O` komutu reddedilir ve gerçek bus'a hiçbir frame
   gönderilemez. Host tarafından aşılamaz. (`l` loopback modu çalışmaya
   devam eder — MCP2515 bu modda TXCAN hattını sürmez, dolayısıyla
   adaptörü yeniden yüklemeden test edebilirsiniz.)
3. **Kanal kapalıyken sessizlik.** `C` komutu MCP2515'i configuration
   moduna alır; kontrolcü hattan tamamen çekilir.

Ayrıca:

- Referans toprak olarak **pin 5 (sinyal toprağı)** kullanın, pin 4
  (şase toprağı) değil. Gerekçesi [docs/chevrolet/cruze_2010_16_ls.md](docs/chevrolet/cruze_2010_16_ls.md).
- Araç hareket hâlindeyken bus'a frame **göndermeyin**.
- Emin olmadığınız `cansend` komutlarını gerçek araçta denemeyin.

---

## Donanım

| Bileşen | Detay |
|---|---|
| Kart | Arduino Uno (Nano / Pro Micro da çalışır) |
| CAN kontrolcü | MCP2515 + TJA1050/MCP2551 modül |
| Kristal | 8 MHz (mavi "niren" modüller) veya 16 MHz — `config.h`'de belirtin |
| CS | D10 |
| INT | D2 |
| SPI | D11 (MOSI), D12 (MISO), D13 (SCK) |

**Gerekli kütüphane:** [coryjfowler/MCP_CAN_lib](https://github.com/coryjfowler/MCP_CAN_lib)
(Arduino IDE → Library Manager → "mcp_can" by Cory J. Fowler).

> **MCP2515 modülünün üzerindeki 120 Ω sonlandırma direncini sökün.**
> Araç bus'ı zaten iki ucundan sonlandırılmıştır; üçüncü bir direnç
> toplam direnci 60 Ω'dan 40 Ω'a düşürür ve hattı bozar. En sık yapılan
> hata budur.

Kablolama şeması, malzeme listesi, ölçüm değerleri ve adım adım devreye
alma kontrol listesi: **[docs/hardware.md](docs/hardware.md)**.

---

## Hızlı başlangıç

### 1. Firmware'i yükle

Arduino IDE ile:

```
File → Open → slcan_firmware_uno/slcan_firmware_uno.ino
Tools → Board: Arduino Uno
Tools → Port: /dev/ttyACM0 (veya /dev/ttyUSB0)
Upload
```

Komut satırından:

```bash
make upload PORT=/dev/ttyUSB0     # arduino-cli gerektirir
# veya
pio run -e uno -t upload          # PlatformIO gerektirir
```

### 2. Bağımlılıkları kur

```bash
sudo apt install can-utils socat iproute2
```

### 3. Arayüzü ayağa kaldır

```bash
sudo ./scripts/slcan-up.sh
```

Script cihazı otomatik bulur, `socat` köprüsünü kurar, `slcand`'i
bağlar ve `slcan0` arayüzünü açar. Seçenekler:

```bash
sudo ./scripts/slcan-up.sh -d /dev/ttyUSB0 -b 115200 -s 6 -i slcan0
#                          ^cihaz          ^seri baud  ^CAN hızı ^arayüz adı
```

**Bit hızını bilmiyorsanız** `-a` verin — firmware bütün hızları
listen-only modda tarayıp doğrusunu bulur (~2 saniye, bus'a hiç
dokunmadan):

```bash
sudo ./scripts/slcan-up.sh -a
```

Kapatmak için:

```bash
sudo ./scripts/slcan-down.sh
```

### 4. Doğrula

```bash
candump slcan0
```

---

## Analiz araçları

Adaptör sadece veriyi taşır; asıl iş o verinin ne anlama geldiğini
bulmaktır. `tools/` altında iki araç var, ikisi de sadece Python 3
standart kütüphanesini kullanır.

### `candiff.py` — hangi mesaj neyi taşıyor?

Tersine mühendisliğin temel iş akışı: hiçbir şey yapmadan bir log al,
sonra tek bir eylemi yaparken ikinci logu al, ikisini karşılaştır.

```bash
candump -l slcan0            # 1. baseline, Ctrl-C ile bitir
candump -l slcan0            # 2. cam düğmesine basarken al

./tools/candiff.py diff candump-1.log candump-2.log
```

```
== Sadece aday logda gorulen ID'ler ==
ID   adet  periyot
------------------
4B1  9        10ms

== Baseline'da sabit, adayda degisen baytlar ==
   (aradiginiz sinyal buyuk ihtimalle burada)
ID   bayt  baseline  adaydaki degerler  degisen bitler  n
---------------------------------------------------------
3B3  1     00        00 04              .....1..        2
```

Diğer alt komutlar:

```bash
./tools/candiff.py summary kayit.log              # ID başına istatistik
./tools/candiff.py watch kayit.log --id 3B3 --changes-only
```

### `uds.sh` — çok çerçeveli teşhis

VIN, arıza kodları ve ECU kimliği tek CAN çerçevesine sığmaz; ISO-TP
taşıma katmanı gerekir. Bu, Linux'un `can-isotp` modülüyle yapılır —
firmware'e dokunmadan.

```bash
./tools/uds.sh scan          # cevap veren ECU'ları bul
./tools/uds.sh vin           # araç kimlik numarası
./tools/uds.sh dtc           # arıza kodları, durum bitleriyle
./tools/uds.sh ident         # parça no, yazılım sürümü
```

Yazma ve kontrol servisleri (ECUReset, WriteDataByIdentifier,
SecurityAccess, arıza silme…) `-f` verilmedikçe **reddedilir**.

Ayrıntı: **[docs/iso_tp_uds.md](docs/iso_tp_uds.md)**.

---

## Yapılandırma

Ayarlanabilir her şey [`slcan_firmware_uno/config.h`](slcan_firmware_uno/config.h)
içindedir; başka hiçbir dosyayı düzenlemeniz gerekmez.

| Ayar | Varsayılan | Açıklama |
|---|---|---|
| `MCP2515_CS_PIN` | `10` | SPI chip-select pini |
| `MCP2515_INT_PIN` | `2` | MCP2515 `/INT` pini |
| `MCP2515_CRYSTAL` | `MCP_8MHZ` | **Modülünüzdeki kristal.** Yanlışsa hiç veri gelmez |
| `SERIAL_BAUDRATE` | `115200` | Seri hız — script'e `-b` ile aynısını verin |
| `SLCAN_READ_ONLY` | `0` | `1` → donanım seviyesinde salt-okunur adaptör |
| `SLCAN_TX_QUEUE_LEN` | `8` | Gönderim kuyruğu derinliği (slot başına 16 bayt SRAM) |
| `CAN_RX_DRAIN_MAX` | `4` | Bir `loop()` turunda okunacak azami frame sayısı |
| `SLCAN_NONBLOCKING_TX` | `1` | Host okumayı bırakırsa bloklamak yerine frame düşür |
| `SLCAN_AUTODETECT` | `1` | Bit hızı tarayıcısı (`B` komutu). `0` → ~400 bayt flash tasarrufu |
| `SLCAN_AUTODETECT_DWELL_MS` | `200` | Her aday hızda dinleme süresi |

---

## Desteklenen SLCAN komutları

Bütün komutlar `\r` (CR) ile biter. Cevap: `\r` = OK, `\a` (BEL) = hata.

| Komut | Açıklama |
|---|---|
| `S0`..`S8` | Bit hızı: 10, 20, 50, 100, 125, 250, 500, *(yok)*, 1000 kbit/s |
| `O` | Kanalı **normal** modda aç (bus'ı sürer, ACK gönderir) |
| `L` | Kanalı **listen-only** modda aç (hattı hiç sürmez) |
| `l` | Kanalı **loopback** modda aç (self-test, bus'tan tamamen izole) |
| `C` | Kanalı kapat, kontrolcüyü configuration moduna al |
| `tIIILDD..` | Standart data frame gönder (3 hane ID, DLC, veri) |
| `TIIIIIIIILDD..` | Extended data frame gönder (8 hane ID) |
| `rIIIL` | Standart remote (RTR) frame gönder |
| `RIIIIIIIIL` | Extended remote frame gönder |
| `F` | Durum bayraklarını oku (`Fxx\r`), okuyunca sıfırlanır |
| `Mxxxxxxxx` | Acceptance code (kanal kapalıyken) |
| `mxxxxxxxx` | Acceptance mask — `1` = "önemsiz" (SJA1000 semantiği) |
| `Z0` / `Z1` | Timestamp kapalı / açık |
| `V` | Donanım + yazılım sürümü |
| `v` | Yazılım sürümü |
| `N` | Seri numarası |
| `B` | **Bit hızı tara** (standart dışı uzantı) — aşağıya bakın |

### Bit hızı tarayıcısı (`B`)

Standart SLCAN'de yok; bu firmware'in eklentisi. Desteklenen her hızı
sırayla dener ve trafiği en temiz aldığı hızı bildirir.

```
B\r        ->  B6\r     (500 kbit/s tespit edildi)
           ->  \a       (hiçbir hızda trafik yok)
```

Nasıl çalışır: her aday hızda 200 ms **listen-only** modda dinler. Bu
mod kritik — yanlış bit zamanlamasıyla hattı sürmek, aracın gerçek
modüllerini bus-off durumuna sokabilecek en hızlı yoldur. Listen-only'de
MCP2515 ACK bile göndermez, sadece hata sayaçlarını biriktirir; doğru
hızda sayaçlar temiz kalır ve frame'ler çözülür. Karar bu iki sinyale
göre verilir.

Tarama sırasında `C` dışındaki bütün komutlar reddedilir (`C` taramayı
iptal eder). Tarama başarısız olursa adaptör **yapılandırılmamış**
bırakılır — böylece host yanlışlıkla son denenen hızda kanal açamaz.

`scripts/slcan-up.sh -a` bu komutu sizin için kullanır.

**Desteklenmeyenler ve nedenleri:**

- `S7` (800 kbit/s): MCP2515 sürücüsünde bu hız için bit-timing tablosu
  yok. Sessizce başka bir hıza düşmek yerine hata döner — yanlış zamanlamalı
  bir düğüm gerçek araç bus'ında ciddi hasar verebilir.
- `sXXXX` (ham BTR0/BTR1): Bu register'lar SJA1000'e özgüdür, MCP2515'in
  bit-timing yapısı tamamen farklıdır. Aynı gerekçeyle reddedilir.

### Donanım filtresi (`M` / `m`)

SLCAN, SJA1000 semantiğini miras alır: maske biti **`1` = önemsiz**.
MCP2515 ise tam tersini kullanır (`1` = eşleşmeli). Firmware bu çevrimi
otomatik yapar — SLCAN semantiğiyle yazın, gerisi halledilir.

Sadece OBD-II cevaplarını (`7E8`) almak için:

```
M000007E8    # acceptance code
m00000000    # hiçbir bit önemsiz değil -> tam eşleşme
```

Filtreleme silikonda yapıldığı için, gereksiz frame'ler seri porta hiç
çıkmaz. Yoğun bir bus'ta tek etkili hızlanma yöntemi budur.

---

## socat + slcand neden gerekli

`slcand`'in doğrudan gerçek TTY cihazına (`/dev/ttyUSB0`, `/dev/ttyACM0`)
bağlanması **denendi ve güvenilir çalışmadı**: `-S` (baud) parametresi ve
DTR/RTS ayarları ne şekilde verilirse verilsin, `slcand` kanalı açtığını
bildirdiği hâlde komutlar işlenmiyordu. Sebep, Arduino'nun DTR ile
tetiklenen otomatik reset devresinin `slcand`'in port açma sırasıyla
çakışması.

**Çözüm:** `socat` gerçek seri portu raw modda açıp bir PTY'ye köprüler.
`slcand` donanıma değil bu PTY'ye bağlanır, dolayısıyla DTR'ye hiç
dokunmaz.

```
[Arduino/MCP2515] <--USB (gerçek TTY)--> [socat] <--PTY--> [slcand] <--> [slcan0]
```

`scripts/slcan-up.sh` bunu sizin için yapar. Elle yapmak isterseniz:

```bash
# 1. socat ile gerçek portu sabit yollu bir PTY'ye köprüle
socat -d pty,raw,echo=0,link=/run/slcan-pty,b115200 \
         /dev/ttyUSB0,raw,echo=0,b115200,nonblock &

# 2. Kartın reset'ini bitirmesini bekle (bootloader ~2 sn)
sleep 2

# 3. slcand'i PTY'ye bağla
sudo slcand -o -c -f -s6 /run/slcan-pty slcan0

# 4. Arayüzü ayağa kaldır
sudo ip link set slcan0 txqueuelen 1000
sudo ip link set slcan0 up
```

---

## Kullanım örnekleri

### Trafiği izleme

```bash
candump slcan0                    # her şey
candump slcan0,7E8:7FF            # sadece OBD-II cevapları
candump -c -c slcan0              # renkli
cansniffer slcan0                 # değişen baytları vurgular
```

### SavvyCAN için log alma

```bash
candump -l slcan0
```

Çalışılan dizinde `candump-<tarih>_<saat>.log` oluşur; SavvyCAN'de
**"CANDump"** formatıyla açılır.

### OBD-II sorguları

> Bunlar bus'a **yazar**. Aracın kontağı açık, motoru kapalı ve araç
> hareketsizken deneyin.

```bash
# Mode 01 PID 00 - desteklenen PID'ler
cansend slcan0 7DF#0201000000000000

# Mode 01 PID 0C - motor devri
cansend slcan0 7DF#02010C0000000000

# Mode 01 PID 01 - MIL durumu / monitor status
cansend slcan0 7DF#0201010000000000

# Mode 03 - kayıtlı arıza kodları
cansend slcan0 7DF#0103000000000000

# Mode 07 - bekleyen arıza kodları
cansend slcan0 7DF#0107000000000000
```

Cevaplar `7E8`–`7EF` aralığındaki ID'lerden gelir (cevap veren ECU
sayısına göre değişir). Ayrıntılı PID listesi:
[docs/ford/obd_ii_reference.md](docs/ford/obd_ii_reference.md).

### Arayüz istatistikleri

```bash
ip -details -statistics link show slcan0
```

---

## Performans ve sınırlar

SLCAN bir **ASCII** protokolüdür. 8 baytlık standart bir frame seri
portta 26 karakter yer kaplar; 8N1 çerçevelemeyle karakter başına 10 bit
gider:

| Seri hız | Teorik azami frame/s |
|---|---|
| 115200 | ~443 |
| 500000 | ~1923 |
| 1000000 | ~3846 |

Yoğun bir 500 kbit/s araç bus'ı rahatlıkla 2000+ frame/s üretir.
**Darboğaz CAN kontrolcüsü değil, USB seri hattıdır.** Seçenekler:

1. `config.h`'de `SERIAL_BAUDRATE`'i `500000UL` yapın ve script'e
   `-b 500000` verin. 16 MHz Uno'da 500000 baud tam bölünür (%0.0 hata),
   115200'den (%-3.5, U2X ile düzeltilir) daha temizdir.
2. `M`/`m` ile donanım filtresi kurun — ilgilenmediğiniz frame'ler seri
   porta hiç çıkmaz.
3. Native USB'li bir kart kullanın (Pro Micro / Leonardo, 32u4): CDC
   üzerinden UART sınırı ortadan kalkar.

Frame kaybı olduğunda firmware bunu gizlemez: `F` komutunun cevabındaki
data-overrun (`0x08`) ve RX-full (`0x01`) bitleri kurulur.

---

## Geliştirme

Firmware'in protokol mantığı Arduino'ya bağımlı değildir ve normal bir
bilgisayarda test edilebilir:

```bash
make test            # her şey (firmware + araçlar)
make test-firmware   # C++ codec, protokol ve güvenlik kilidi testleri
make test-tools      # Python araç testleri
make build           # arduino-cli ile derle
make clean
```

Entegrasyon testleri gerçek `slcan_protocol.cpp` ve `can_iface.cpp`
dosyalarını, sahte bir Arduino/MCP2515 HAL'i (`test/mocks/`) üzerinde
çalıştırır. `regression:` ile başlayan testlerin her biri, eski tek
dosyalık firmware'de bulunan somut bir hatayı sabitler.

Her push'ta CI şunları yapar: C++ testleri, iki farklı Python sürümünde
araç testleri, `arduino:avr:uno` ve `arduino:avr:nano` için derleme,
salt-okunur ve tarayıcısız yapılandırmalarla ek derlemeler, shell script
lint'i.

---

## Sorun giderme

| Belirti | Olası sebep | Çözüm |
|---|---|---|
| `S6` komutuna `\a` (BEL) dönüyor | MCP2515 SPI üzerinden cevap vermiyor | Kabloları ve CS pinini kontrol edin; `MCP2515_CRYSTAL` ayarını doğrulayın |
| Kanal açılıyor ama `candump` boş | Yanlış bit hızı | `sudo ./scripts/slcan-up.sh -a` ile otomatik tespit ettirin |
| Kanal açılıyor ama `candump` boş | Yanlış kristal ayarı | `MCP2515_CRYSTAL`'i 8 MHz ↔ 16 MHz değiştirip deneyin |
| `-a` "no bus traffic" diyor | Hat sessiz veya yanlış pinlerdesiniz | Kontağı açın; pin 6/14 (HS) veya 3/11 (Ford MS) |
| `candump` boş, hat da doğru | Yanlış OBD pinleri | HS-CAN: pin 6/14. Ford MS-CAN: pin 3/11 ([detay](docs/ford/obd_ii_reference.md)) |
| Bağlantı kurulmuyor, `slcand` cevap alamıyor | `slcand` doğrudan gerçek TTY'ye bağlı | `scripts/slcan-up.sh` kullanın (socat + PTY) |
| İlk komutlar yutuluyor | Port açılırken kart reset atıyor | Port açtıktan sonra 2 sn bekleyin (script bunu yapar) |
| Rastgele bozuk frame'ler | Seri hız uyuşmazlığı | `config.h`'deki `SERIAL_BAUDRATE` ile `-b` parametresi aynı olmalı |
| Frame kaybı, `F` → `09` | Seri port darboğazı | Baud'u yükseltin veya `M`/`m` ile filtreleyin |
| Araçtaki modüller hata veriyor | Adaptör yanlış hızda bus'ı sürüyor | Derhal çıkarın; `SLCAN_READ_ONLY 1` ile yeniden yükleyin |
| Firmware'in çalıştığından emin değilim | — | `l` (loopback) modunda açıp `cansend` edin; gönderdiğiniz frame geri gelmeli |

### Kendi kendine test

MCP2515'i araca hiç bağlamadan bütün zinciri doğrulayabilirsiniz:

```bash
# Terminal 1
sudo ./scripts/slcan-up.sh
candump slcan0

# Terminal 2 - loopback modunda gönderilen frame geri gelir
cansend slcan0 123#DEADBEEF
```

Loopback modu için `slcand` yerine doğrudan seri terminalden `C`, `S6`,
`l`, `t1234DEADBEEF` komutlarını da gönderebilirsiniz.

---

## Proje yapısı

```
.
├── slcan_firmware_uno/
│   ├── slcan_firmware_uno.ino   Modülleri birbirine bağlar (setup/loop)
│   ├── config.h                 Ayarlanabilir her şey
│   ├── slcan_codec.h/.cpp       ASCII <-> CAN frame çevrimi (HAL'siz, test edilebilir)
│   ├── can_iface.h/.cpp         MCP2515 sarmalayıcı + kanal durum makinesi
│   └── slcan_protocol.h/.cpp    Komut yorumlayıcı, frame pompası, bit hızı tarayıcı
├── scripts/
│   ├── slcan-up.sh              socat + slcand + ip link, tek komutta
│   └── slcan-down.sh            Temiz kapatma
├── tools/
│   ├── candiff.py               candump loglarını karşılaştır (sinyal bulma)
│   ├── uds.sh                   ISO-TP üzerinden UDS/OBD sorguları
│   └── udsdecode.py             UDS cevaplarını çözümle (VIN, DTC, NRC)
├── test/
│   ├── test_slcan_codec.cpp     Codec unit testleri
│   ├── test_slcan_protocol.cpp  Protokol entegrasyon testleri
│   ├── test_readonly_build.cpp  Güvenlik kilidi testleri
│   ├── test_candiff.py          Log analiz testleri
│   ├── test_udsdecode.py        UDS çözümleyici testleri
│   └── mocks/                   Sahte Arduino / MCP2515 HAL
├── docs/                        Donanım, ISO-TP/UDS ve araç notları
├── Makefile                     make test / build / upload
└── platformio.ini               PlatformIO yapılandırması
```

---

## Dokümantasyon

| Belge | İçerik |
|---|---|
| [docs/hardware.md](docs/hardware.md) | Malzeme listesi, kablolama, 120 Ω tuzağı, devreye alma kontrol listesi |
| [docs/iso_tp_uds.md](docs/iso_tp_uds.md) | ISO-TP taşıma katmanı, UDS servisleri, DTC çözme, güvenlik |
| [docs/chevrolet/cruze_2010_16_ls.md](docs/chevrolet/cruze_2010_16_ls.md) | Chevrolet Cruze 2010 1.6 LS — OBD pin dizilimi |
| [docs/ford/obd_ii_reference.md](docs/ford/obd_ii_reference.md) | Ford HS-CAN / MS-CAN, OBD-II PID referansı |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Katkı kuralları ve kod stili |

---

## Lisans

MIT — bkz. [LICENSE](LICENSE).

Orijinal yazar: **Muhammed Hüseyin Özkaya**.
