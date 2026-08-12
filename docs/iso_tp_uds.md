# ISO-TP ve UDS — tek çerçevenin ötesi

README'deki OBD-II örnekleri tek CAN çerçevesine sığan sorgulardı: 8 bayt
istek, 8 bayt cevap. Gerçek teşhis bunun ötesindedir. 17 karakterlik bir
VIN, üç haneli arıza kodu listesi veya ECU parça numarası tek çerçeveye
sığmaz — bunlar için **ISO 15765-2 (ISO-TP)** taşıma katmanı gerekir.

Bu katman firmware'de değil, Linux çekirdeğinde çalışır. Adaptörde
hiçbir değişiklik gerekmez.

---

## İçindekiler

- [Kavramlar](#kavramlar)
- [Kurulum](#kurulum)
- [tools/uds.sh kullanımı](#toolsudssh-kullanımı)
- [Elle ISO-TP](#elle-iso-tp)
- [Adresleme](#adresleme)
- [Sık kullanılan servisler](#sık-kullanılan-servisler)
- [Arıza kodlarını okumak](#arıza-kodlarını-okumak)
- [Negatif cevaplar](#negatif-cevaplar)
- [SLCAN'a özgü tuzaklar](#slcana-özgü-tuzaklar)
- [Güvenlik](#güvenlik)

---

## Kavramlar

ISO-TP, 8 baytlık CAN çerçeveleri üzerinde 4095 bayta kadar mesaj taşır.
Dört çerçeve tipi vardır:

| Tip | İlk bayt | Anlamı |
|---|---|---|
| **SF** Single Frame | `0L` | Tek çerçeve, `L` = uzunluk (≤7) |
| **FF** First Frame | `1L LL` | Çok çerçeveli mesajın başı, 12 bit uzunluk |
| **CF** Consecutive Frame | `2N` | Devam çerçevesi, `N` = 0-F sıra numarası |
| **FC** Flow Control | `30 BS ST` | "Devam et", blok boyutu ve minimum ara |

VIN sorgusunun kablodaki görüntüsü:

```
7E0  02 09 02 00 00 00 00 00     tester -> ECU   : SF, "09 02"
7E8  10 14 49 02 01 31 47 31     ECU -> tester   : FF, toplam 0x14 = 20 bayt
7E0  30 00 00 00 00 00 00 00     tester -> ECU   : FC, "hepsini gönder"
7E8  21 43 31 50 43 35 53 42     ECU -> tester   : CF #1
7E8  22 34 45 37 31 32 33 34     ECU -> tester   : CF #2
7E8  23 35 36 00 00 00 00 00     ECU -> tester   : CF #3
```

Flow control çerçevesini **tester gönderir** — yani sizin tarafınız.
Adaptörünüzün gecikmesi bu yüzden önemlidir; aşağıdaki
[SLCAN tuzakları](#slcana-özgü-tuzaklar) bölümüne bakın.

Bu koreografiyi elle yönetmek gereksizdir: Linux'un `can-isotp`
çekirdek modülü hepsini halleder ve size düz bir bayt dizisi verir.

---

## Kurulum

`can-isotp` Linux 5.10'dan beri çekirdekte mevcuttur.

```bash
sudo modprobe can-isotp
lsmod | grep can_isotp        # yüklendiğini doğrula
sudo apt install can-utils    # isotpsend, isotprecv, isotpdump
```

Kalıcı hale getirmek için:

```bash
echo can-isotp | sudo tee /etc/modules-load.d/can-isotp.conf
```

Çekirdeğiniz eskiyse alternatif [hartkopp/can-isotp](https://github.com/hartkopp/can-isotp)
deposundan DKMS modülü olarak kurulabilir.

---

## tools/uds.sh kullanımı

```bash
sudo ./scripts/slcan-up.sh          # slcan0 ayakta olmalı

./tools/uds.sh scan                 # cevap veren ECU'ları bul
./tools/uds.sh vin                  # araç kimlik numarası
./tools/uds.sh dtc                  # kayıtlı arıza kodları
./tools/uds.sh ident                # ECU parça no, yazılım sürümü
./tools/uds.sh raw 22F190           # ham istek
```

Farklı bir ECU'ya sormak için adres çiftini verin:

```bash
./tools/uds.sh -t 7E1 -r 7E9 ident   # ikinci ECU (genelde şanzıman)
```

Örnek çıktı:

```
arayuz: slcan0   tester: 0x7E0 -> ECU: 0x7E8

== Kayitli ariza kodlari (UDS 19 02) ==
  2 ariza kodu:
    P0301
      durum 0x09: testFailed (su an ariza var), confirmedDTC (onaylanmis)
    P0420
      durum 0x08: confirmedDTC (onaylanmis)
```

Cevapları elle çözmek isterseniz çözümleyici tek başına da çalışır:

```bash
./tools/udsdecode.py dtc 5902FF03010009
./tools/udsdecode.py raw 7F2231
```

---

## Elle ISO-TP

Script'e güvenmek istemiyorsanız iki terminalde:

```bash
# Terminal 1 — dinle
isotprecv -s 7E0 -d 7E8 slcan0

# Terminal 2 — sor (VIN)
echo "22 F1 90" | isotpsend -s 7E0 -d 7E8 slcan0
```

`-s` gönderdiğiniz ID, `-d` dinlediğiniz ID. Her iki araçta da aynı
çifti kullanın.

Ham ISO-TP trafiğini çerçeve çerçeve görmek için:

```bash
isotpdump -s 7E0 -d 7E8 -c -a slcan0
```

---

## Adresleme

OBD-II standardı 11-bit adresleri sabitler:

| Tester → ECU | ECU → Tester | Tipik ECU |
|---|---|---|
| `7DF` | `7E8`-`7EF` | fonksiyonel yayın (hepsi cevaplar) |
| `7E0` | `7E8` | motor kontrol (ECM/PCM) |
| `7E1` | `7E9` | şanzıman (TCM) |
| `7E2` | `7EA` | — |
| `7E3` | `7EB` | — |

Kural: cevap ID'si = istek ID'si + 8.

Üretici-özel modüller (gövde, ABS, airbag) genellikle 29-bit adresler
kullanır:

```
18DA<hedef><kaynak>     ISO 15765-4 normal fixed addressing

18DAF110   tester (F1) -> ECU 0x10
18DA10F1   ECU 0x10    -> tester (F1)
```

```bash
./tools/uds.sh -t 18DAF110 -r 18DA10F1 ident
```

`scan` komutu sadece 7E0-7E7 aralığına bakar; üretici modüllerini bulmak
için `candump` ile pasif dinleyip hangi ID'lerin konuştuğunu görmek daha
verimlidir.

---

## Sık kullanılan servisler

| Servis | Ad | Örnek istek | Ne yapar |
|---|---|---|---|
| `10` | DiagnosticSessionControl | `1001` | Oturum aç (01 varsayılan, 03 genişletilmiş) |
| `3E` | TesterPresent | `3E00` | "Buradayım" — en zararsız istek |
| `22` | ReadDataByIdentifier | `22F190` | Bir DID oku |
| `19` | ReadDTCInformation | `1902FF` | Arıza kodlarını listele |
| `09` | (OBD) araç bilgisi | `0902` | VIN, kalibrasyon kimliği |
| `03` | (OBD) kayıtlı DTC | `03` | Basit DTC listesi |
| `01` | (OBD) anlık veri | `010C` | Devir, hız, sıcaklık |

**Faydalı DID'ler** (`22 XX XX`):

| DID | İçerik |
|---|---|
| `F186` | Aktif tanılama oturumu |
| `F187` | Üretici parça numarası |
| `F189` | Yazılım sürüm numarası |
| `F18C` | ECU seri numarası |
| `F190` | VIN |
| `F195` | Tedarikçi yazılım sürümü |

`ident` komutu bunların hepsini sırayla sorar ve cevap verenleri
gösterir. Desteklenmeyenler sessizce atlanır.

---

## Arıza kodlarını okumak

DTC kodları 2 bit harf + 2 bit ilk rakam + kalan haneler şeklinde
kodlanır:

```
0x0301  ->  0b00 00 0011 0000 0001
             │  │
             │  └── ilk rakam: 0 (jenerik) / 1 (üretici)
             └───── P=powertrain, C=chassis, B=body, U=network
        ->  P0301  (silindir 1 tekleme)
```

UDS `19 02` cevabı her koda bir de **durum baytı** ekler:

| Bit | Anlamı |
|---|---|
| `0x01` | testFailed — şu anda arıza var |
| `0x04` | pendingDTC — bekleyen, henüz onaylanmadı |
| `0x08` | confirmedDTC — onaylanmış, kalıcı kayıt |
| `0x80` | MIL (motor arıza lambası) yanıyor |

`udsdecode.py` bu bitleri metne çevirir.

> Arıza kodlarını **silmek** (`14` veya OBD mode `04`) `uds.sh`
> tarafından varsayılan olarak reddedilir. Silmek, aracın hazırlık
> (readiness) monitörlerini de sıfırlar ve muayeneden kalmanıza yol
> açabilir; ayrıca gerçek arızanın kanıtını yok eder.

---

## Negatif cevaplar

ECU isteği reddettiğinde `7F <servis> <sebep>` döner:

| Kod | Anlamı | Ne yapmalı |
|---|---|---|
| `11` | Servis desteklenmiyor | ECU bu servisi bilmiyor |
| `31` | İstek aralığı dışında | Bu DID/PID bu ECU'da yok |
| `33` | Güvenlik erişimi reddedildi | Genişletilmiş oturum + SecurityAccess gerekir |
| `22` | Koşullar uygun değil | Motor çalışıyor olabilir, kontağı açıp motoru kapatın |
| `78` | Cevap bekleniyor | ECU çalışıyor, biraz daha bekleyin |
| `7F` | Bu oturumda desteklenmiyor | Önce `1003` ile genişletilmiş oturum açın |

`0x78` özeldir: ECU "meşgulüm, cevap gelecek" der. `-w` ile bekleme
süresini artırın.

---

## SLCAN'a özgü tuzaklar

**Flow control gecikmesi.** Çok çerçeveli bir cevapta flow control'ü
tester gönderir. USB seri hattı üzerinden gidiş-dönüş gecikmesi
115200 baud'da milisaniyeler mertebesindedir. Bazı ECU'lar `STmin`
beklemeden art arda çerçeve gönderir ve adaptörün seri tamponu taşar.
Belirti: uzun cevaplar yarım gelir.

Çözüm sırasıyla:

1. `config.h`'de `SERIAL_BAUDRATE`'i `500000UL` yapın.
2. Flow control'de daha büyük blok boyutu isteyin:
   ```bash
   isotprecv -s 7E0 -d 7E8 -b 0 slcan0     # BS=0: hepsini kesintisiz gönder
   ```
3. Donanım filtresiyle diğer trafiği kesin — ISO-TP oturumu sırasında
   aracın normal trafiği seri hattı doldurur:
   ```
   M000007E8
   m00000000
   ```
   (`uds.sh` bunu otomatik yapmaz; `slcan-up.sh` öncesi elle uygulanır.)

**Zamanlama.** ISO-TP `N_Ar`/`N_Br` zaman aşımları tipik olarak 1000 ms
civarındadır. Adaptörün gecikmesi bunun çok altında kalır, ama araç
hareket hâlindeyken bus doluysa marj daralır. Teşhis işlemlerini araç
duruyorken yapın.

---

## Güvenlik

`uds.sh` aşağıdaki servisleri `-f` verilmedikçe reddeder:

| Servis | Neden tehlikeli |
|---|---|
| `11` ECUReset | ECU'yu yeniden başlatır; sürüş sırasında ölümcül |
| `14` ClearDiagnosticInformation | Arıza kayıtlarını siler |
| `27` SecurityAccess | Deneme sayacını doldurup ECU'yu kilitleyebilir |
| `28` CommunicationControl | ECU'yu bus'tan koparabilir |
| `2E` WriteDataByIdentifier | ECU'ya kalıcı veri yazar |
| `2F` InputOutputControl | Aktüatörleri doğrudan sürer (fren, kilit, fan) |
| `31` RoutineControl | Üretici rutini çalıştırır |
| `34`-`37` | ECU yazılımını değiştirir — geri dönüşü olmayabilir |
| `85` ControlDTCSetting | Arıza kaydını durdurur |

`27` (SecurityAccess) özellikle risklidir: çoğu ECU birkaç başarısız
denemeden sonra kendini belirli bir süre kilitler, bazıları kalıcı bir
sayaç tutar.

**Temel kurallar:**

- Kendi aracınızda, kontak açık / motor kapalı / araç hareketsiz.
- Trafikte veya kamuya açık yolda asla.
- Başkasının aracında izinsiz asla.
- Önce `candump` ile pasif dinleyin; ne olduğunu anlamadan yazmayın.
- Bir işlemin ne yaptığından emin değilseniz yapmayın.

---

## Kaynaklar

- [ISO 15765-2 — Wikipedia](https://en.wikipedia.org/wiki/ISO_15765-2)
- [Unified Diagnostic Services — Wikipedia](https://en.wikipedia.org/wiki/Unified_Diagnostic_Services)
- [OBD-II PIDs — Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [hartkopp/can-isotp](https://github.com/hartkopp/can-isotp)
