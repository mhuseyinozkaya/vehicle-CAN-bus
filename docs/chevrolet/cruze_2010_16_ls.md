## OBD Pin dizilimi

| Pin | Açıklama                                                         |
| --- | ---------------------------------------------------------------- |
| 1   | SWCAN/LS-GMLAN Body Control Module, Radyo, Klima, Pencere, panel |
| 4   | Şase topraklaması                                                |
| 5   | Sinyal topraklaması                                              |
| 6   | CAN High                                                         |
| 14  | CAN Low                                                          |
| 16  | Kararlı +12V gerilim                                             |

Şase topraklaması pini doğrudan akünün negatif(-) kutubu ile bağlıdır, doğrudan bağlı olduğu içinde elektromanyetik parazitten etkilenebilir. Eğer sinyal okumak için referans noktası burası seçilirse okunan değer gerçeği yansıtmayabilir ve araca zarar verilebilir.

Sinyal toprak hattı ise araç bilgisayarının doğrudan temiz, gürültüsüz olarak sağladığı hattır.

- Araçtan veri okunacağı zaman referans noktası olarak sinyal topraklaması(pin 5) seçilmelidir.


![]("../images/cruze_pins.jpg")

## Kaynaklar
https://en.wikipedia.org/wiki/On-board_diagnostics
