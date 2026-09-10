-- =====================================================================
--  Demo data.  Run AFTER schema.sql:
--    psql -U postgres -d ticketdb -f seed.sql
--
--  Keeping demo rows here rather than hardcoded in C++ means the client
--  ships clean, and "seeding a development database" is a normal practice
--  a reviewer expects to see — unlike leftover fake data in the UI code.
-- =====================================================================

INSERT INTO departments (name) VALUES
    ('Bilgi İşlem'),
    ('İnsan Kaynakları'),
    ('Muhasebe'),
    ('Satış');

INSERT INTO categories (name, department_id) VALUES
    ('Donanım',        1),
    ('Yazılım',        1),
    ('Ağ ve İnternet', 1),
    ('Yetkilendirme',  1),
    ('Genel Talep',    NULL);

-- All demo passwords are 'Test1234!'.
-- Replace these hashes with ones your own BCrypt call produces — a hash from
-- a different library version will simply fail to verify.
INSERT INTO users (username, password_hash, full_name, email, role, department_id) VALUES
    ('e.demir',  '$2a$11$REPLACE_WITH_REAL_BCRYPT_HASH_VALUE_HERE_00000000', 'Elif Demir',    'elif.demir@example.com',    'manager',    1),
    ('b.aydin',  '$2a$11$REPLACE_WITH_REAL_BCRYPT_HASH_VALUE_HERE_00000000', 'Burak Aydın',   'burak.aydin@example.com',   'technician', 1),
    ('s.koc',    '$2a$11$REPLACE_WITH_REAL_BCRYPT_HASH_VALUE_HERE_00000000', 'Selin Koç',     'selin.koc@example.com',     'technician', 1),
    ('o.sahin',  '$2a$11$REPLACE_WITH_REAL_BCRYPT_HASH_VALUE_HERE_00000000', 'Onur Şahin',    'onur.sahin@example.com',    'staff',      3),
    ('d.yildiz', '$2a$11$REPLACE_WITH_REAL_BCRYPT_HASH_VALUE_HERE_00000000', 'Deniz Yıldız',  'deniz.yildiz@example.com',  'staff',      2);

-- ticket_number is omitted deliberately: the trigger fills it in.
-- Dates are relative to today so the demo always has overdue rows, whenever
-- you run it. Hardcoded dates go stale and your red rows quietly disappear.
INSERT INTO tickets (title, description, status, priority, created_by, assigned_to, department_id, category_id, due_date) VALUES
    ('3. kat yazıcısı çevrimdışı',
     'Muhasebe katındaki yazıcı ağda görünmüyor. Yeniden başlatma denendi.',
     'open', 'normal', 4, 2, 1, 1, CURRENT_DATE + 3),

    ('Bordro modülü kayıt sırasında hata veriyor',
     'Kaydet butonuna basıldığında "unhandled exception" hatası alınıyor.',
     'in_progress', 'critical', 5, 3, 1, 2, CURRENT_DATE - 2),

    ('Yeni personel için bilgisayar kurulumu',
     'Pazartesi başlayacak yeni personel için donanım ve hesap açılışı gerekiyor.',
     'waiting', 'low', 5, NULL, 1, 1, CURRENT_DATE + 10),

    ('VPN bağlantısı sürekli kopuyor',
     'Uzaktan çalışan kullanıcılarda VPN oturumu birkaç dakikada bir düşüyor.',
     'open', 'high', 4, 2, 1, 3, CURRENT_DATE - 1),

    ('Tasarım ekibi için ek monitör talebi',
     'İki adet 27 inç monitör talep edilmektedir.',
     'resolved', 'normal', 5, 3, 4, 5, CURRENT_DATE - 8);

INSERT INTO ticket_comments (ticket_id, author_id, body, is_internal) VALUES
    (1, 2, 'Yazdırma biriktiricisini yeniden başlattım, değişiklik olmadı.', FALSE),
    (1, 3, 'Bu modelde son firmware güncellemesi sonrası bilinen bir sorun var. Geri alma planını departmana henüz bildirmeyelim.', TRUE),
    (1, 2, 'Konuyu inceliyoruz, yarın bilgi vereceğiz.', FALSE),
    (2, 3, 'Hata logu incelendi, null referans kaynaklı.', FALSE),
    (4, 2, 'Firewall tarafında oturum zaman aşımı süresi düşük ayarlanmış olabilir.', TRUE);

INSERT INTO time_entries (ticket_id, user_id, minutes, note) VALUES
    (1, 2,  45, 'Yerinde inceleme'),
    (2, 3, 120, 'Hata ayıklama'),
    (2, 3,  90, 'Düzeltme ve test'),
    (4, 2,  30, 'Firewall log incelemesi');

INSERT INTO ticket_history (ticket_id, changed_by, field_name, old_value, new_value) VALUES
    (2, 3, 'status',   'open',   'in_progress'),
    (2, 1, 'priority', 'high',   'critical'),
    (5, 3, 'status',   'open',   'resolved');
