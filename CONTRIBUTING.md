# Katkı rehberi

## Başlamadan

```bash
git clone https://github.com/ibrahimtariksolak/vehicle-CAN-bus.git
cd vehicle-CAN-bus
make test
```

Testler donanım gerektirmez; sahte bir Arduino/MCP2515 HAL üzerinde
çalışır.

## Kurallar

**Her davranış değişikliği bir testle gelir.** Bu firmware gerçek araç
bus'ına bağlanıyor; sessiz bir hata pahalıya patlayabilir. Bir hatayı
düzeltiyorsanız, önce o hatayı yakalayan testi yazın (`regression:` ile
başlayan test isimleri bu amaçla kullanılıyor), sonra düzeltin.

**Ayarlar `config.h`'de kalır.** Kart, pin, hız gibi hiçbir sabit başka
bir dosyada geçmemeli.

**MCP2515 çağrıları `can_iface.cpp`'de kalır.** Sürücü kütüphanesinin
API'si forklar arasında değişiyor; tek bir yerde tutmak, kütüphane
değiştiğinde tek bir dosyayı düzeltmeyi yeterli kılıyor.

**`slcan_codec.*` Arduino'ya bağımlı olmaz.** Bu dosyalar normal bir
derleyiciyle test edilebiliyor; `Arduino.h` dâhil etmek bunu bozar.

**Seri porta hiçbir ek çıktı yazılmaz.** Debug amaçlı tek bir
`Serial.println()` bile SLCAN akışını bozar ve `slcand` bağlantısını
düşürür.

## Kod stili

- 4 boşluk girinti, tab yok.
- Satır uzunluğu 80 sütun.
- Kaynak kod yorumları İngilizce; README ve `docs/` Türkçe.
- Yorumlar *ne* yapıldığını değil *neden* yapıldığını anlatır.

## Commit mesajları

[Conventional Commits](https://www.conventionalcommits.org/) formatı:

```
fix: extended frames were sent to modules as standard frames
feat: support listen-only channel mode
docs: document the acceptance mask inversion
```

## Pull request kontrol listesi

- [ ] `make test` temiz geçiyor
- [ ] Arduino IDE veya `make build` ile derleniyor
- [ ] Yeni davranış için test eklendi
- [ ] `config.h`'ye ayar eklendiyse README tablosu güncellendi
- [ ] Gerçek donanımda denendiyse PR açıklamasına hangi araç/hız yazıldı

## Donanımda test

Araca bağlanmadan önce loopback modunda doğrulayın:

```bash
sudo ./scripts/slcan-up.sh
cansend slcan0 123#DEADBEEF   # loopback modunda geri gelmeli
```

Araçta test ederken: kontak açık, motor kapalı, araç hareketsiz.
