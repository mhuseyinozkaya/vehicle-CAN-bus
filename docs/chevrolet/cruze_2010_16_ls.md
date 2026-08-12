# Chevrolet Cruze 2010 1.6 LS — OBD-II pin dizilimi

## Pin haritası

| Pin | Ağ / sinyal | Açıklama |
|-----|-------------|----------|
| 1   | SWCAN / LS-GMLAN | Body Control Module, radyo, klima, cam, gösterge paneli (33.3 kbit/s tek hatlı) |
| 4   | Şase toprağı | Doğrudan akü eksi kutbuna bağlı |
| 5   | Sinyal toprağı | Araç bilgisayarının sağladığı temiz referans |
| 6   | CAN High | HS-CAN, 500 kbit/s |
| 14  | CAN Low | HS-CAN, 500 kbit/s |
| 16  | +12 V | Kontak kapalıyken de besleme sağlar |

## Hangi toprak?

Pin 4 (şase toprağı) doğrudan akünün negatif kutbuna bağlıdır. Bu hat
üzerinden farlar, marş motoru, fan gibi yüksek akım çeken tüketicilerin
dönüş akımı geçer. Bu akımlar, kablonun direnci üzerinde birkaç yüz
milivolta varan gerilim düşümleri (ground offset) yaratır ve hat
elektromanyetik parazite açıktır.

Ölçüm referansı olarak pin 4 seçilirse:

- Okunan sinyal seviyeleri gerçeği yansıtmaz.
- Ölçüm cihazı ile araç arasında toprak döngüsü (ground loop) oluşabilir;
  bu döngüden geçen akım hassas modüllere zarar verebilir.

Pin 5 (sinyal toprağı) ise araç bilgisayarının, tam da bu amaçla
sağladığı düşük gürültülü referans hattıdır.

> **Araçtan veri okurken referans noktası olarak daima pin 5 (sinyal
> toprağı) kullanın.**

Adaptör beslemesi için pin 16 (+12 V) ile pin 5 çifti uygundur.

## Pin görseli

![Chevrolet Cruze OBD-II konnektör pin dizilimi](../images/cruze_pins.jpg)

## Notlar

- SWCAN (pin 1) ayrı bir fiziksel ağdır ve MCP2515 + TJA1050 kombinasyonu
  ile okunamaz; tek hatlı CAN için farklı bir transceiver gerekir
  (örn. MCP2515 + MCP2515'e uygun bir SW-CAN transceiver).
- HS-CAN (pin 6/14) `S6` (500 kbit/s) ile okunur.

## Kaynaklar

- [On-board diagnostics — Wikipedia](https://en.wikipedia.org/wiki/On-board_diagnostics)
- [OBD-II PIDs — Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs)
