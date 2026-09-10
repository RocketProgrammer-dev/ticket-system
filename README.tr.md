# Ticket System — İş Takip ve Ticket Yönetim Sistemi

Çok kullanıcılı masaüstü ticket yönetim sistemi. Çalışanlar destek, bakım ve
yazılım talepleri için ticket açar; bu ticketlar teknik personele atanır ve
çözüm sürecine kadar takip edilir.

*For English documentation see [README.md](README.md).*

> **Terminoloji notu:** "ticket", "log", "commit" gibi alan terimleri
> bilinçli olarak çevrilmemiştir. Türkiye'deki yazılım ve destek ekiplerinde
> yerleşik kullanım bu yöndedir.

---

## İçindekiler

- [Mimari](#mimari)
- [Teknolojiler](#teknolojiler)
- [Hızlı başlangıç](#hızlı-başlangıç)
- [Ayrıntılı kurulum](#ayrıntılı-kurulum)
- [Kaynaktan derleme](#kaynaktan-derleme)
- [Uygulamanın kullanımı](#uygulamanın-kullanımı)
- [Tasarım kararları](#tasarım-kararları)
- [Gereksinim karşılama tablosu](#gereksinim-karşılama-tablosu)
- [Bilinen kısıtlar](#bilinen-kısıtlar)
- [Sorun giderme](#sorun-giderme)
- [Proje yapısı](#proje-yapısı)
- [Lisanslar](#lisanslar)

---

## Mimari

Üç katman, iki makineye dağıtılmış hâlde:

```
  [Kullanıcı PC]  Qt istemci ─┐
  [Kullanıcı PC]  Qt istemci ─┼── HTTP/JSON ──▶  REST API ── SQL ──▶ PostgreSQL
  [Kullanıcı PC]  Qt istemci ─┘                  [Sunucu makinesi]
```

**İstemci veritabanına doğrudan bağlanmaz.** Bu bir tercih meselesi değildir:
20. gereksinim yetki kontrollerinin sunucu tarafında yapılmasını şart koşar ve
her masaüstünde veritabanı parolası bulunuyorsa bu imkânsızdır. Parolayı
çalıştırılabilir dosyadan okuyan herkes tickets tablosunu yönetici yetkisiyle
sorgulayabilirdi.

Her katman kendi içinde de ayrılmıştır (22. gereksinim):

| Katman | İstemci | Sunucu |
|---|---|---|
| Sunum | `mainwindow`, dialoglar | — |
| Veri modeli | `ticketmodel`, `commentstore` | — |
| İletişim | `apiclient` | Minimal API uç noktaları |
| İş kuralları | — | yetki kontrolleri, doğrulama |
| Kalıcılık | — | EF Core → PostgreSQL |

İstemcinin arayüz sınıfları hiçbir zaman URL oluşturmaz veya JSON ayrıştırmaz;
`ApiClient` ile konuşurlar. REST yerine başka bir yöntem kullanılsaydı tek bir
dosya değişecekti.

---

## Teknolojiler

| Bileşen | Teknoloji | Not |
|---|---|---|
| Masaüstü istemci | C++20, Qt 6 (Widgets) | Qt 6.11 ile derlendi ve test edildi |
| REST API | C# / ASP.NET Core Minimal API | .NET 10 |
| ORM | Entity Framework Core + Npgsql | Database-first |
| Veritabanı | PostgreSQL 16 | |
| Kimlik doğrulama | JWT bearer token, BCrypt hash | |
| Derleme | CMake (istemci), dotnet CLI (API) | |
| Kurulum paketi | Inno Setup 7 | |

Şartname C++20 ve Qt 6'yı belirtir; bunlar istemci için geçerlidir. Sunucu
tarafı dilinin serbest olduğu proje sorumlusuyla teyit edilmiş, staj süresi
içinde geliştirme hızı nedeniyle C# seçilmiştir.

---

## Hızlı başlangıç

İstemci, API ve veritabanının aynı bilgisayarda çalıştığı tek makinelik
gösterim için.

```bash
# 1. Veritabanı
createdb ticketdb
psql -d ticketdb -f db/schema.sql
psql -d ticketdb -f db/seed.sql

# 2. API
cd backend/TicketApi
dotnet run
# bu terminali açık bırakın

# 3. İstemci
cmake -B build -S .
cmake --build build
./build/TicketApp
```

`e.demir` / `Test1234` (yönetici) ile giriş yapın.

İstemci sunucuya ulaşamıyorsa [Sorun giderme](#sorun-giderme) bölümüne bakın.

---

## Ayrıntılı kurulum

### 1. PostgreSQL

PostgreSQL 16 veya üstünü kurun.

**Uygulamaya özel bir veritabanı kullanıcısı oluşturun.** Uygulamanın tek bir
veritabanına erişmesi yeterlidir; `postgres` süper kullanıcısının bir yapılandırma
dosyasında durmasına gerek yoktur.

```sql
CREATE USER ticketapp WITH PASSWORD '<belirlenen-parola>';
CREATE DATABASE ticketdb OWNER ticketapp;
```

Şemayı ve örnek verileri yükleyin:

```bash
psql -U ticketapp -d ticketdb -f db/schema.sql
psql -U ticketapp -d ticketdb -f db/seed.sql
```

`schema.sql` on tablo, üç enum tipi, iki trigger, bir view ve gerekli
indeksleri oluşturur. `seed.sql` dört departman, beş kategori, beş kullanıcı
ve yorumları ile zaman kayıtları bulunan beş ticket ekler.

**Demo hesapları** — hepsinin parolası `Test1234`:

| Kullanıcı adı | Ad | Rol | Görebildikleri |
|---|---|---|---|
| `e.demir` | Elif Demir | Yönetici | Her şey ve Yönetim penceresi |
| `b.aydin` | Burak Aydın | Teknik personel | Tüm ticketlar, iç notlar, zaman kaydı |
| `s.koc` | Selin Koç | Teknik personel | Yukarıdakiyle aynı |
| `o.sahin` | Onur Şahin | Personel | Yalnızca kendi ticketları, iç not yok |
| `d.yildiz` | Deniz Yıldız | Personel | Yukarıdakiyle aynı |

> Doğru bilgilerle giriş başarısız oluyorsa, seed dosyasındaki parola
> hashlerinin kendi BCrypt sürümünüz için yeniden üretilmesi gerekebilir.
> [Sorun giderme](#sorun-giderme) bölümüne bakın.

### 2. API

**Geliştirme ortamı:**

```bash
cd backend/TicketApi
dotnet restore
dotnet run
```

Yapılandırma `appsettings.Development.json` dosyasından okunur. Bu dosya parola
içerdiği için sürüm kontrolüne dâhil edilmemiştir. Oluşturun:

```json
{
  "ConnectionStrings": {
    "Default": "Host=localhost;Port=5432;Database=ticketdb;Username=ticketapp;Password=<parolanız>"
  },
  "Jwt": {
    "Key": "geliştirme-için-en-az-32-karakterlik-herhangi-bir-metin"
  }
}
```

API dinlediği adresi ekrana yazar. Uç noktaları doğrudan denemek için tarayıcıda
`/swagger` adresini açın.

**Üretim ortamı:** kendi kendine yeten yayınlama, Windows servisi olarak
çalıştırma, güvenlik duvarı kuralları ve yapılandırma için
[backend/DEPLOYMENT.md](backend/DEPLOYMENT.md) dosyasına bakın.

### 3. İstemci

İstemci API adresini `ticketapp.ini` dosyasından okur; bu dosya
**çalıştırılabilir dosyayla aynı klasörde** bulunmalıdır:

```ini
[Server]
Url=http://192.168.1.42:5000
```

Sunucunun adını veya IP adresini kullanın. `localhost` yalnızca API istemciyle
aynı makinede çalışıyorsa işe yarar.

Bu ayarı değiştirmek için bilinçli olarak bir arayüz sunulmamıştır —
[Tasarım kararları](#tasarım-kararları) bölümüne bakın.

Windows'ta kurulum paketi bu adresi kurulum sırasında sorar ve dosyayı sizin
için yazar.

---

## Kaynaktan derleme

### İstemci (Linux)

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools cmake build-essential
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### İstemci (Windows)

Qt 6.11'i MinGW araç zinciriyle kurun, ardından `CMakeLists.txt` dosyasını Qt
Creator'da açıp **Release** yapılandırmasıyla derleyin veya Qt komut isteminden:

```
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

### Çeviriler

Kullanıcıya görünen her metin `tr()` içine alınmıştır. Yeni arayüz metni
ekledikten sonra:

```bash
lupdate-qt6 src/ -ts translations/ticketapp_tr.ts translations/ticketapp_en.ts
linguist6 translations/ticketapp_tr.ts     # çevirin, Ctrl+Enter "bitti" işaretler
lrelease-qt6 translations/ticketapp_tr.ts  # .qm dosyasını üretir
```

`.qm` dosyaları çalıştırılabilir dosyanın yanında dağıtılmalıdır. Linguist'te
"bitti" olarak işaretlenmemiş satırlar `.qm` dosyasına dâhil edilmez ve sessizce
İngilizceye döner.

> Ubuntu'da soneksiz `lupdate` ve `lrelease` komutları çoğunlukla Qt5'i
> gösterir ve hata verir. `-qt6` sonekli sürümleri kullanın.

### Windows kurulum paketi

Tüm süreç için [INSTALLER.md](INSTALLER.md) dosyasına bakın. Özet:

```
cmake --build build-release                    # Release, Debug değil
windeployqt --release deploy\windows\TicketApp.exe
copy translations\*.qm deploy\windows\
ISCC.exe installer.iss
```

Sonucu **normal** bir komut isteminde test edin, Qt komut isteminde değil — Qt
komut isteminde Qt zaten PATH üzerindedir ve eksik DLL'leri gizler.

---

## Uygulamanın kullanımı

### Roller

| | Personel | Teknik personel | Yönetici |
|---|---|---|---|
| Ticket açma | ✔ | ✔ | ✔ |
| Kendi ticketlarını görme | ✔ | ✔ | ✔ |
| Tüm ticketları görme | | ✔ | ✔ |
| Genel yorum | ✔ | ✔ | ✔ |
| İç not | | ✔ | ✔ |
| Durum/öncelik/atama değiştirme | | ✔ | ✔ |
| Zaman kaydı | | ✔ | ✔ |
| Dashboard ve raporlar | | | ✔ |
| Kullanıcı/departman/kategori yönetimi | | | ✔ |
| Audit log | | | ✔ |

Bunların tamamı sunucu tarafında uygulanır. İstemci, kullanıcının
kullanamayacağı kontrolleri gizler; ancak bu yalnızca kolaylık içindir — API,
istemcinin ne gönderdiğine bakmaksızın isteği reddeder.

### Ana pencere

- **Ticket listesi** — gecikmiş satırlar kırmızı, kritik öncelik kalın
- **Arama** — ticket numarası, konu, talep eden veya atanan kişide eşleşir
- **Filtreler** — durum, öncelik, yalnızca gecikmişler
- **Sıralama** — herhangi bir sütun başlığına tıklayın
- **Sayfalama** — sayfa başına 25/50/100/1000
- **Ctrl+N** yeni ticket, **Ctrl+E** CSV dışa aktarma, **F5** yenileme
- **Bildirimler** — araç çubuğunda okunmamış sayısı, 30 saniyede bir sorgulanır

Dışa aktarmadan önce satır seçilirse yalnızca seçilenler aktarılır; hiçbir satır
seçilmezse görünen sayfanın tamamı aktarılır.

### Ticket detayı

Bir tickete çift tıklayın. **Yorumlar**, **Harcanan zaman** (yalnızca teknik
personel) ve **Ekler** sekmeleri bulunur. Teknik personel ayrıca durum, öncelik
ve atanan kişiyi değiştirebilir; her değişiklik bir geçmiş kaydı oluşturur.

"Bu ticketı dışa aktar" dört bölümlü bir CSV üretir: özet, yorumlar, zaman
kayıtları ve ekler.

### Yönetim penceresi (yalnızca yönetici)

- **Dashboard** — özet kartları, duruma ve önceliğe göre dağılım, kişi başına
  iş yükü
- **Raporlar** — güncel ticket listesinin CSV çıktısı
- **Yönetim** — kullanıcılar, departmanlar ve kategoriler
- **Audit log** — filtrelenebilir, başarısız giriş denemeleri kırmızı

### Dil

**Ayarlar → Dil**, ardından uygulamayı yeniden başlatın. Qt çevirileri widget'lar
oluşturulurken uygular; bu nedenle dil değişikliği hâlihazırda açık olan
pencerelerde etkili olamaz. İlk çalıştırmada sistem dili varsa o kullanılır.

---

## Tasarım kararları

**Personel yalnızca kendi ticketlarını görür.** Şartname, talep edenlerin ticket
açtıktan sonra neleri görebileceğini belirtmez. En az yetki ilkesi seçilmiştir,
çünkü ticket açıklamaları düzenli olarak hassas içerik barındırır — İK sorunları,
maaş konuları, güvenlik açıkları. Departman genelinde görünürlüğe geçmek
`GET /api/tickets` içinde tek satırlık bir değişikliktir.

**API adresi kullanıcı tarafından değiştirilemez.** Adres, sıradan kullanıcıların
yazamayacağı Program Files konumuna kurulan `ticketapp.ini` dosyasından okunur.
Hedefini her kullanıcının değiştirebildiği bir istemci, kimlik bilgisi toplamak
için açık bir kapıdır. Makineyi yöneten kişi dosyayı yine
düzenleyebilir.

**Kullanıcılar silinmez, inaktif edilir.** Ticketı, yorumu veya zaman kaydı olan
kullanıcılar için silme reddedilir (HTTP 409) ve arayüz bunun yerine kullanıcıyı inaktif yapmayı
önerir. Geçmiş kayıtlarda görünen bir kişiyi silmek, 21. gereksinimin korumak
için var olduğu denetim izinde delikler açardı. Silme yalnızca hiç etkinliği
olmayan hesaplar için mümkündür.

**İnaktif kullanıcılar yönetim listesinde varsayılan olarak gizlidir.** Saklama ve
görünürlük ayrı konulardır. Birkaç yıl içinde ayrılmış çalışanlar mevcut
personelden fazla olur ve ekranı kullanılmaz hâle getirir; kayıtlar veritabanında
kalır, liste ise operasyonel olarak anlamlı olanı gösterir. Bir onay kutusu
hepsini görünür yapar.

**Grafikler Qt Charts yerine QPainter ile çizilmiştir.** Qt Charts yalnızca GPL
veya ticari lisansla sunulur, LGPL ile değil. Kullanılması bu uygulamanın GPL
lisanslı olmasını gerektirirdi.

**Filtreleme, sıralama ve sayfalama SQL tarafında yapılır.** Bunlardan herhangi
birinin istemcide yapılması yalnızca geçerli sayfayı etkilerdi: "kritik" filtresi
1. sayfadaki kritik ticketları gösterirken sonraki sayfalardakileri gizlerdi ve
sıralama, hangi 25 kaydın en üstte olacağını seçmek yerine rastgele 25 kaydı
yeniden dizerdi. Sunucu tarafı filtreleme olmadan sayfalama, sayfalamanın hiç
olmamasından daha kötüdür; çünkü çalışıyormuş gibi görünür.

**Ticket numaraları veritabanı sequence'i ve trigger'ı ile üretilir.** Aynı anda
gelen iki kayıt çakışamaz. Uygulama kodunda üretilmesi, eş zamanlı yük altında
er ya da geç tekrar eden numaralar doğurur.

**Zaman tam sayı dakika olarak saklanır.** Saatin ondalık sayı olarak tutulması
0,1 değerini tam olarak temsil edemez yuvarlama hatalarına yol açabilir.

**Değişiklik geçmişi id değil isim saklar.** `assigned_to: 2 → 3` kaydını sonradan
okuyan biri için anlamsızdır.

**Yüklenen dosyalara sistem tarafından üretilen adlar verilir.** İstemcinin
gönderdiği dosya adı yalnızca görüntüleme etiketi olarak saklanır, hiçbir zaman
bir dosya yolu oluşturmakta kullanılmaz. `../../etc/cron.d/evil` gibi bir ad,
veritabanı sütununda zararsız bir metne dönüşür; çünkü gerçek dosya, uygulamanın
seçtiği klasörde bir GUID'dir. Uzantılar kara listeye değil, beyaz listeye göre
denetlenir — kara listeler her zaman bir şeyi atlar.

---

## Gereksinim karşılama tablosu

| # | Gereksinim | Durum | Nerede |
|---|---|---|---|
| 1 | Proje tamamen sıfırdan geliştirilecektir | ✔ | — |
| 2 | Kullanıcı giriş ve çıkış sistemi | ✔ | `loginwindow`, `POST /api/auth/login` |
| 3 | Personel, teknik personel ve yönetici rolleri | ✔ | `userrole.h`, JWT claim'leri |
| 4 | Kullanıcılar yeni ticket oluşturabilecektir | ✔ | `newticketdialog` |
| 5 | Benzersiz ve otomatik ticket numarası | ✔ | `ticket_number_seq` + trigger |
| 6 | Ticketların personele atanabilmesi | ✔ | `PUT /api/tickets/{id}` |
| 7 | Durum ve öncelik yönetimi | ✔ | Ticket detay penceresi |
| 8 | Son teslim tarihi | ✔ | `tickets.due_date` |
| 9 | Ticket altında yorumlaşma | ✔ | `ticket_comments` |
| 10 | Genel yorum ve iç notun ayrılması | ✔ | `is_internal`, sunucuda filtrelenir |
| 11 | Dosya ekleme | ✔ | `AttachmentStorage.cs` |
| 12 | Harcanan sürenin kaydedilmesi | ✔ | `time_entries` |
| 13 | Durum, atama ve öncelik geçmişi | ✔ | `ticket_history` |
| 14 | Uygulama içi bildirim sistemi | ✔ | `notifications`, 30 sn sorgulama |
| 15 | Geciken ticketların otomatik belirlenmesi | ✔ | `OverdueScanner` arka plan servisi |
| 16 | Arama, filtreleme, sıralama ve sayfalama | ✔ | Tamamı sunucu tarafında |
| 17 | Yönetici dashboard ve raporlama ekranı | ✔ | Yönetim penceresi |
| 18 | Raporların CSV olarak dışa aktarılması | ✔ | `csvexporter.cpp` |
| 19 | Kullanıcı, departman ve kategori yönetimi | ✔ | Yönetim sekmesi |
| 20 | Sunucu tarafında rol ve yetki kontrolü | ✔ | Her uç nokta |
| 21 | Kritik işlemlerin audit log'a kaydedilmesi | ✔ | `audit_log`, Audit sekmesi |
| 22 | Veritabanı, arayüz ve iş mantığının ayrılması | ✔ | Bkz. [Mimari](#mimari) |
| 23 | Nesne yönelimli programlama | ✔ | — |
| 24 | Hataların log dosyasına yazılması | ✔ | `logging.cpp` |
| 25 | Düzenli ve anlamlı Git commitleri | ✔ | — |
| 26 | Kurulum ve kullanım belgesi | ✔ | Bu dosya |
| 27 | Windows kurulum paketi | ✔ | `installer.iss` |

---

## Bilinen kısıtlar

Eksik kalan veya gerçek bir kurulumdan önce çalışma gerektiren noktalar. Bunlar
gözden kaçmış değil, bilinen kısıtlardır.

**Trafik düz HTTP üzerinden akar.** Kullanıcı adları, parolalar ve ticket
içerikleri ağı dinleyen herkes tarafından okunabilir. Üretim kullanımı HTTPS
gerektirir — Kestrel'de yapılandırılmış bir sertifika veya API'nin IIS ya da
nginx arkasında ters vekil olarak konumlandırılması. Projedeki en önemli kısıt
budur.

**JWT imzalama anahtarı düz bir yapılandırma dosyasında tutulur.** Dosyanın
izinlerini servis hesabıyla sınırlayın. Bir secrets manager daha iyi olurdu.

**Bildirimler 30 saniyede bir sorgulanır**, sunucudan itilmez. WebSocket
bağlantısı daha hızlı olurdu ancak yeniden bağlanma mantığı ve sürdürülmesi
gereken bir protokol getirir; bu ölçekte sorgulama yeterlidir.

**Her yeni ticket tüm yöneticilere bildirilir.** Üç yöneticiyle bu yararlıdır;
otuz yöneticiyle gürültüye dönüşür. Yalnızca ticketın departmanındaki
yöneticilere bildirim göndermek doğru iyileştirme olurdu — şema bunu
`users.department_id` ile hâlihazırda destekliyor.

**Atanan kişi açılır listesi isimle eşleştirir.** `Ticket` yapısı atanan kişinin
id'sini değil görünen adını taşır; aynı ada sahip iki kullanıcı ayırt edilemezdi.
Çözüm, ticket JSON'una `assignedToId` eklemektir.

**Ticket açıklamaları liste ile birlikte gönderilir.** 500 ticket çekmek, çoğu
görüntülenmeyecek 500 açıklamayı da taşır. Tam detayı döndüren ayrı bir
`GET /api/tickets/{id}` daha düzenli olurdu.

**Otomatik veritabanı yedeklemesi yapılandırılmamıştır.**

**Otomatik test bulunmamaktadır.** Doğrulama baştan sona elle yapılmıştır.

---

## Sorun giderme

Geliştirme sırasında karşılaşılan sorunlar ve nedenleri.

### `psql: error: db/schema.sql: Permission denied`

`sudo -u postgres`, home dizininizin içini okuyamayan bir kullanıcıya geçer.
Dosyayı kendi shellinizin açmasını sağlayın:

```bash
sudo -u postgres psql -d ticketdb -v ON_ERROR_STOP=1 < db/schema.sql
```

`-f` yerine `<` yönlendirmesine dikkat edin. Ya da daha iyisi, Linux kullanıcı adınızla
aynı adda bir veritabanı rolü oluşturun:
`sudo -u postgres createuser --superuser $USER`.

### `28P01: password authentication failed for user "postgres"`

API, parola doğrulaması gerektiren TCP üzerinden bağlanır; yerel soket üzerinden
çalışan `psql` ise peer doğrulaması kullanabilir. Bir parola belirleyin:

```bash
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'parolanız';"
```

Ardından uygulamanın kullandığı mekanizmayla doğrulayın:

```bash
psql -h localhost -U postgres -d ticketdb -c "SELECT 1;"
```

Bu çalışıp API hâlâ başarısız oluyorsa, API sandığınızdan farklı bir yapılandırma
okuyordur. Bir sonraki maddeye bakın.

### API `appsettings.Development.json` dosyasını yok sayıyor

Bu dosya yalnızca `ASPNETCORE_ENVIRONMENT=Development` iken yüklenir; bu değişken
normalde `Properties/launchSettings.json` tarafından atanır. O dosya yoksa
uygulama Production olarak çalışır. Oluşturun:

```json
{
  "profiles": {
    "TicketApi": {
      "commandName": "Project",
      "applicationUrl": "http://localhost:5000",
      "environmentVariables": { "ASPNETCORE_ENVIRONMENT": "Development" }
    }
  }
}
```

`export` ile atanan bir ortam değişkeni yalnızca o terminalde vardır;
yapılandırmanın bir dosyada olmasının nedeni budur.

### `BCrypt.Net.SaltParseException: Invalid salt version`

Saklanan hash bozulmuştur; nedeni neredeyse her zaman shell expansion'dır (kabuk genişletmesidir). Bir
BCrypt hash'i `$2a$11$` içerir ve **çift** tırnak içinde bash `$2` ile `$11`
ifadelerini değişken olarak genişleterek hash'i sessizce bozar. Etkileşimli bir
psql oturumu kullanın:

```bash
psql -d ticketdb
```

```sql
UPDATE users SET password_hash = '$2a$11$...';
```

Geçerli bir hash tam olarak 60 karakterdir ve `$2a$` veya `$2b$` ile başlar.
Doğrulayın:

```sql
SELECT username, left(password_hash, 7), length(password_hash) FROM users;
```

Yeni bir hash üretmek için:

```bash
python3 -m venv ~/.venvs/tools
~/.venvs/tools/bin/pip install bcrypt
~/.venvs/tools/bin/python -c "import bcrypt; print(bcrypt.hashpw(b'Test1234', bcrypt.gensalt(11)).decode())"
```

### `Reading as 'System.Int32' is not supported for fields having DataTypeName 'public.user_role'`

PostgreSQL enum tipleri **iki kez** tanıtılmalıdır — bir kez veri kaynağında
Npgsql'e, bir kez de kendi tip kaydını tutan EF Core'a:

```csharp
dataSourceBuilder.MapEnum<UserRole>("user_role");          // Npgsql

builder.Services.AddDbContext<AppDbContext>(o => o
    .UseNpgsql(dataSource, npgsql => {
        npgsql.MapEnum<UserRole>("user_role");             // EF Core
    }));
```

Yalnızca birincisinin yapılması, başlangıçta değil sorgu anında hata verir.

### `Failed to bind to address http://127.0.0.1:5000: address already in use`

Önceki bir `dotnet run` hâlâ çalışıyordur:

```bash
ss -tlnp | grep 5000     # PID'i bulun
pkill -f "dotnet run"
```

### Kodda açıkça bulunan bir uç nokta 404 dönüyor

API önceki derlemeyi sunuyordur. `dotnet run` değişiklikte yeniden yüklemez ve
derleme başarısız olursa son başarılı ikili dosyayı sunmaya devam eder.
Geliştirme sırasında `dotnet watch run` kullanın. Sunucunun gerçekte neleri
tanıdığını doğrulayın:

```bash
curl -s http://localhost:5000/swagger/v1/swagger.json | grep -o '"/api/[^"]*"' | sort -u
```

### CMake `(` ve `)` adlı dosyalar üretiyor veya yollar `/` kökünden başlıyor

Proje yolu boşluk veya parantez içeriyordur. CMake tırnaksız argümanları
boşluktan böler ve parantezleri liste sözdizimi olarak yorumlar; sonuçta anlamsız
adlara sahip hedefler oluşur. Derleme yollarını boşluk, parantez ve ASCII dışı
karakterlerden uzak tutun.

### `moc_` dosyasından gelen `undefined reference to 'SınıfAdı::slotAdı()'`

Başlıkta tanımlanan bir slot'un gövdesi yoktur. moc, bağlanmış olsun olmasın
tanımlanan her slot için kod üretir. Ya gövdeyi yazın ya da tanımı silin.

Buna karşılık `undefined reference to vtable` genellikle `CMAKE_AUTOMOC`
kapalıdır, sınıfta `Q_OBJECT` eksiktir veya başlık `add_executable` içinde
listelenmemiştir anlamına gelir.

### `lupdate: could not exec '/usr/lib/qt5/bin/lupdate'`

Ubuntu Qt5 ve Qt6'yı yan yana kurar ve soneksiz komutlar çoğunlukla Qt5'i
gösterir. `lupdate-qt6`, `lrelease-qt6`, `linguist6` komutlarını veya Qt
kurulumunuzun içindeki tam yolu kullanın.

### Uygulama Windows'ta açılıyor ama pencere görünmüyor

Neredeyse her zaman eksik `platforms\qwindows.dll`. `windeployqt` bu dosyayı
çalıştırılabilir dosyanın yanındaki `platforms` alt klasörüne kopyalamalıdır.
Testi normal bir komut isteminde yapın, asla Qt komut isteminde değil — Qt komut
isteminde Qt PATH üzerindedir ve tam olarak bu hatayı gizler.

### İstemci iki makine arasında sunucuya ulaşamıyor

Olasılık sırasına göre üç neden:

1. **API localhost'a bağlıdır.** `--urls "http://0.0.0.0:5000"` ile başlatın veya
   `appsettings.json` içinde `Kestrel:Endpoints` ayarlayın. localhost'a bağlıyken
   yalnızca kendi makinesinden gelen bağlantıları kabul eder.
2. **Güvenlik duvarı portu engelliyordur.** Linux'ta `sudo ufw allow 5000/tcp`.
3. **Ağ istemcileri birbirinden yalıtıyordur**; birçok kurumsal kablosuz ağ bunu
   bilerek yapar. Telefon hotspot'u veya kablo ile test edin.

Önce istemci makinesinden doğrulayın:

```
curl http://<sunucu-ip>:5000/swagger/index.html
```

### Loglar nerede?

**İstemci:** Windows'ta `%APPDATA%\TicketSystem\TicketApp\ticketapp.log`,
Linux'ta `~/.local/share/TicketSystem/TicketApp/ticketapp.log`. Ayrıca
**Ayarlar → Log klasörünü aç** ile ulaşılabilir. 2 MB'de döner ve bir önceki
dosyayı saklar.

**API:** konsola yazar. Servis olarak çalıştırırken bir dosyaya yönlendirin.

---

## Proje yapısı

```
ticket-system/
├── CMakeLists.txt              İstemci derleme dosyası
├── installer.iss               Inno Setup betiği
├── INSTALLER.md                Windows paketleme adımları
├── README.md                   İngilizce belgelendirme
├── README.tr.md                Bu dosya
│
├── src/                        Qt istemci
│   ├── main.cpp                Giriş noktası, ekran geçişleri
│   ├── loginwindow.*           Kimlik doğrulama
│   ├── mainwindow.*            Ticket listesi, araç çubuğu, menüler, sayfalama
│   ├── ticketmodel.*           Tablo modeli
│   ├── ticketdetaildialog.*    Yorumlar, zaman, ekler, düzenleme
│   ├── newticketdialog.*       Ticket oluşturma formu
│   ├── managementdialog.*      Dashboard, raporlar, yönetim, audit
│   ├── admintab.*              Kullanıcı, departman, kategori
│   ├── audittab.*              Audit log görüntüleyici
│   ├── barchartwidget.*        QPainter grafikleri (GPL Qt Charts'tan kaçınır)
│   ├── apiclient.*             Tüm HTTP; REST'i bilen tek dosya
│   ├── commentstore.*          Ticket bazlı yorum önbelleği
│   ├── csvexporter.*           CSV üretimi
│   ├── logging.*               qInstallMessageHandler ile dosyaya loglama
│   ├── settings.*              Kurulum yapılandırması
│   └── language.*              Çeviri yükleme
│
├── resources/                  Simge ve Qt kaynak dosyaları
├── translations/               .ts çeviri kaynakları
├── deploy/                     ticketapp.ini şablonu
│
├── db/
│   ├── schema.sql              Tablolar, tipler, trigger'lar, indeksler, view
│   └── seed.sql                Örnek veriler
│
└── backend/
    ├── DEPLOYMENT.md           Sunucu kurulum kılavuzu
    └── TicketApi/
        ├── Program.cs          Tüm uç noktalar
        ├── Data/AppDbContext.cs
        ├── Models/Entities.cs
        └── Services/
            ├── OverdueScanner.cs      Arka planda gecikme tespiti
            └── AttachmentStorage.cs   Dosya yükleme yönetimi
```

---

## Lisanslar

**Qt**, **LGPLv3** kapsamında kullanılmaktadır. Bu lisans, Qt'nin dinamik olarak
bağlanması, kullanıcıların Qt kütüphanelerini değiştirebilmesi ve lisans ile
atıf bilgisinin pakete dâhil edilmesi koşuluyla kapalı kaynaklı bir uygulamada
kullanıma izin verir. `windeployqt` executable'ın yanında ayrı Qt DLL'lerini üretir.

**Qt Charts bilinçli olarak kullanılmamıştır.** Yalnızca GPL veya ticari lisansla
sunulur; kullanılması bu uygulamanın GPL lisanslı olmasını gerektirirdi. Grafikler
bunun yerine `QPainter` ile elle çizilmiştir.

**Qt Installer Framework de aynı nedenle kullanılmamıştır.** Kurulum paketi
**Inno Setup** ile üretilmiştir.

> Yeni Inno Setup sürümleri ticari kullanım için ticari lisans talep etmektedir.
> Uygulama ticari olarak dağıtılmadan önce bu durumun teyit edilmesi gerekir.
> WiX Toolset (MS-PL) ve NSIS (zlib) serbestçe kullanılabilir alternatiflerdir.

**BCrypt.Net-Next**, **Npgsql**, **Entity Framework Core** ve **Swashbuckle**
paketlerinin tamamı MIT veya Apache 2.0 lisanslıdır ve bu uygulamaya kısıt
getirmez.