# Windows 11 bellek karşılaştırması

19 Kasım 2026 tarihinde Windows 11 Pro 26H2, yapı 26300.9974, x64 üzerinde ölçüldü. C++/WinRT sürümü sabit başlık gösterirken C# sürümünün özel yerleşik bellek kullanımını 13,36 MiB'den 2,98 MiB'ye indirdi. Kapaklı parça geçişlerinde medyan 18,11 MiB'den 3,61 MiB'ye düştü.

## Sürece özel fiziksel RAM

Her hücrede medyan ve örneklerde görülen en yüksek değer, MiB cinsinden verilir. 1 MiB = 1.048.576 bayt.

| Durum | C++/WinRT | C# | Wireless Battery 1.0.0 |
|---|---:|---:|---:|
| Şarkı gösterilmiyor | 1,51 / 1,60 | 8,94 / 8,96 | 3,75 / 3,76 |
| Sabit başlık, kart kapalı | 2,98 / 3,08 | 13,36 / 15,79 | 3,75 / 3,82 |
| Sekiz ardışık kapaklı parça değişimi | 3,61 / 3,72 | 18,11 / 38,51 | 3,75 / 3,86 |
| Son kart kapandıktan sonra, duraklatılmış | 3,59 / 3,72 | 27,47 / 38,45 | 3,75 / 3,84 |

## Toplam çalışma kümesi

Bu değer RAM'deki özel ve paylaşılabilir sayfaları birlikte sayar. Tabloda medyanlar gösterilir.

| Durum | C++/WinRT | C# | Wireless Battery 1.0.0 |
|---|---:|---:|---:|
| Şarkı gösterilmiyor | 17,52 MiB | 62,50 MiB | 27,26 MiB |
| Sabit başlık, kart kapalı | 25,32 MiB | 83,55 MiB | 27,26 MiB |
| Kapaklı parça değişimleri | 28,61 MiB | 93,92 MiB | 27,26 MiB |
| Son kart kapandıktan sonra | 28,67 MiB | 103,89 MiB | 27,26 MiB |

## Ölçüm ve davranış kontrolleri

1. İki turda çalıştırma sırası C++ → C# ve C# → C++ olarak değiştirildi. Her uygulama şarkısız başlatıldı, üç saniye beklendi ve beş saniye ölçüldü. Ardından sessiz test oynatıcısı zaten parça yayımlarken widget yeniden açıldı.
2. Her sürümde açılıştaki başlık gerçek görev çubuğunda görüldü. Duraklatma, devam ettirme ve aynı parçanın tekrar bildirilmesi sırasında kartın kapalı kaldığı kontrol edildi. Sekiz saniyelik sabit başlık ölçümü alındı.
3. Tur başına sekiz farklı başlık ve dönüşümlü iki adet 512 × 512 PNG, Windows medya oturumu üzerinden iletildi. Parçalar yaklaşık 1,5 saniye aralıklarla değiştirildi. Her geçişte görev çubuğu başlığı, kartın görünmesi ve kapağın beklenen rengi masaüstünde kontrol edildi. Son kart kapandıktan sonra dört saniye daha ölçüm alındı.
4. Native kart iki turda son değişimden yaklaşık 4,04 ve 4,02 saniye sonra kapandı. Önceki, sonraki, oynat ve duraklat komutlarının test oynatıcısına ulaştığı ve oynatıcı sonraki parça desteğini kapatınca düğmenin devre dışı kaldığı doğrulandı.

Bellek dışarıdan, yaklaşık 100 ms aralıkla `K32GetProcessMemoryInfo` ve `PROCESS_MEMORY_COUNTERS_EX2` ile örneklendi. Medyanlar iki turun ilgili aşamadaki örneklerinden hesaplandı. En yüksek örnek, örnekler arasında oluşabilecek daha kısa zirveleri kaçırabilir. Ölçüm widget sürecini kapsar. Test oynatıcısı ve Explorer ayrı süreçlerdir. Bilgisayarda açık olan Wireless Battery kendi normal işini sürdürürken aynı anda örneklendi.

C# karşılaştırmasında önceki dağıtım paketindeki uygulama kullanıldı. C++ EXE dosyası 346.112 bayt ve dağıtım ZIP'i 163.519 bayttır. C++ EXE SHA-256 özeti `4E61921D602CDF81D9371747547104A3555F69C5C75FABEC73A3667D199BA9F5`, ölçülen pil göstergesinin EXE özeti `AD3BCA7AF7B56262E3A30E6F83AB1F9FC937B286926323FA6FFAE1A5D561DDBA` olarak doğrulandı.

Geliştirme sırasında bir ön koşuda yeni başlıkla birlikte beklenen kapak gözlenemedi. Dağıtım sürümü görünür kartta yeni medya bilgisi geldiğinde aynı akış tanıtıcısındaki kapağı da yeniden okur ve veri alırken daha yeni bir bildirim geldiyse eski sonucu bırakır. Tablolar son paketin iki tamamlanmış turunu içerir. Spotify ve YouTube Music hizmetlerinde canlı oynatma ayrıca sınanmadı.

Örneklerin tamamı, özet sayaçlar ve tur kontrolleri birlikte saklandı. [Ham örnekler](samples.csv) · [Bellek özeti](summary.csv) · [Kart kontrolleri](checks.csv)
