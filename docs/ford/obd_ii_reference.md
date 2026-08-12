# Ford — HS-CAN / MS-CAN ve OBD-II referansı

## İki ayrı CAN ağı

Ford araçlarda birbirinden **fiziksel olarak ayrı** iki CAN ağı bulunur.

| Ağ | Hız | OBD-II pinleri (H / L) | Üzerinde ne var |
|---|---|---|---|
| **HS-CAN** (High Speed) | 500 kbit/s | pin 6 / pin 14 | Motor kontrol, devir, şanzıman, ABS, direksiyon — aracın kritik işlevleri |
| **MS-CAN** (Medium Speed) | 125 kbit/s | pin 3 / pin 11 | Klima, kapılar, camlar, radyo, gösterge paneli — konfor ve gövde sistemleri |

> **Not:** Ağlar fiziksel olarak ayrı olduğu için hem HS-CAN hem de
> MS-CAN üzerinde **aynı CAN ID'ye sahip, tamamen farklı anlamdaki**
> paketler bulunabilir. Bir logu yorumlarken hangi ağdan alındığını
> mutlaka not edin.

Bu adaptör tek seferde tek bir ağa bağlanır. İki ağı birlikte izlemek
için iki ayrı Arduino + MCP2515 seti gerekir.

## Bağlantı

```
HS-CAN:  MCP2515 CANH -> OBD pin 6      MCP2515 CANL -> OBD pin 14
MS-CAN:  MCP2515 CANH -> OBD pin 3      MCP2515 CANL -> OBD pin 11
Toprak:  MCP2515 GND  -> OBD pin 5      (sinyal toprağı — pin 4 değil)
Besleme: MCP2515 VCC  -> OBD pin 16 üzerinden regüle edilmiş 5 V
```

Bit hızı seçimi:

```bash
sudo ./scripts/slcan-up.sh -s 6    # HS-CAN, 500 kbit/s
sudo ./scripts/slcan-up.sh -s 4    # MS-CAN, 125 kbit/s
```

## Pin görseli

![Ford Galaxy OBD-II konnektör pin dizilimi](../images/ford_galaxy_pins.jpg)

## OBD-II sorgu formatı

Standart OBD-II (ISO 15765-4) tek çerçeveli istek:

```
7DF # LL MM PP 00 00 00 00 00
      │  │  └── PID
      │  └───── Mode (servis)
      └──────── Ek uzunluğu (mode + PID sayısı)
```

`7DF` fonksiyonel (broadcast) adrestir; cevaplar `7E8`–`7EF`
aralığındaki fiziksel adreslerden gelir.

### Sık kullanılan Mode 01 PID'leri

| PID | Anlam | Formül (A, B = veri baytları) |
|---|---|---|
| `00` | Desteklenen PID'ler (01–20) | bit maskesi |
| `01` | Monitor status / MIL | A bit7 = MIL, A&0x7F = DTC sayısı |
| `04` | Hesaplanan motor yükü | A × 100 / 255 % |
| `05` | Soğutma suyu sıcaklığı | A − 40 °C |
| `0C` | Motor devri | (256A + B) / 4 rpm |
| `0D` | Araç hızı | A km/h |
| `0F` | Emme havası sıcaklığı | A − 40 °C |
| `10` | MAF hava debisi | (256A + B) / 100 g/s |
| `11` | Gaz kelebeği konumu | A × 100 / 255 % |
| `1F` | Motor çalışma süresi | 256A + B saniye |
| `2F` | Yakıt seviyesi | A × 100 / 255 % |
| `42` | Kontrol modülü gerilimi | (256A + B) / 1000 V |

### Modlar

| Mode | Açıklama |
|---|---|
| `01` | Anlık veri |
| `02` | Freeze frame verisi |
| `03` | Kayıtlı arıza kodları (DTC) |
| `04` | DTC'leri sil |
| `06` | Test sonuçları |
| `07` | Bekleyen DTC'ler |
| `09` | Araç bilgisi (VIN vb.) |
| `0A` | Kalıcı DTC'ler |

### Örnek

```bash
# İstek: motor devri
cansend slcan0 7DF#02010C0000000000

# Cevap (candump çıktısı):
#   slcan0  7E8  [8]  04 41 0C 1A F8 00 00 00
#                      │  │  │  └──┴── 0x1AF8 = 6904 -> 6904/4 = 1726 rpm
#                      │  │  └─────── PID 0C
#                      │  └────────── 0x41 = 0x40 + mode 01 (cevap)
#                      └───────────── uzunluk
```

## Kaynaklar

- [OBD-II PIDs — Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [ISO 15765-2 (CAN üzerinden taşıma katmanı)](https://en.wikipedia.org/wiki/ISO_15765-2)
