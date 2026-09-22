# ETeams STE — API Reference & Kế hoạch xây dựng Web Control Panel thay thế iPad

> Nguồn phân tích: `d:\Projects\ETS\eteams-ste-fixing` (đọc trực tiếp source code, không chỉ dựa vào `doc/*.md` vì `checklist.md` mục 8 xác nhận tài liệu đã lỗi thời ở phần PD controller).
> File chính đã đọc: `firmware/server/api.py`, `firmware/server/admin.py`, `firmware/server/ota.py`, `firmware/auth/pairing.py`, `firmware/auth/rsa.py`, `firmware/auth/aes_gcm.py`, `firmware/auth/otp.py`, `firmware/drivers/pd_controller.py`, `firmware/config/config.py`, `firmware/main.py`, `firmware/test/test_pairing.py`, `firmware/test/test_ota.py`, `doc/architecture.md`, `checklist.md`.
> Ngày phân tích: 2026-09-15.

> **Muốn bắt tay làm POC ngay?** Đọc [`poc-huong-dan-trien-khai.md`](poc-huong-dan-trien-khai.md) — có code đầy đủ, làm theo là chạy được, không cần đọc file này trước. File này (bên dưới) là phân tích/kiến trúc tham khảo — cần khi muốn hiểu "vì sao", hoặc khi mở rộng POC lên quy mô nhiều phòng/production.

---

## Mục lục

1. [Tổng quan hệ thống](#1-tổng-quan-hệ-thống)
2. [Danh sách API endpoint (những gì iPad gọi)](#2-danh-sách-api-endpoint-những-gì-ipad-gọi)
3. [Giao thức bảo mật chi tiết](#3-giao-thức-bảo-mật-chi-tiết)
4. [Kế hoạch: xây dựng Website thay thế iPad](#4-kế-hoạch-xây-dựng-website-thay-thế-ipad)
5. [Bổ sung: mô hình Kiosk PWA — "Add to Home Screen" trên iPad](#5-bổ-sung-mô-hình-kiosk-pwa--add-to-home-screen-trên-ipad)
6. [Có cách nào gọi thẳng ESP32 không? (Bypass C1/C2 và kiến trúc Direct/Hybrid)](#6-có-cách-nào-gọi-thẳng-esp32-không-bypass-c1c2-và-kiến-trúc-directhybrid)
7. [Bổ sung: nhận diện khuôn mặt (Face Recognition) làm lớp xác thực mở cửa](#7-bổ-sung-nhận-diện-khuôn-mặt-face-recognition-làm-lớp-xác-thực-mở-cửa)
8. [Đánh giá plan & Kế hoạch demo rút gọn 2 tuần](#8-đánh-giá-plan--kế-hoạch-demo-rút-gọn-2-tuần)

---

## 1. Tổng quan hệ thống

Đây **không phải** là app iPad — đây là **firmware chạy trên ESP32-S3** (MicroPython), đóng vai trò bộ điều khiển trung tâm cho một trạm/phòng họp: khoá cửa điện (maglock), sạc iPad qua USB-C PD (IC TPS25751), màn LCD OLED hiển thị trạng thái/OTP. iPad là **client duy nhất** được phép điều khiển thiết bị này qua mạng.

```
┌────────────┐   HTTPS :8080 (TLS 1.3, self-signed)   ┌──────────────────────────┐
│    iPad    │ ─────────────────────────────────────► │   ESP32-S3 (MicroPython)  │
│ (RSA-2048  │ ◄───────────────────────────────────── │  server/api.py (Microdot) │
│  private   │        AES-256-GCM encrypted body       │  auth/pairing.py          │
│  key)      │                                          │  drivers/*.py             │
└────────────┘                                          └──────────────────────────┘
                                                              │
                                            HTTP :80 (plaintext, không TLS)
                                                              │
                                                    ┌──────────────────────┐
                                                    │  Admin UI (kỹ thuật) │
                                                    │  server/admin.py     │
                                                    └──────────────────────┘
```

Đặc điểm cốt lõi cần nhớ trước khi đọc phần API và phần kế hoạch web:

- **"One device, one client"** — mỗi ESP32 chỉ ghép đôi (pair) được với **đúng một** danh tính RSA-2048 tại một thời điểm. Không có khái niệm "nhiều user cùng điều khiển" ở tầng firmware — đó là khái niệm phải xây ở tầng ứng dụng phía trên (xem Phần 4).
- Pairing dùng RSA-2048 (chậm, một lần), giao tiếp bình thường dùng AES-256-GCM (nhanh, ~1 ms).
- API điều khiển (`:8080`) luôn dùng TLS nhưng **cert tự ký** (self-signed) — trình duyệt sẽ chặn thẳng, không có cách nào "bỏ qua" bằng JavaScript (khác với Python `requests(verify=False)` mà script test dùng). Đây là ràng buộc quan trọng nhất cho Phần 4.
- Admin UI (`:80`) là **HTTP thuần, không mã hoá transport** — kể cả mật khẩu đăng nhập gửi ở dạng plaintext trên dây. Chỉ nên dùng trong LAN/VLAN cô lập.
- Pairing yêu cầu đọc OTP 6 số hiển thị **vật lý** trên màn LCD của thiết bị → không thể ghép đôi 100% từ xa; luôn cần một người đứng tại chỗ trong bước pairing lần đầu.

---

## 2. Danh sách API endpoint (những gì iPad gọi)

Toàn bộ endpoint dưới đây là những gì client "iPad" gọi tới ESP32 để gửi thông tin / điều khiển / tương tác với firmware. Chia làm 4 nhóm theo mức xác thực.

### 2.1. Cổng `:8080` (HTTPS/TLS 1.3) — API chính cho iPad

#### Nhóm A — Public (không cần pairing, không mã hoá)

| Method | Path | Request body | Response | Ghi chú |
|---|---|---|---|---|
| GET | `/version` | – | `{status, company:"Eteams", version, hardware:"ESP32-S3", bin}` | Luôn truy cập được dù đã pair hay chưa. Chỉ dùng để nhận diện firmware (QA / kiểm bản build), **không lộ** trạng thái pairing/network/health. Nguồn: `server/api.py:623-641`. |

#### Nhóm B — Pairing (plaintext JSON + chữ ký RSA, chỉ hoạt động khi **chưa pair**)

| Method | Path | Request body | Response thành công | Response lỗi |
|---|---|---|---|---|
| POST | `/auth/pair` | `{nonce, hash, signature}` | `{session_token}` (HTTP 200) | `{status:"failed", error}` — 403 nếu đã pair, 400 nếu nonce/hash/signature sai |
| POST | `/auth/trust` | `{otp, session_token, nonce, hash, signature}` | `{enc_session_key}` (base64, RSA-encrypted AES key) | 403 nếu đã pair, 400 nếu otp/nonce/hash/signature/token sai |

Chi tiết canonical string và chữ ký xem [Phần 3](#3-giao-thức-bảo-mật-chi-tiết). Nguồn: `server/api.py:645-707`, `auth/pairing.py:240-381`.

#### Nhóm C — Encrypted API (bắt buộc đã pair; body là **envelope AES-256-GCM**, xem Phần 3.2)

| Method | Path | Payload đã giải mã (plaintext JSON) | Kết quả (đã giải mã) | Mô tả |
|---|---|---|---|---|
| POST | `/auth/update-key` | `{pem}` | `{status:"ok"}` | Thay public key RSA của "iPad" mà **không cần unpair** — dùng khi đổi thiết bị/khoá mới nhưng vẫn giữ phiên AES hiện tại. |
| POST | `/auth/unpair` | `{}` | `{status:"ok", message:"device unpaired"}` | Xoá toàn bộ trạng thái pairing (NVS `auth/*`, file `ipad_pub.pem`) — thiết bị về trạng thái xuất xưởng, `/auth/pair` mở lại. |
| POST | `/info` | `{}` | `{status, company:"ETeams", version, hardware, bin}` | Thông tin firmware (bản mã hoá của `/version`, lưu ý chữ "ETeams" viết hoa khác `/version` — không nên dựa vào casing). |
| POST | `/status` | `{}` | `{status, paired, connected, ip, relays:{maglock}, pd:{...}}` | Trạng thái tổng: pairing, mạng, relay cửa, PD. `pd` có thể là `"not ready"`, `{resetting:true}`, `{error:...}` hoặc dict đầy đủ (xem cột PD status bên dưới). |
| POST | `/relays/maglock` | `{value: 0\|1}` | `{status:"ok", relay:"maglock", value}` | Điều khiển khoá từ cửa trực tiếp. `1` = relay có điện = cửa **mở**; `0` = cửa **khoá**. 400 nếu `value` không phải 0/1. |
| POST | `/power/charging` | `{value: 0\|1}` | `{status:"ok", charging:value}` | Bật/tắt sạc iPad (bọc `enable_charging`/`disable_charging`). 409 nếu đang có lệnh PD khác chạy, 503 nếu PD controller chưa sẵn sàng. |
| POST | `/power/pd/status` | `{}` | `{status:"ok", pd:{...}}` | Đọc chi tiết trạng thái TPS25751 (không đổi gì). |
| POST | `/power/pd/enable` | `{}` | `{status:"ok"}` | Bật sạc (tương đương `/power/charging {value:1}` nhưng route riêng). |
| POST | `/power/pd/disable` | `{}` | `{status:"ok"}` | Tắt sạc. |
| POST | `/power/pd/profile` | `{watts: 15\|20}` | `{status:"ok", profile_w}` | Ép profile công suất. **Lưu ý:** theo `checklist.md` mục 8, lệnh này chỉ ghi thanh ghi `DATA_1`, không phát 4CC nên **không có hiệu lực ngay** — chỉ áp dụng ở lần cắm lại cáp tiếp theo. Trả `ok` dù chưa đổi gì thấy được. |
| POST | `/logs` | `{limit?: int}` | `{status:"ok", logs:[...], resets:[...], health:[...]}` | Log ring buffer trong RAM (không ghi flash, mất khi reboot). `logs` mặc định 40 dòng (tối đa 120), `health` luôn tối đa 10 dòng bất kể `limit`. **Không có trong bảng của `doc/architecture.md`** — chỉ thấy trong code. |
| POST | `/ota/status` | `{}` | `{status:"ok", state, bytes_received, expected_size}` | Trạng thái phiên OTA hiện tại (`idle`/`receiving`). |
| POST | `/ota/begin` | `{session_id, size, sha256}` | `{status:"ok", chunk_size:4096, total_chunks}` | Mở phiên OTA, xoá phân vùng đích. `size` ≤ 2 MB, `sha256` = hex 64 ký tự thường. |
| POST | `/ota/chunk` | `{session_id, index, data(base64)}` | `{status:"ok", bytes_received}` | Upload từng chunk 4096 byte, **phải tuần tự** (index đúng thứ tự, không được nhảy/lặp). |
| POST | `/ota/apply` | `{session_id}` | `{status:"ok", message:"rebooting"}` | Verify SHA-256 toàn bộ, set boot partition, thiết bị tự reboot ~500 ms sau khi trả response. |
| POST | `/ota/abort` | `{session_id?}` | `{status:"ok"}` | Huỷ phiên OTA hiện tại (không đổi boot partition). |

**Object `pd` đầy đủ (từ `drivers/pd_controller.py: read_status()`)** — hữu ích khi thiết kế UI hiển thị trạng thái sạc:

```json
{
  "mode": "APP",
  "connected": true,
  "plug_present": true,
  "contract": true,
  "power_profile_w": 20,
  "voltage_mv": 9000,
  "current_ma": 2200,
  "port_role": "source",
  "power_role": "source",
  "data_role": "UFP",
  "vbus": 2,
  "sourcing": true,
  "dead_battery": false,
  "cfg_src": "eeprom",
  "fault": false,
  "revision": 8,
  "fw_version": "8"
}
```

**Cơ chế bảo vệ chồng lệnh PD:** chỉ 1 lệnh đổi vai trò nguồn PD (`enable_charging`/`disable_charging`) được chạy cùng lúc toàn hệ thống (biến `_pd_cmd_pending`, khoá `asyncio.Lock` trong `pd_controller.py`). Gọi chồng lên sẽ nhận **409** `{"status":"failed","error":"PD command already in progress"}`. Mỗi lệnh mất ~1.5–2 s (port ngắt/kết nối lại).

**Danh sách path được coi là "encrypted"** (đối chiếu `_ENCRYPTED_PATHS` trong `server/api.py:581-598`) — path nào **không** nằm trong danh sách này mà gọi khi đã pair sẽ bị **403** ngay ở `before_request`; gọi khi **chưa pair** mà không phải `/auth/*` sẽ bị **401**.

### 2.2. Cổng `:80` (HTTP thuần) — Admin UI (không phải giao thức của iPad, nhưng vẫn là "API điều khiển firmware" theo nghĩa quản trị/kỹ thuật)

| Method | Path | Auth | Mô tả |
|---|---|---|---|
| GET | `/admin` | session cookie | Dashboard, tự redirect `/admin/login` nếu chưa đăng nhập |
| GET | `/admin/login` | – | Form đăng nhập |
| POST | `/admin/login` | password (form) | Kiểm mật khẩu (SHA-256 so NVS `admin/pw_hash`), set cookie `session` (HttpOnly, 30 phút, tối đa 3 phiên). Khoá 5 phút sau 5 lần sai. |
| GET | `/admin/logout` | session cookie | Huỷ session |
| POST | `/admin/network` | session cookie | Lưu DHCP/static IP vào NVS `netcfg`, **tự reboot** ~800 ms sau |
| POST | `/admin/retrust` | session cookie | **Unpair + nạp public key iPad mới** trong 1 bước — cách duy nhất để đổi "danh tính" client được phép điều khiển mà không cần vào REPL/UART |
| POST | `/admin/unpair` | session cookie | Unpair, giữ nguyên key cũ (client cũ có thể pair lại) |
| POST | `/admin/password` | session cookie + current password | Đổi mật khẩu admin |
| GET | `/admin/logs` | session cookie | Xem log ring buffer dạng HTML |
| POST | `/admin/logs/clear` | session cookie | Xoá log ring buffer |

Nguồn: `server/admin.py` toàn bộ file.

### 2.3. Bảng tổng hợp mã lỗi HTTP dùng xuyên suốt

| Mã | Khi nào |
|---|---|
| 200 | Thành công |
| 400 | Payload sai định dạng/giá trị (vd `value` không phải 0/1, `watts` không phải 15/20, nonce/hash/sig sai ở pairing) |
| 401 | Chưa pair mà gọi path không thuộc `/auth/*`; hoặc giải mã AES-GCM thất bại (`authentication failed`, `malformed request`, `not paired`) |
| 403 | Gọi `/auth/pair`/`/auth/trust` khi **đã** pair; hoặc gọi path không nằm trong `_ENCRYPTED_PATHS` khi **đã** pair |
| 409 | Lệnh PD (`enable`/`disable` charging) đang chạy dở, gọi chồng |
| 500 | Lỗi bất ngờ khi đọc I2C PD hoặc set relay (hiếm, có try/except riêng) |
| 503 | PD controller chưa sẵn sàng (`pd.get() is None` — TPS25751 chưa vào APP mode) |

### 2.4. Khác biệt so với `doc/architecture.md` (lưu ý khi dùng tài liệu cũ)

- Tài liệu cũ **không liệt kê** `/logs`, `/version`, `/admin/logs`, `/admin/logs/clear` — đây là các route có thật trong code nhưng được thêm sau khi viết doc.
- Tài liệu cũ mô tả cơ chế sạc bằng `Gaid` (warm reset) + `SWSr`/`SWSk` (PR_Swap). **Code thực tế hiện tại** (`drivers/pd_controller.py`) đã đổi hẳn sang ghi trực tiếp thanh ghi `PORT_CONFIG` (Source-only/Sink-only) vì chuỗi lệnh cũ không hoạt động khi cờ `dead_battery` đang bật (IC từ chối PR_Swap sang Source). Không ảnh hưởng tới **hình dạng API** (endpoint/path/payload không đổi), chỉ ảnh hưởng hành vi bên trong — nhưng ảnh hưởng tới các trường trả về trong `pd` (`dead_battery`, `cfg_src`, `sourcing`, `vbus`... là các trường mới, không có trong tài liệu cũ).

---

## 3. Giao thức bảo mật chi tiết

Phần này bắt buộc phải hiểu đúng 100% trước khi cài đặt lại client (web) ở Phần 4 — mọi sai lệch nhỏ (thứ tự nối chuỗi, độ dài IV, AAD...) đều khiến ESP32 từ chối yêu cầu.

### 3.1. Pha Pairing (RSA-2048 PKCS#1 v1.5, một lần duy nhất, không đảo ngược trừ unpair)

```
Client ("iPad")                                          ESP32
──────────────────                                        ─────
1. nonce = uuid4().hex
2. canonical = nonce.encode()
3. hash = base64(SHA-256(canonical))
4. signature = base64(RSA-PKCS1v1.5-SHA256-Sign(canonical, iPad_private_key))
   POST /auth/pair {nonce, hash, signature} ──────────────►
                                                            - 403 nếu đã pair
                                                            - kiểm nonce chưa dùng (chống replay)
                                                            - verify SHA-256(canonical) == hash
                                                            - verify RSA signature bằng ipad_pub.pem
                                                            - sinh OTP 6 số (HOTP counter=0, secret ngẫu nhiên
                                                              mỗi lần thử) → hiện trên LCD vật lý
                                                            - sinh session_token (16 byte random, base64)
                        {session_token} ◄───────────────────

   [Người vận hành đọc OTP trên màn LCD, nhập vào client]

5. canonical = (otp + ":" + session_token + ":" + nonce2).encode()   ⚠ nonce MỚI, khác nonce bước 1
6. hash2 = base64(SHA-256(canonical))
7. signature2 = base64(RSA-Sign(canonical))
   POST /auth/trust {otp, session_token, nonce: nonce2, hash: hash2,
                     signature: signature2} ───────────────►
                                                            - 403 nếu đã pair
                                                            - kiểm nonce2 chưa dùng
                                                            - verify hash + signature
                                                            - so session_token khớp bước 1
                                                            - so OTP khớp HOTP(secret, counter=0)
                                                            - sinh AES-256 session key (32 byte random)
                                                            - RSA-PKCS1v1.5-Encrypt(aes_key, iPad_public_key)
                                                            - lưu NVS: auth/aes_key, auth/paired=1  ← KHOÁ VĨNH VIỄN
                        {enc_session_key(base64)} ◄──────────
8. aes_key = RSA-Decrypt(enc_session_key, iPad_private_key)
9. Lưu aes_key an toàn (Keychain trên iOS thật)
```

**Sau bước này, `/auth/pair` và `/auth/trust` trả 403 vĩnh viễn** cho tới khi có `/auth/unpair` (cần AES session hợp lệ) hoặc `/admin/unpair`/`/admin/retrust` (cần mật khẩu admin, HTTP thuần cổng 80).

Chi tiết chữ ký số:
- Thuật toán: **RSA-2048, PKCS#1 v1.5, SHA-256** (không phải PSS, không phải OAEP).
- `iPad_public_key` được nạp sẵn vào ESP32 dưới dạng file `/ipad_pub.pem` (PEM SubjectPublicKeyInfo) tại thời điểm build/flash firmware (`make rsa` sinh cặp khoá, `make firmware dev|release` nhúng public key vào ảnh `-vfs.bin`).
- **"Dev mode"**: nếu ESP32 không tìm thấy `/ipad_pub.pem` (`auth/pairing.py:load()`), nó **bỏ qua hoàn toàn việc kiểm tra chữ ký** (`_verify_hash_and_sig` trả `True` luôn) và trả AES key **không mã hoá** ở bước trust. Đây là chế độ chỉ dùng khi phát triển — **không được để sót trên thiết bị production**.
- Chống replay ở pha pairing dùng **chung** một tập `_seen_nonces` (tối đa 200 phần tử, FIFO) với pha giao tiếp mã hoá — nonce dùng ở `/auth/pair` không được trùng với bất kỳ nonce nào đã dùng trước đó (kể cả ở API thường).
- Cửa sổ chờ nhập OTP: **300 giây** (`_PAIRING_TIMEOUT`), quá hạn tự động revert về `unpaired`.

### 3.2. Pha giao tiếp bình thường (AES-256-GCM, áp dụng cho toàn bộ Nhóm C ở mục 2.1)

**Request envelope** (client gửi lên):
```json
{
  "iv":         "<base64, đúng 12 byte>",
  "ciphertext": "<base64, AES-256-GCM(plaintext_json)>",
  "tag":        "<base64, đúng 16 byte GCM tag>",
  "nonce":      "<chuỗi ngẫu nhiên, khuyến nghị uuid4().hex>"
}
```

**Response envelope** (ESP32 trả về, không có `nonce`):
```json
{ "iv": "...", "ciphertext": "...", "tag": "..." }
```

Quy tắc bắt buộc:
1. **AAD (Additional Authenticated Data) của GCM = `nonce.encode()` trên request.** Đây là điểm dễ làm sai nhất — nếu không truyền `nonce` làm AAD khi encrypt/decrypt, tag sẽ luôn sai. Response **không có AAD**.
2. `nonce` phải là **chuỗi mới, chưa từng dùng** (kiểm tra trong cùng tập `_seen_nonces` dùng chung với pairing, tối đa 200 phần tử — nonce cũ nhất bị loại khi đầy). Vì set này **chỉ ở RAM**, sau khi ESP32 reboot mọi nonce cũ coi như "chưa dùng" lại — không cần bộ đếm bền (persistent counter).
3. IV **phải ngẫu nhiên mỗi lần** (`os.urandom(12)`), 12 byte đúng chuẩn NIST SP 800-38D.
4. Plaintext là JSON UTF-8 thường (`json.dumps(...).encode()`), không có padding đặc biệt.
5. Thuật toán là **AES-256-GCM chuẩn** (CTR + GHASH đúng chuẩn NIST) — hoàn toàn tương thích với `AESGCM` của Python `cryptography` (đã dùng trong `test_pairing.py`/`test_ota.py`) và với `crypto.createCipheriv('aes-256-gcm', ...)` của Node.js hay `SubtleCrypto.encrypt({name:'AES-GCM', iv, additionalData}, ...)` của trình duyệt. **Đây là tin tốt cho Phần 4** — không cần cài lại AES-GCM thủ công.

### 3.3. Vấn đề TLS

- Cert TLS ở cổng `:8080` là **tự ký** (`make tls` sinh `tls_server_cert.pem`/`tls_server_key.pem`, không qua CA công cộng nào).
- Script test Python bỏ qua kiểm tra cert bằng `verify=False` / `ssl.CERT_NONE`. **Trình duyệt không cho phép JavaScript làm điều tương đương** — `fetch()`/`XMLHttpRequest` sẽ báo lỗi mạng cứng (`ERR_CERT_AUTHORITY_INVALID`) và không có API nào để bỏ qua từ code. Đây là ràng buộc kiến trúc quan trọng nhất cho Phần 4.

### 3.4. Vấn đề RSA PKCS#1 v1.5 encryption trên trình duyệt

- Bước `/auth/trust` trả về `enc_session_key` = **RSA-PKCS#1 v1.5 encryption** (không phải RSA-OAEP) của AES key.
- **Web Crypto API (`SubtleCrypto`) của mọi trình duyệt chỉ hỗ trợ `RSASSA-PKCS1-v1_5` để KÝ, không hỗ trợ `RSAES-PKCS1-v1_5` để MÃ HOÁ/GIẢI MÃ** (bị loại bỏ khỏi spec vì rủi ro tấn công Bleichenbacher). Trình duyệt **không thể** tự giải mã `enc_session_key` bằng crypto gốc — bắt buộc phải dùng thư viện big-integer JS (vd `node-forge`, `jsencrypt`) hoặc (khuyến nghị) đẩy việc này xuống backend.
- Ngược lại, **Node.js `crypto` và Python `cryptography`** đều hỗ trợ đầy đủ RSA PKCS#1 v1.5 cả hai chiều (ký/verify **và** encrypt/decrypt) — đây là lý do Phần 4 khuyến nghị kiến trúc có backend trung gian.

---

## 4. Kế hoạch xây dựng Website thay thế iPad

### 4.0. Kết luận kiến trúc (đọc trước, chi tiết ở dưới)

Không thể làm một trang web "thuần frontend" nói chuyện thẳng tới ESP32 theo đúng cách iPad làm, vì 2 lý do kỹ thuật cứng nêu ở 3.3 và 3.4 (cert tự ký + RSA PKCS1v1.5 encrypt không có trong Web Crypto). Kiến trúc bắt buộc phải có **một backend trung gian ("Device Gateway")** đóng vai trò là "iPad" thật sự về mặt giao thức — giữ khoá riêng RSA, nói AES-GCM/HTTPS-tự-ký với ESP32 — còn trình duyệt chỉ nói HTTP(S)/WebSocket bình thường với backend đó. May mắn là điều này **giải quyết luôn** bài toán "nhiều người dùng web cùng điều khiển 1 cửa" mà firmware vốn không hỗ trợ (firmware chỉ chấp nhận đúng 1 danh tính client) — backend là danh tính đó, người dùng web chỉ là các phiên đăng nhập vào backend.

```
┌───────────────┐        ┌───────────────┐        ┌──────────────────┐        ┌─────────────┐
│  Trình duyệt   │ HTTPS  │   Web Frontend │  REST/  │  Device Gateway   │ HTTPS  │  ESP32-S3   │
│ (nhiều người   │◄──────►│   (SPA tĩnh)   │  WS     │  (Backend service)│  8080  │  (1 hoặc    │
│  dùng, nhiều   │  cert  │                │────────►│  giữ RSA-2048     │◄──────►│  nhiều máy) │
│  phiên)        │  thật  │                │         │  giữ AES key/phòng│  self- │             │
└───────────────┘        └───────────────┘        │  hàng đợi lệnh PD │  signed│             │
                                                    └──────────────────┘        └─────────────┘
                                                            │
                                                    ┌──────────────────┐
                                                    │  DB: devices,     │
                                                    │  users, sessions, │
                                                    │  audit log, OTA   │
                                                    └──────────────────┘
```

### 4.1. Các ràng buộc bắt buộc phải thiết kế theo (rút ra từ source code)

| # | Ràng buộc | Hệ quả thiết kế |
|---|---|---|
| C1 | Cert TLS cổng 8080 tự ký, trình duyệt không bỏ qua được | Trình duyệt **không bao giờ** gọi thẳng ESP32; mọi giao tiếp qua Device Gateway. Gateway nói TLS với ESP32 bằng thư viện HTTP có tuỳ chọn tắt verify (như `requests(verify=False)`), nhưng nên **pin theo SHA-256 fingerprint** của cert từng thiết bị (lưu lúc provisioning) thay vì tắt verify hoàn toàn mù quáng — chống MITM trong LAN. |
| C2 | RSA PKCS1v1.5 encrypt không có trong Web Crypto | Toàn bộ crypto pairing (ký + giải mã AES key) **phải** chạy ở Gateway (Node.js `crypto` hoặc Python `cryptography`), không được đẩy xuống trình duyệt. |
| C3 | "One device, one client" ở tầng firmware | Gateway = "iPad" logic duy nhất với mỗi ESP32. Người dùng web là các **session ứng dụng**, không phải các "pairing" riêng biệt. Nếu vẫn muốn giữ 1 app iPad thật song song điều khiển cùng thiết bị → **không được**, trừ khi sửa firmware (ngoài phạm vi). Phải chọn: web thay thế hẳn iPad, hoặc song song bằng cách chính iPad cũng gọi qua Gateway thay vì gọi thẳng ESP32. |
| C4 | Pairing bước 2 cần đọc OTP trên LCD vật lý | Không thể "Thêm thiết bị" 100% từ xa. UI "Add Device" của web bắt buộc có bước: người tại hiện trường nhập OTP đọc từ màn ESP32 vào form web trong vòng 300 giây. |
| C5 | Nếu thiết bị **đã pair** với một iPad thật trước đó | Phải vào `http://<ip-thiết-bị>/admin` (LAN, cần mật khẩu admin) → "Re-trust iPad", dán PEM public key của Gateway để **unpair app cũ + nạp khoá mới trong 1 bước**. Đây là hành động thủ công, cần thực hiện cho **từng thiết bị**, không thể làm hàng loạt từ xa qua Internet nếu admin UI không mở ra ngoài LAN. |
| C6 | `_peer_monitor` watchdog tự reboot ESP32 nếu peer (client đã pair) không trả lời ping ICMP trong ~4 s×3 lần | Địa chỉ IP mà firmware ping chính là IP của **client cuối cùng giải mã request thành công** (`pairing.set_peer_ip`). Nếu để browser gọi trực tiếp (giả sử không vướng C1) thì IP đổi liên tục theo từng máy người dùng → watchdog sẽ liên tục "mất peer" → reboot thiết bị vô cớ. Khi dùng Gateway (1 IP cố định, luôn online, luôn trả lời ICMP) thì watchdog này lại **hoạt động đúng như thiết kế** — đây là bằng chứng củng cố thêm cho kiến trúc Gateway, không chỉ là giải pháp né TLS. **Yêu cầu hạ tầng:** máy chủ Gateway phải cho phép trả lời ICMP Echo từ dải mạng của ESP32 (không bị firewall chặn). |
| C7 | Chỉ 1 lệnh PD (bật/tắt sạc) chạy được cùng lúc mỗi thiết bị (409 nếu chồng) | Gateway phải có **hàng đợi lệnh theo từng thiết bị** (single-flight per device) để dịch 409 thành trải nghiệm "đang xử lý, vui lòng đợi" thay vì lỗi thô; chặn double-click ở UI. |
| C8 | Admin UI (`:80`) không mã hoá transport | Kênh Gateway ↔ Admin UI (nếu Gateway cần tự động hoá retrust/network config) phải nằm trong mạng tin cậy (VLAN riêng/VPN), không được đi qua Internet công cộng ở dạng thô. |
| C9 | ESP32 không có cơ chế push (không MQTT/WebSocket) | Gateway phải **poll** `/status`, `/power/pd/status` định kỳ; muốn cập nhật real-time cho nhiều tab trình duyệt thì Gateway poll 1 lần rồi phát lại (fan-out) qua WebSocket/SSE, không để mỗi tab tự poll thẳng thiết bị. |
| C10 | Khoá RSA private + AES session key = "chìa khoá vật lý mở cửa" | Phải lưu trong secret store (Vault/KMS/OS keyring được mã hoá at-rest), có audit log **mọi** lệnh mở khoá/tắt mở khoá, giới hạn quyền (RBAC) ai được gọi `/auth/unpair`, `/admin/retrust`, `/relays/maglock`. |
| C11 | `nonce` chống replay chỉ sống trong RAM ESP32, tối đa 200 phần tử | Gateway phải tự sinh nonce ngẫu nhiên **duy nhất mỗi request** (uuid4), không tái sử dụng, không cần đồng bộ counter với thiết bị. |
| C12 | `/power/pd/profile` hiện không có hiệu lực tức thời (ghi thanh ghi, không phát lệnh 4CC) | UI không nên hiển thị đổi trạng thái ngay sau khi gọi — nên hiển thị "đã đặt, sẽ áp dụng ở lần cắm cáp tiếp theo" hoặc ẩn tính năng này cho tới khi firmware sửa (xem `checklist.md` mục 8). |

### 4.2. Phạm vi (Scope) đề xuất

**Trong phạm vi (V1):**
- Quản lý danh sách thiết bị (thêm/xoá/xem trạng thái nhiều phòng/nhiều ESP32).
- Luồng pairing thiết bị mới qua web (có bước nhập OTP thủ công).
- Điều khiển: mở/khoá cửa, bật/tắt sạc, xem trạng thái PD, xem log.
- Cập nhật firmware qua OTA từ giao diện web (upload file `.bin`, theo dõi tiến trình).
- Quản lý người dùng nội bộ (đăng nhập, phân quyền: Viewer / Operator / Admin).
- Audit log mọi hành động điều khiển.

**Ngoài phạm vi (V1, cân nhắc sau):**
- Sửa firmware để hỗ trợ nhiều client cùng pair (đa danh tính) — hiện là giới hạn cứng của firmware.
- Truy cập Admin UI (`:80`) từ xa qua Internet công cộng (chỉ nên qua VPN nội bộ).
- Tự động hoá "Re-trust" hàng loạt từ xa (cần physical/LAN access theo C5).
- Sửa lỗi `/power/pd/profile` no-op (thuộc firmware, không thuộc web).

### 4.3. Kiến trúc chi tiết & thành phần

| Thành phần | Vai trò | Công nghệ đề xuất | Vì sao |
|---|---|---|---|
| **Web Frontend** | SPA cho end-user (Operator/Admin xem & điều khiển) | React/Vue + TypeScript, phục vụ qua HTTPS cert thật (Let's Encrypt/nội bộ) | Không liên quan gì tới cert tự ký của ESP32 — chỉ nói chuyện với Gateway. |
| **Device Gateway (BFF)** | Giữ danh tính RSA "iPad", thực hiện pairing, mã hoá/giải mã AES-GCM, poll trạng thái, hàng đợi lệnh, expose REST/WebSocket sạch cho frontend | **Khuyến nghị: Python + FastAPI** (tái dùng gần như nguyên vẹn logic crypto đã được kiểm chứng trong `test_pairing.py`/`test_ota.py` — giảm rủi ro cài sai giao thức). Thay thế được bằng Node.js + TypeScript (module `crypto` built-in hỗ trợ đủ RSA PKCS1v1.5 + AES-256-GCM) nếu team quen JS hơn. | Cả 2 stack đều native-support đủ mọi phép crypto cần thiết (không cần build lại RSA/AES thủ công như firmware phải làm trên MicroPython). |
| **Device Registry (DB)** | Lưu danh sách thiết bị, trạng thái pairing, fingerprint TLS, cấu hình mạng | PostgreSQL (hoặc bất kỳ RDBMS quen thuộc) | Quan hệ rõ ràng device–user–audit. |
| **Secret Store** | Lưu RSA private key (của Gateway/"iPad"), AES session key mỗi thiết bị | HashiCorp Vault / AWS KMS / Azure Key Vault, tối thiểu là mã hoá at-rest bằng key riêng ngoài DB | C10. |
| **Realtime layer** | Đẩy cập nhật trạng thái tới trình duyệt đang mở | WebSocket (hoặc Server-Sent Events nếu chỉ cần 1 chiều) | C9. |
| **Audit/Log store** | Ghi mọi lệnh điều khiển, ai/khi nào/thiết bị nào/kết quả | Bảng `audit_log` trong cùng DB, hoặc đẩy sang ELK/Loki nếu quy mô lớn | C10. |
| **Reverse proxy / TLS termination cho Gateway** | Cấp cert thật cho domain Gateway, terminate TLS trước khi vào app | Nginx/Caddy/Traefik | Để frontend không bao giờ đụng cert tự ký. |

### 4.4. Mô hình dữ liệu (Data model) tối thiểu

```
Device
  id                 UUID (PK)
  name               string            "Phòng họp 3A"
  mac                string            định danh phần cứng, lấy từ /status hoặc /info
  ip                 string            IP hiện tại trong LAN
  tls_fingerprint     string            SHA-256 cert :8080, ghi lúc provisioning, dùng để pin
  pairing_state       enum(unpaired, pairing, paired)
  aes_key_ref         string            con trỏ vào Secret Store, KHÔNG lưu key trần trong DB
  rsa_keypair_ref     string            con trỏ vào Secret Store (mặc định: dùng chung 1 cặp khoá
                                        cho cả fleet, giống cách firmware/Makefile "make rsa" reuse
                                        khoá cho mọi unit; có thể nâng cấp per-device sau)
  last_seen_at        timestamp         cập nhật mỗi lần /status thành công
  last_health         jsonb             snapshot pd/relay/network gần nhất
  created_at, updated_at

User
  id, email, password_hash, role(enum: viewer, operator, admin), created_at

DeviceUserAccess         (nếu cần phân quyền theo từng phòng thay vì toàn hệ thống)
  device_id, user_id, role

AuditLog
  id, device_id, user_id, action, payload_summary, result, created_at

OtaJob
  id, device_id, firmware_version, sha256, status(enum: uploading, in_progress, done, failed),
  progress_pct, started_at, finished_at, error
```

### 4.5. API hợp đồng (Input/Output contract) giữa Frontend ↔ Gateway

Đây là "API mới" mà đội frontend sẽ code theo — **khác** với API ESP32 ở Phần 2 (Gateway dịch 1-nhiều, thêm auth người dùng, bỏ hết phần crypto).

| Method | Path | Input | Output | Ghi chú |
|---|---|---|---|---|
| POST | `/api/auth/login` | `{email, password}` | `{token}` (JWT) hoặc cookie session | Auth người dùng web, tách biệt hoàn toàn với pairing thiết bị |
| GET | `/api/devices` | – (JWT) | `[{id, name, pairing_state, last_seen_at, ...}]` | Danh sách phòng theo quyền user |
| POST | `/api/devices` | `{name, ip}` | `{id, pairing_state:"unpaired"}` | Khai báo thiết bị mới vào registry (chưa pair) |
| POST | `/api/devices/{id}/pair/start` | – | `{step:"await_otp"}` | Gateway tự sinh nonce, ký, gọi `/auth/pair` tới ESP32, hiển thị OTP đang chờ nhập |
| POST | `/api/devices/{id}/pair/confirm` | `{otp}` | `{step:"paired"}` hoặc lỗi | Người vận hành đọc OTP trên LCD, nhập vào đây; Gateway tự gọi `/auth/trust`, lưu AES key vào Secret Store |
| POST | `/api/devices/{id}/unpair` | – (role=admin) | `{ok:true}` | Gọi `/auth/unpair` phía ESP32 |
| GET | `/api/devices/{id}/status` | – | `{paired, connected, ip, relays, pd}` | Trả cache mới nhất (poll nền), không block chờ ESP32 |
| WS | `/api/devices/{id}/stream` | – | event `status_update` mỗi lần đổi | Đẩy realtime, tránh mỗi tab tự poll |
| POST | `/api/devices/{id}/door` | `{value:0\|1}` (role≥operator) | `{ok, value}` | Map sang `/relays/maglock`, ghi Audit log |
| POST | `/api/devices/{id}/charging` | `{value:0\|1}` | `{ok, value}` | Map `/power/charging`, qua hàng đợi single-flight (C7) |
| GET | `/api/devices/{id}/pd` | – | object `pd` như Phần 2 | Map `/power/pd/status` |
| POST | `/api/devices/{id}/pd/profile` | `{watts:15\|20}` | `{ok}` + cảnh báo "áp dụng ở lần cắm lại" | Xem C12 |
| GET | `/api/devices/{id}/logs?limit=` | – | `{logs, resets, health}` | Map `/logs` |
| POST | `/api/devices/{id}/ota` | multipart file `.bin` | `{job_id}` | Gateway tự băm SHA-256, chia chunk 4096B, gọi `/ota/begin→chunk→apply` tuần tự |
| GET | `/api/ota-jobs/{job_id}` hoặc WS | – | `{status, progress_pct}` | Theo dõi tiến trình OTA |
| POST | `/api/devices/{id}/network-config` | `{use_dhcp, ip, subnet, gateway, dns}` (role=admin) | `{ok}` | Gateway tự đăng nhập Admin UI (`:80`, cần lưu mật khẩu admin trong Secret Store) và POST `/admin/network` |
| POST | `/api/devices/{id}/retrust` | `{new_public_key_pem}` (role=admin, hiếm dùng) | `{ok}` | Chỉ dùng khi thay hẳn cặp khoá Gateway cho 1 thiết bị cụ thể |

Toàn bộ endpoint có `role≥operator` trở lên đều **ghi AuditLog** (ai, lúc nào, thiết bị nào, giá trị trước/sau nếu có).

### 4.6. Luồng hoạt động chi tiết (Sequence)

**(a) Thêm thiết bị mới (provisioning/pairing lần đầu)**
```
Admin (web) → POST /api/devices {name, ip}                → Gateway lưu DB (state=unpaired)
Admin (web) → POST /api/devices/{id}/pair/start            → Gateway:
                                                                 - GET https://ip:8080/version (xác nhận sống, lấy fingerprint TLS, lưu vào Device.tls_fingerprint)
                                                                 - nonce1 = uuid4(); ký RSA; POST /auth/pair
                                                                 - nhận session_token, lưu tạm (Redis/DB, TTL 300s)
                                                                 - trả về frontend: "đang chờ OTP"
[Kỹ thuật viên đứng tại phòng đọc 6 số trên màn LCD]
Admin (web) → POST /api/devices/{id}/pair/confirm {otp}    → Gateway:
                                                                 - nonce2 = uuid4(); canonical = otp:session_token:nonce2; ký RSA
                                                                 - POST /auth/trust
                                                                 - RSA-decrypt enc_session_key → aes_key
                                                                 - lưu aes_key vào Secret Store, state=paired
                                                                 - trả {step:"paired"}
```

**(b) Điều khiển bình thường (vd mở cửa)**
```
User (web, đã login) → POST /api/devices/{id}/door {value:1}
  → Gateway kiểm quyền (role≥operator) → lấy aes_key từ Secret Store
  → nonce = uuid4(); encrypt {"value":1} bằng AES-256-GCM (AAD=nonce)
  → POST https://device_ip:8080/relays/maglock {iv, ciphertext, tag, nonce}
    (kết nối TLS pin theo tls_fingerprint đã lưu, không verify CA công cộng)
  → decrypt response, ghi AuditLog(user, device, action="door_open", result)
  → trả {ok:true, value:1} cho frontend + broadcast qua WebSocket cho các tab khác đang xem
```

**(c) Poll trạng thái nền + fan-out realtime**
```
Mỗi thiết bị paired: Gateway chạy 1 worker loop (vd mỗi 3-5s, có thể chỉ chạy nhanh khi có
người đang xem — subscriber count > 0 trên WebSocket của device đó):
  → POST /status (AES-GCM) → cập nhật Device.last_seen_at, last_health trong DB
  → so sánh với snapshot trước, nếu đổi → broadcast WS event tới mọi client đang subscribe
  → nếu request lỗi liên tiếp N lần → đánh dấu device "offline" trên UI (không tự ý unpair)
```

**(d) OTA update**
```
Admin (web) → upload file .bin qua POST /api/devices/{id}/ota
  → Gateway: sha256 = SHA256(file); size = len(file); session_id = uuid4()
  → POST /ota/status (đảm bảo idle)
  → POST /ota/begin {session_id, size, sha256}
  → vòng lặp: với mỗi chunk 4096B (tuần tự, đúng index):
        POST /ota/chunk {session_id, index, data(base64)}
        cập nhật OtaJob.progress_pct, phát WS progress cho frontend
  → POST /ota/apply {session_id}
  → nếu socket rớt ngay sau apply → coi là THÀNH CÔNG (giống ghi chú trong test_ota.py — verify
    SHA-256 đã chạy phía ESP32 trước khi reset)
  → đợi ~15s, gọi lại /version để xác nhận version mới → cập nhật OtaJob.status=done
```

### 4.7. Bảo mật & vận hành (Non-functional)

- **RBAC tối thiểu 3 vai trò**: Viewer (chỉ xem trạng thái), Operator (mở/khoá cửa, bật/tắt sạc), Admin (pairing/unpair/retrust/network-config/OTA). `/auth/unpair`, `/admin/retrust` nên yêu cầu thêm bước xác thực lại (re-auth / MFA) vì hậu quả không thể hoàn tác từ xa (phải quay lại LAN admin UI để pair lại).
- **Audit log bất biến** (append-only) cho mọi lệnh điều khiển cửa — đây là hệ thống khoá cửa vật lý, cần khả năng trả lời "ai mở cửa phòng nào lúc mấy giờ" bất kỳ lúc nào.
- **Rate limit** theo user và theo device cho các endpoint điều khiển, tránh spam gây 409 hàng loạt hoặc vô tình trigger watchdog phía ESP32.
- **Không bao giờ** để frontend thấy raw AES key, RSA private key, hay mật khẩu Admin UI — toàn bộ nằm ở Gateway/Secret Store.
- **Kênh Gateway ↔ ESP32** nằm trong LAN/VLAN riêng hoặc qua VPN site-to-site nếu Gateway host ở cloud — vì Admin UI (`:80`) không có TLS (C8) và vì watchdog ping ICMP (C6) cần đường truyền ổn định độ trễ thấp.
- **Giám sát vận hành**: định kỳ kéo `/logs` (field `health`) về hệ thống log tập trung để phát hiện sớm heap thấp / watchdog reset bất thường của từng thiết bị — các dấu hiệu này đã có sẵn trong log firmware, chỉ cần Gateway kéo về đều đặn.
- **Xử lý khi thiết bị mất kết nối**: KHÔNG tự động gọi `/auth/unpair` khi mất kết nối tạm thời — chỉ đánh dấu "offline" trên UI. Việc unpair chỉ nên là hành động thủ công có chủ đích của Admin.

### 4.8. Kế hoạch triển khai theo giai đoạn

| Giai đoạn | Nội dung | Đầu ra (Definition of Done) |
|---|---|---|
| **0 — Nền tảng** | Dựng Gateway skeleton (FastAPI/Express), DB schema (4.4), Secret Store, cấu hình TLS thật cho Gateway | Gateway chạy, có `/health`, kết nối DB/Secret Store thành công |
| **1 — Crypto core** | Cài lại chính xác 3.1–3.2 trong Gateway: hàm ký RSA, giải mã enc_session_key, encrypt/decrypt AES-GCM với AAD=nonce | Viết unit test đối chiếu **trực tiếp** với `firmware/test/test_pairing.py` làm oracle — cùng input phải ra cùng canonical string/hash/signature (đảm bảo không lệch giao thức) |
| **2 — Pairing flow qua web** | API `/api/devices`, `/pair/start`, `/pair/confirm`; UI nhập OTP | Pair thành công 1 thiết bị thật từ web, verify bằng cách gọi thẳng `/status` qua Gateway |
| **3 — Điều khiển cơ bản** | `/door`, `/charging`, `/pd`, `/logs`; UI dashboard 1 thiết bị | Test toàn bộ case tương ứng nhóm **E** (pairing/security) và **G** (PD) trong `checklist.md` nhưng gọi qua web thay vì `test_pairing.py` |
| **4 — Realtime + đa thiết bị** | WebSocket fan-out, danh sách nhiều phòng, worker poll nền | Mở 2 tab trình duyệt khác nhau, thao tác ở tab A thấy cập nhật ở tab B trong <2s |
| **5 — RBAC + Audit** | Users, roles, audit log, re-auth cho hành động nguy hiểm | Mọi thao tác cửa có dòng audit tra được; user Viewer không gọi được `/door` |
| **6 — OTA qua web** | Upload `.bin`, tiến trình realtime | Test case nhóm **J** trong `checklist.md` (OTA thành công, huỷ giữa chừng, sai sha256) chạy qua web pass hết |
| **7 — Hardening** | TLS fingerprint pinning thay verify=False mù quáng, rate limit, penetration test nội bộ | Báo cáo test bảo mật không còn lỗ hổng "ai cũng gọi được /door" |

### 4.9. Kế hoạch kiểm thử

Tái sử dụng trực tiếp bộ test case đã có trong `checklist.md` (nhóm **E** — pairing/bảo mật API, **G** — sạc PD, **H** — watchdog, **J** — OTA) làm **acceptance test cho Gateway**, chỉ thay "gọi bằng `test_pairing.py`/`test_ota.py`" bằng "gọi qua API web". Bổ sung thêm các case riêng cho tầng web:

| ID | Kiểm tra | Kỳ vọng |
|---|---|---|
| W1 | 2 user cùng bấm mở cửa gần như đồng thời | Không lỗi 500 phía Gateway; ESP32 chỉ nhận đúng số lệnh cần thiết |
| W2 | Trình duyệt gọi thẳng `https://esp32-ip:8080/version` | Bị chặn bởi cảnh báo cert (xác nhận đúng ràng buộc C1, không phải bug) |
| W3 | Ngắt kết nối Gateway ↔ ESP32 (rút mạng) | UI chuyển "offline" trong vài giây, không unpair, tự phục hồi khi có mạng lại (poll worker retry) |
| W4 | Gọi API điều khiển bằng user role=viewer | 403 phía Gateway, không có request nào lọt xuống ESP32 |
| W5 | Pairing timeout (không nhập OTP trong 300s) | Gateway nhận biết pairing hết hạn, cho phép bấm "pair/start" lại từ đầu |
| W6 | So khớp canonical string/chữ ký giữa Gateway và `test_pairing.py` với cùng khoá test | Byte-for-byte giống nhau |

### 4.10. Rủi ro & câu hỏi cần chốt trước khi code (Open Questions)

> **Cập nhật 2026-09-15:** câu hỏi #1 và #4 (bản gốc) đã được chốt — xem [Phần 5](#5-bổ-sung-mô-hình-kiosk-pwa--add-to-home-screen-trên-ipad): web **thay thế hẳn** iPad app (mô hình kiosk cố định theo phòng), và Gateway **đặt tại chỗ (on-site), cùng LAN** với ESP32. Danh sách dưới đây là các câu hỏi **còn mở** sau khi đã chốt 2 điểm trên.

1. **1 cặp khoá RSA dùng chung cho cả fleet, hay mỗi thiết bị 1 cặp?** Dùng chung đơn giản (khớp cách `make rsa` hiện tại đang reuse khoá), nhưng nếu private key của Gateway bị lộ thì ảnh hưởng **toàn bộ** thiết bị cùng lúc — nên cân nhắc phân vùng theo cụm tòa nhà thay vì one-key-for-all.
2. **Admin UI (`:80`) có cần Gateway tự động hoá (network-config/retrust) hay chỉ để con người làm tay?** Ảnh hưởng tới việc có cần lưu mật khẩu Admin UI của từng thiết bị trong Secret Store hay không (thêm 1 bề mặt tấn công).
3. **Chính sách fail-safe khi mất mạng/mất Gateway** (liên quan `checklist.md` mục 1 — hiện đang tắt tính năng "mất mạng thì mở cửa" vì chưa xác nhận cực tính đấu dây relay thực tế). Web/Gateway không thể tự quyết định thay — cần chốt cùng đội phần cứng trước khi UI hiển thị bất kỳ thông điệp "an toàn" nào liên quan tới trạng thái mất kết nối.
4. **`/power/pd/profile` no-op** (mục C12) — có cần chờ firmware sửa trước khi đưa tính năng đổi profile 15W/20W lên UI, hay cứ để "best-effort" kèm cảnh báo?
5. Các câu hỏi mới phát sinh từ mô hình kiosk — xem cuối [Phần 5](#5-bổ-sung-mô-hình-kiosk-pwa--add-to-home-screen-trên-ipad).

---

## 5. Bổ sung: mô hình Kiosk PWA — "Add to Home Screen" trên iPad

Phần này cụ thể hoá Phần 4 theo 2 quyết định đã chốt với người yêu cầu (2026-09-15):

- **Mạng:** iPad điều khiển luôn ở **cùng LAN/WiFi nội bộ** với ESP32 (không cần điều khiển từ Internet/4G bên ngoài).
- **Mô hình dùng:** **Kiosk cố định theo phòng** — mỗi iPad gắn cố định 1 phòng, không đăng nhập từng nhân viên.

Ý tưởng "làm 1 trang web, public ra, iPad tải xuống và Add to Home Screen để chạy như app" chính là mô hình **PWA (Progressive Web App)**. Về mặt kỹ thuật, nó **không thay đổi** kết luận kiến trúc ở mục 4.0 (vẫn bắt buộc có Device Gateway vì C1/C2) — nó chỉ là cách đóng gói/cài đặt cho **Web Frontend** đã mô tả ở mục 4.3. Phần dưới đây thay thế/chi tiết hoá lại mục 4.3–4.6 cho đúng bối cảnh kiosk.

### 5.1. PWA trên Safari/iPadOS — nó thực sự làm được gì, và không làm được gì

"Add to Home Screen" trên iPadOS tạo ra một icon chạy trang web ở chế độ **standalone** (ẩn thanh địa chỉ Safari, giống app thật), nhưng **bên trong vẫn là WebKit/Safari sandbox 100%**. Hệ quả:

| Vẫn làm được | Không làm được (khác native app) |
|---|---|
| `fetch`/`XMLHttpRequest`/`WebSocket` tới HTTPS có cert hợp lệ | Bỏ qua cảnh báo cert tự ký (giống C1 — không có cách nào lách bằng JS) |
| `localStorage`/`IndexedDB` lưu dữ liệu cục bộ trên iPad | Đảm bảo dữ liệu **không bao giờ** bị xoá — Safari có thể dọn dữ liệu web nếu lâu ngày không dùng (ITP); PWA đã "Add to Home Screen" được ưu tiên hơn tab thường nhưng **không tuyệt đối bền** |
| Service Worker để cache tài nguyên tĩnh (chạy được offline UI, không chạy được lệnh thật vì cần mạng LAN) | Chạy nền thật sự khi app bị đóng (không có background fetch định kỳ đáng tin cậy như native) |
| Web Push (chỉ từ iOS 16.4+, và chỉ khi đã Add to Home Screen) | Truy cập Bluetooth/NFC (Web NFC không tồn tại trên Safari), Camera QR (có `getUserMedia` nhưng là tính năng khác, cần code riêng nếu cần) |
| Toàn màn hình, giấu thanh Safari, khoá hướng dọc (`orientation` trong manifest) | Tự khoá cứng người dùng không thoát ra Safari/Home — muốn kiosk thật 100% (không ai vuốt ra ngoài được) phải dùng **Guided Access** hoặc **MDM Single App Mode** của iOS (xem 5.6), đây là tính năng của hệ điều hành, không phải của web app |

> **Rủi ro cần xác nhận riêng:** repo này không có mã nguồn app iPad gốc (chỉ có firmware ESP32 + 2 script Python mô phỏng client). Nếu app iPad gốc từng dùng tính năng ngoài phạm vi web (Bluetooth, NFC, quét thẻ, Camera...), phần đó **không** thể thay thế bằng PWA và cần xác nhận trước khi cam kết "web thay thế 100% iPad". Theo toàn bộ giao thức đọc được từ firmware, iPad chỉ cần network (HTTPS) — không có yêu cầu phần cứng nào khác từ phía ESP32.

### 5.2. Yêu cầu kỹ thuật tối thiểu để iPad "Add to Home Screen" chạy như app

1. **HTTPS bắt buộc** với **chứng chỉ hợp lệ** (không phải tự ký) — Service Worker chỉ chạy trên "secure context". Vì Gateway/Frontend đặt on-site (LAN), có 2 cách lấy cert hợp lệ mà **không cần mở ra Internet công cộng**:
   - **Khuyến nghị:** mua/đăng ký 1 tên miền thật (vd `ste-kiosk.congty.com`), cấp chứng chỉ **Let's Encrypt qua DNS-01 challenge** (không yêu cầu server phải public ra Internet để xác thực), rồi trỏ tên miền đó về **địa chỉ IP nội bộ** của Gateway bằng DNS nội bộ (split-horizon DNS) hoặc file hosts quản lý qua MDM. iPad trong LAN công ty truy cập bình thường, không ai ngoài Internet chạm được tới Gateway.
   - **Phương án khác** nếu muốn thực sự public (đúng nghĩa "publish" người dùng nêu): trỏ tên miền về IP public của router công ty, port-forward vào Gateway on-site, xin cert Let's Encrypt kiểu HTTP-01 bình thường. Cách này khiến Gateway lộ ra Internet — **không cần thiết** với lựa chọn "luôn cùng LAN" đã chốt, chỉ nêu để biết là có tồn tại nếu sau này đổi ý.
2. **`manifest.json`** khai báo tên, icon, chế độ hiển thị:
   ```json
   {
     "name": "ETeams STE Kiosk",
     "short_name": "STE Kiosk",
     "start_url": "/kiosk/{roomToken}?src=a2hs",
     "display": "standalone",
     "orientation": "portrait",
     "background_color": "#0b0b0c",
     "theme_color": "#0b0b0c",
     "icons": [
       {"src": "/icons/icon-180.png", "sizes": "180x180", "type": "image/png"},
       {"src": "/icons/icon-512.png", "sizes": "512x512", "type": "image/png"}
     ]
   }
   ```
3. **Thẻ `<head>`** riêng cho Safari/iOS (Safari đọc `manifest.json` từ iOS 16 trở lên nhưng vẫn nên giữ các thẻ `apple-*` cho tương thích ngược):
   ```html
   <link rel="manifest" href="/manifest.json">
   <meta name="apple-mobile-web-app-capable" content="yes">
   <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
   <meta name="apple-mobile-web-app-title" content="STE Kiosk">
   <link rel="apple-touch-icon" href="/icons/icon-180.png">
   <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover, user-scalable=no">
   ```
   `viewport-fit=cover` + CSS `env(safe-area-inset-*)` để giao diện không bị che bởi vùng bo góc/notch khi chạy standalone.
4. **Service Worker** tối thiểu: cache-first cho JS/CSS/icon tĩnh (để mở app nhanh, không trắng màn hình khi mạng LAN chập chờn vài giây), **network-only** cho mọi gọi API điều khiển thật (không bao giờ cache lệnh mở cửa/trạng thái — dữ liệu này luôn phải tươi).
5. **Hướng dẫn cài đặt cho người lắp đặt** (vì Apple **không cho web tự hiện popup "Add to Home Screen"** — người dùng phải làm thủ công): mở Safari → vào đúng URL kiosk của phòng đó → nút Share → "Add to Home Screen". Nên có 1 trang HTML tĩnh riêng (`/kiosk/{roomToken}/setup`) hiển thị hướng dẫn từng bước kèm ảnh, hiện ra khi phát hiện đang mở bằng Safari (không phải standalone).

### 5.3. Vì mô hình là Kiosk (không đăng nhập) — thiết kế lại danh tính & phân quyền

Vì đã chọn **kiosk cố định theo phòng**, bỏ hẳn khái niệm "user đăng nhập trên iPad" ở mục 4.5 cho bề mặt kiosk. Thay bằng:

- **Mỗi phòng có 1 `kioskToken` ngẫu nhiên (≥128 bit), sinh ra khi Admin bấm "Tạo link Kiosk" cho thiết bị đó trong màn quản trị.** Link kiosk có dạng `https://ste-kiosk.congty.com/kiosk/{kioskToken}`.
- **`kioskToken` được nhúng thẳng vào `start_url` của manifest** — khi người lắp đặt "Add to Home Screen" ngay tại URL đó, icon trên Home Screen sẽ **luôn** mở đúng phòng đó, vĩnh viễn, kể cả nếu Safari sau này xoá `localStorage` (không phụ thuộc vào việc lưu trữ phía trình duyệt để "nhớ phòng nào" — đây là điểm quan trọng để tránh vé phòng bị "quên" sau khi bị dọn bộ nhớ).
- **Coi `kioskToken` là bí mật (bearer secret), không phải ID công khai:** mọi gọi API điều khiển từ trang kiosk gửi kèm token này (header `Authorization: Bearer <kioskToken>`); Gateway kiểm token còn hiệu lực + map đúng 1 `device_id`, **không cho phép** 1 token điều khiển nhiều phòng.
- **Thu hồi khi mất/hỏng iPad:** Admin bấm "Thu hồi" trên màn quản trị → token cũ vô hiệu ngay lập tức → tạo token mới + link mới → người lắp đặt phải Add to Home Screen lại từ link mới trên iPad thay thế. Đây là chi phí vận hành chấp nhận được vì sự kiện này hiếm.
- **Bề mặt Admin/Quản trị (dựng riêng, không chạy trên iPad kiosk)** vẫn giữ nguyên mô hình đăng nhập + RBAC (Viewer/Operator/Admin) như mục 4.5/4.7 — dùng bởi IT/facilities để: thêm thiết bị, pairing, sinh/thu hồi kiosk token, OTA, xem audit log toàn hệ thống. Theo lựa chọn "luôn cùng LAN" đã chốt, mặc định Admin console cũng chỉ cần truy cập trong mạng công ty (không cần VPN riêng ở V1; có thể bổ sung sau nếu IT cần quản trị từ xa).
- **Tuỳ chọn tăng cường (V2, không bắt buộc):** thêm bước nhập mã PIN 4 số trên màn kiosk trước khi "Mở cửa" để có tính trách nhiệm (accountability) — vì mặc định mô hình kiosk là "ai đứng trước iPad cũng bấm mở được", giống hệt một bàn phím/nút bấm mở cửa vật lý thông thường, nên đây là điều **users cần xác nhận có chấp nhận được không**, không phải lỗi thiết kế.

### 5.4. Kiến trúc & sơ đồ mạng cập nhật (on-site, LAN-only)

```
                         Toà nhà / LAN nội bộ công ty
┌───────────────────────────────────────────────────────────────────────────┐
│                                                                             │
│   ┌───────────────┐   HTTPS (cert hợp lệ,   ┌───────────────────────┐     │
│   │  iPad Kiosk    │   qua DNS nội bộ)       │  Gateway + Frontend   │     │
│   │  Phòng 3A      │◄───────────────────────►│  (on-site: mini-PC/   │     │
│   │  (Home Screen  │   REST + WebSocket      │  NUC/Raspberry Pi/VM) │     │
│   │  icon "STE     │                          │  - Web Frontend static│     │
│   │  Kiosk")       │                          │  - Device Gateway API │     │
│   └───────────────┘                          │  - Secret Store        │     │
│                                                │  - DB (devices, audit)│     │
│   ┌───────────────┐                          └────────────┬──────────┘     │
│   │  iPad Kiosk    │◄──────────────────────────────────────┤ AES-GCM/TLS   │
│   │  Phòng 3B      │                                        │ tự ký (pin    │
│   └───────────────┘                                        │ fingerprint)  │
│                                                              ▼               │
│   ┌───────────────┐                                 ┌───────────────┐      │
│   │  PC quản trị   │──── HTTPS (Admin console) ────► │   ESP32-S3    │      │
│   │  (IT/facility) │                                 │   3A, 3B, ...  │      │
│   └───────────────┘                                 └───────────────┘      │
│                                                                             │
└───────────────────────────────────────────────────────────────────────────┘
```

So với sơ đồ tổng quát ở mục 4.0, điểm khác biệt chính: **Gateway + Frontend nằm ngay trong toà nhà**, không có VPN/site-to-site, không có tầng "trình duyệt bất kỳ từ Internet" — mọi thiết bị nói chuyện qua switch/WiFi nội bộ. Nhờ vậy watchdog `_peer_monitor` (C6) hoạt động lý tưởng: Gateway có IP LAN cố định, độ trễ ping thấp, luôn online.

### 5.5. Cập nhật Data model & API cho lớp Kiosk

Bổ sung vào mục 4.4:

```
KioskLink
  id                UUID (PK)
  device_id          FK -> Device.id            (1 kiosk link = đúng 1 phòng)
  token_hash         string   lưu HASH của kioskToken (không lưu token trần, giống mật khẩu)
  status             enum(active, revoked)
  created_by         FK -> User.id (admin đã tạo)
  created_at, revoked_at
  last_used_at        timestamp   cập nhật mỗi lần API kiosk gọi thành công
```

Bổ sung vào bảng API mục 4.5 — nhóm endpoint riêng cho bề mặt Kiosk (auth bằng `Authorization: Bearer <kioskToken>`, KHÔNG dùng JWT user):

| Method | Path | Input | Output | Ghi chú |
|---|---|---|---|---|
| GET | `/kiosk/api/self` | – (Bearer kioskToken) | `{device:{name, pairing_state}, ...}` | Trang kiosk gọi lúc khởi động để lấy thông tin hiển thị (tên phòng...) |
| GET | `/kiosk/api/status` | – | `{paired, connected, relays, pd}` | Map `/status`, cache từ worker poll nền (không gọi trực tiếp ESP32 theo từng request) |
| WS | `/kiosk/api/stream` | – | event `status_update` | Đẩy realtime riêng cho đúng phòng đó |
| POST | `/kiosk/api/door` | `{value:0\|1}` | `{ok, value}` | Map `/relays/maglock`, ghi AuditLog với actor = `kiosk:{device_id}` thay vì user |
| POST | `/kiosk/api/charging` | `{value:0\|1}` | `{ok, value}` | Map `/power/charging` |

Nếu `kioskToken` bị revoke, mọi endpoint trên trả **401** kèm thông điệp rõ ràng để Frontend hiển thị màn "Thiết bị này đã bị thu hồi quyền truy cập — liên hệ IT" thay vì lỗi mạng chung chung.

### 5.6. Khuyến nghị vận hành phần cứng iPad kiosk

- Bật **Guided Access** (Cài đặt → Trợ năng → Truy cập có hướng dẫn) hoặc dùng **MDM** (Apple Business Manager + Jamf/Mosyle/Kandji...) đặt **Single App Mode** khoá cứng iPad chỉ chạy Safari/PWA này, không cho thoát ra Home thật, không cho vuốt mở app khác — đúng tinh thần "kiosk" mà PWA tự nó không làm được (xem bảng 5.1).
- **Auto-Lock: Never** + cắm sạc cố định qua chính cổng USB-C mà ESP32 đang cấp nguồn (tận dụng luôn tính năng sạc PD của thiết bị — trùng khớp với chính hệ thống đang phân tích).
- Service Worker/JS nên có cơ chế **tự reload định kỳ** (vd mỗi 6–12h vào khung giờ ít dùng) để tránh tích luỹ rò rỉ bộ nhớ trình duyệt lâu ngày trên iPad chạy 24/7 — độc lập với heap của ESP32 (mục theo dõi trong `checklist.md` mục I4 chỉ nói về heap firmware, không phải RAM trình duyệt).
- Trang kiosk nên tự phát hiện mất kết nối tới Gateway (WebSocket đóng) và tự retry với backoff, hiển thị rõ trạng thái "Mất kết nối — đang thử lại" thay vì đứng hình.

### 5.7. Câu hỏi mở mới phát sinh từ mô hình Kiosk

1. **Có cần PIN/xác nhận trước khi mở cửa từ kiosk không** (mục 5.3, tăng tính trách nhiệm) hay chấp nhận "ai đứng trước iPad cũng mở được" như một nút bấm vật lý thông thường?
2. **Công ty đã có sẵn giải pháp MDM cho iPad chưa** (Apple Business Manager, Jamf, Mosyle...)? Nếu chưa, cần lên kế hoạch mua/triển khai riêng để dùng Guided Access/Single App Mode ở mục 5.6 — đây là quyết định ngoài phạm vi web nhưng ảnh hưởng trực tiếp tới trải nghiệm "kiosk thật".
3. **Ai giữ hạ tầng on-site** (mini-PC/NUC/Raspberry Pi chạy Gateway) tại mỗi toà nhà — có đội IT tại chỗ để lắp đặt/bảo trì phần cứng đó không, hay cần chọn dạng đóng gói dễ thay thế (vd 1 Docker Compose duy nhất, backup/restore đơn giản)?
4. **Domain nội bộ dùng chung cho mọi toà nhà hay mỗi toà nhà 1 sub-domain riêng** (vd `bldg1.ste-kiosk.congty.com`, `bldg2...`) — ảnh hưởng cách cấp chứng chỉ Let's Encrypt (wildcard cert `*.ste-kiosk.congty.com` qua DNS-01 sẽ đơn giản hoá việc này cho nhiều toà nhà cùng lúc).

---

## 6. Có cách nào gọi thẳng ESP32 không? (Bypass C1/C2 và kiến trúc Direct/Hybrid)

Câu trả lời ngắn: **có**, cả 2 rào cản ở Phần 3.3/3.4 đều có cách lách hợp lệ (không phải hack/khai thác lỗ hổng), vì các iPad kiosk là thiết bị công ty tự quản lý, không phải trình duyệt ẩn danh của khách lạ. Phần này trình bày chính xác cách lách, và một kiến trúc **Hybrid** khuyến nghị thay thế cho "Gateway proxy toàn bộ" ở Phần 4.

### 6.1. Bypass C1 — cert TLS tự ký

Cài chứng chỉ của ESP32 (hoặc cert do 1 CA nội bộ tự tạo ký cho ESP32) làm **trusted root** trên từng iPad kiosk, thông qua 1 **configuration profile** (`.mobileconfig`):

| Cách cài | Khi nào dùng |
|---|---|
| **MDM** (Apple Business Manager + Jamf/Mosyle/Kandji...) đẩy profile hàng loạt | Nếu công ty đã/sẽ có MDM (cũng cần cho Guided Access ở mục 5.6 rồi — dùng lại luôn) |
| **Apple Configurator 2** (app macOS miễn phí) cài `.mobileconfig` qua cáp USB, từng máy | Nếu chỉ vài chục kiosk và không muốn mua MDM |
| Cài thủ công: mở Safari tới link chứa `.mobileconfig`/`.cer` → Cài đặt → **Cài đặt chung → Giới thiệu → Cài đặt Uỷ quyền Chứng chỉ** bật "Trust" | Chỉ hợp lý cho số lượng rất nhỏ, làm 1 lần |

Sau khi cài, Safari/WebKit coi cert đó hợp lệ như CA công cộng — `fetch()` gọi thẳng `https://<esp32-ip>:8080` không còn bị chặn. **Khuyến nghị cho fleet chính thức:** dùng 1 CA nội bộ tự tạo, ký lại cert cho toàn bộ ESP32 (thay `tls_server_cert.pem`/`tls_server_key.pem` sinh bởi `make tls`), rồi chỉ cần cài **đúng 1 root CA** vào mọi iPad — không phải cài lại mỗi khi có thiết bị mới hay xoay vòng cert.

#### 6.1.1. Riêng cho máy dev/mượn cá nhân — làm nhẹ, gỡ được hoàn toàn

Nếu iPad đang dùng là **máy mượn để dev/debug**, **không dùng MDM và không dùng Apple Configurator 2 ở chế độ "Prepare"** — chế độ đó **supervise (giám sát)** thiết bị, một trạng thái khó gỡ (thường phải khôi phục cài đặt gốc mới bỏ được), không phù hợp với máy không phải của mình.

Cách đúng cho trường hợp này: cài **đúng 1 file cert** (không phải MDM profile) qua cơ chế "Install Profile" có sẵn của iOS — nhẹ, không giám sát, **gỡ được bất cứ lúc nào** từ chính máy, không cần máy tính nào khác can thiệp.

```
1. Lấy sẵn file có trong repo: release/tls_server_cert.pem (cert tự ký, CN=esp32)
   Đổi đuôi thành .cer (nội dung PEM giữ nguyên, không cần convert định dạng):
     cp release/tls_server_cert.pem release/tls_server_cert.cer

2. Không có Mac để AirDrop? Dùng luôn 1 web server tạm ngay trên máy dev
   (tắt đi ngay sau khi cài xong):
     python -m http.server 8765
   Lấy IP LAN máy dev (Windows: `ipconfig`, tìm IPv4 của adapter WiFi).

3. Trên iPad (cùng WiFi), mở Safari, gõ:
     http://<ip-máy-dev>:8765/tls_server_cert.cer
   → hiện "Profile Downloaded" (chỉ mới tải về, chưa cài).

4. Cài đặt → General → VPN & Device Management → chọn hồ sơ "esp32"
   → Install → nhập mã khoá màn hình → xác nhận Install lần 2.

5. BƯỚC HAY BỊ QUÊN (bắt buộc, thiếu bước này Safari vẫn từ chối):
   Cài đặt → General → About → Certificate Trust Settings
   → bật "Enable Full Trust for Root Certificates" cho dòng "esp32".

6. Test: mở https://<ip-esp32>:8080/version trong Safari — ra JSON, hết cảnh báo.

Gỡ bỏ khi xong việc / trả máy:
   Cài đặt → General → VPN & Device Management → chọn "esp32" → Remove Profile
   → nhập mã khoá → xác nhận. Máy về nguyên trạng 100%, chủ máy tự gỡ được
   bất cứ lúc nào mà không cần bạn.
```

**Mẹo tiết kiệm công sức:** nếu chỉ đang phát triển/debug logic giao thức (pairing, AES-GCM...) chứ chưa cần kiểm thử riêng trên Safari/iPad, có thể **chưa cần đụng vào iPad** — Chrome/Edge trên máy dev cho phép bấm "Advanced → Proceed" để bỏ qua cảnh báo cert tự ký (điều Safari trên iOS không cho làm), nên test API/crypto trực tiếp trên máy tính trước sẽ nhẹ hơn nhiều. Chỉ cài trust lên iPad ở bước cuối, khi thật sự cần kiểm tra PWA chạy trên Safari thật.

### 6.2. Bypass C2 — RSA PKCS#1v1.5 decrypt trong trình duyệt

Bước duy nhất cần phép toán này là **giải mã `enc_session_key`** lúc `/auth/trust` (1 lần/thiết bị). Đây chỉ là modular exponentiation `m = c^d mod n` — hoàn toàn viết được bằng JavaScript:
- Dùng `BigInt` gốc của JS tự viết ~50-100 dòng (giống hệt cách `firmware/auth/rsa.py` làm bằng `pow(s, e, n)` thuần Python), hoặc
- Dùng thư viện có sẵn: **`node-forge`** (bản browser) có đúng hàm `privateKey.decrypt(ciphertext, 'RSAES-PKCS1-V1_5')`, hoặc `jsrsasign`.

Sau bước pairing, **mọi giao tiếp còn lại chỉ dùng AES-256-GCM** — cái này Web Crypto (`crypto.subtle.encrypt/decrypt({name:'AES-GCM', iv, additionalData})`) hỗ trợ **native, đúng chuẩn, không cần lách gì**.

### 6.3. So sánh 3 kiến trúc

| | **A — Gateway proxy toàn bộ** (Phần 4 gốc) | **B — Direct hoàn toàn** | **C — Hybrid (khuyến nghị)** |
|---|---|---|---|
| Cài trust cert lên iPad | Không cần | Bắt buộc (6.1) | Bắt buộc (6.1) |
| RSA private key nằm ở đâu | Backend/Secret Store | Trình duyệt kiosk (IndexedDB) | Backend (không giao xuống kiosk) |
| AES session key nằm ở đâu | Backend | Trình duyệt kiosk | **Cả hai**: backend giữ 1 bản (để thu hồi từ xa), kiosk giữ 1 bản (để gọi trực tiếp) |
| Gọi mở cửa hàng ngày đi qua đâu | Backend proxy | Thẳng iPad → ESP32 | **Thẳng iPad → ESP32** (không qua backend) |
| Pairing/cấp phát đi qua đâu | Backend | Cần lib RSA JS ở kiosk | Backend (RSA sign + decrypt), giao AES key xuống kiosk 1 lần |
| Thu hồi khi mất iPad | `/auth/unpair` từ xa, tức thì | Phải ra tận nơi (`/admin/retrust`) vì backend không giữ key | `/auth/unpair` từ xa, tức thì (backend vẫn giữ bản sao) |
| Audit tập trung mọi lệnh mở cửa | Có, dễ | Không có (trừ khi kiosk tự bắn log về 1 collector) | Cần kiosk chủ động gửi audit event về backend (không nằm trên đường găng của lệnh mở cửa) |
| Rủi ro nếu code web bị XSS/supply-chain | Thấp (key không có ở client) | **Cao** — lộ luôn key mở cửa vĩnh viễn cho tới khi bị thu hồi | Trung bình — lộ key của **đúng 1 phòng** đó, thu hồi được ngay từ xa |
| Cần hạ tầng 24/7 | Có (Gateway là hot path) | Không | Có, nhưng **không nằm trên đường găng** — chỉ dùng lúc pairing/quản trị, downtime backend không làm iPad ngừng mở được cửa đang hoạt động |
| Độ phức tạp vận hành | Trung bình | Thấp nhất | Trung bình (ít hơn A vì bớt việc proxy 24/7) |

**Khuyến nghị: Kiến trúc C (Hybrid).** Với bối cảnh đã chốt (LAN-only, kiosk cố định theo phòng), Hybrid vừa đơn giản hơn A (không cần Gateway chạy 24/7 làm proxy, không cần lo WebSocket fan-out vì 1 kiosk = 1 phòng = 1 người xem), vừa an toàn hơn B (thu hồi từ xa được, giới hạn thiệt hại theo từng phòng thay vì để RSA key gốc lộ hẳn ra client). Điều kiện bắt buộc đi kèm: phải triển khai được bước cài trust cert (6.1) — nếu công ty **không** có khả năng/mong muốn cài `.mobileconfig` lên từng iPad (vd không có MDM và không chấp nhận Apple Configurator), thì bắt buộc quay lại **Kiến trúc A** (Phần 4 gốc), vì đó là kiến trúc duy nhất không yêu cầu đụng gì tới cấu hình hệ điều hành của iPad.

### 6.4. Việc cần làm thêm nếu chọn Hybrid (so với Phần 4/5)

- Sinh **1 cặp RSA riêng cho mỗi phòng** (không dùng chung 1 khoá cho cả fleet như mặc định của `make rsa`) — vì giờ AES key của từng phòng nằm phân tán ở từng iPad, dùng chung 1 RSA key gốc không giúp ích gì thêm mà chỉ tăng rủi ro nếu 1 kiosk bị soi ra được cả logic pairing.
- Thêm bước "giao AES key xuống kiosk" vào cuối flow pairing ở mục 4.6(a): sau khi backend hoàn tất `/auth/trust`, trả `{aes_key_b64}` (qua kênh HTTPS thật, có xác thực phiên cấp phát tạm thời) để trang kiosk lưu vào `IndexedDB`.
- Viết lại tầng gọi API phía kiosk: thay vì gọi `/api/devices/{id}/door` (Gateway), kiosk tự build envelope AES-GCM và gọi thẳng `https://<esp32-ip>:8080/relays/maglock` bằng `crypto.subtle`.
- Endpoint `/kiosk/api/*` ở mục 5.5 vẫn giữ nguyên cho phần **quản trị/khởi tạo** (lấy `kioskToken`, lấy AES key lần đầu), nhưng **không** còn là đường đi của lệnh mở cửa hàng ngày nữa.
- Cần 1 cơ chế nhẹ để kiosk **bắn audit event** về backend sau mỗi lần gọi thành công (fire-and-forget, không chặn UI) — để không mất khả năng "ai mở cửa phòng nào lúc mấy giờ" đã cam kết ở mục 4.7.

---

## 7. Bổ sung: nhận diện khuôn mặt (Face Recognition) làm lớp xác thực mở cửa

### 7.1. Nguyên tắc thiết kế

**Không đưa bất kỳ thứ gì liên quan khuôn mặt xuống ESP32.** Firmware (MicroPython, ~150 KB heap khả dụng, không có chỗ lưu trữ/so khớp sinh trắc học) hoàn toàn không liên quan tới tính năng này. Nhận diện khuôn mặt là **một lớp xác thực đứng trước** lệnh `POST /relays/maglock {"value":1}` đã có sẵn (Phần 2.1) — kết quả `match=true/false` mới quyết định có gọi lệnh đó hay không. Việc này không đổi gì ở giao thức ESP32 đã phân tích ở Phần 2–3.

**Face ID của Apple không tái sử dụng được cho việc này** — Face ID chỉ trả lời boolean "đúng chủ sở hữu thiết bị đã đăng ký hay không"; dữ liệu sinh trắc học nằm trong Secure Enclave, không app/web nào (kể cả app native) đọc ra được để so khớp với danh sách nhiều nhân viên. Phải tự dựng pipeline riêng.

### 7.2. Luồng dữ liệu & nơi lưu trữ

```
[Enrollment — làm 1 lần/nhân viên, tại Admin console]
HR/Admin chụp ảnh nhân viên (webcam/upload)
  → chạy model trích embedding (vector ~128-512 chiều, vd FaceNet/ArcFace/dlib)
  → lưu vào DB:
      employees          (id, họ tên, chức vụ, trạng thái, ngày tạo)
      face_embeddings    (employee_id, embedding_vector, model_version, created_at)
        — TÁCH SCHEMA/QUYỀN riêng khỏi các bảng dữ liệu thường (Device/User/AuditLog)
        — mã hoá at-rest bằng key riêng (không dùng chung key với DB thường)
      face_consent       (employee_id, consent_text_version, signed_at)  — bằng chứng đồng ý

[Runtime — mỗi lần có người đứng trước kiosk]
Kiosk: getUserMedia() chụp 1 khung hình
  → (khuyến nghị) trích embedding NGAY TRÊN TRÌNH DUYỆT (TensorFlow.js/MediaPipe)
    — ảnh gốc KHÔNG rời khỏi thiết bị, chỉ vector embedding được gửi đi
  → POST /api/face/verify {embedding} tới backend
  → backend so khớp (cosine similarity/L2) với face_embeddings đang active
    trong phạm vi cho phép của phòng đó (không so với toàn bộ công ty nếu không cần)
  → match && confidence ≥ ngưỡng:
        → gọi lệnh mở cửa qua đường đã chọn ở Phần 6 (Direct/Hybrid/Gateway)
        → AuditLog(actor=employee_id, method="face", confidence, device_id, ts)
  → không match / confidence thấp:
        → hiển thị "Không nhận diện được" + PHƯƠNG ÁN DỰ PHÒNG (xem 7.4)
```

Embedding vẫn được coi là **dữ liệu sinh trắc học nhạy cảm** dù không phải ảnh (một số nghiên cứu cho thấy có thể phục hồi gần đúng ảnh khuôn mặt từ embedding) — áp dụng đầy đủ các yêu cầu bảo mật/pháp lý ở mục 7.3, không vì "chỉ là con số" mà lơi lỏng.

**Quy mô nhỏ (vài chục–vài trăm nhân viên):** so khớp cosine similarity bằng vòng lặp thường trong Postgres/ứng dụng là đủ nhanh, **không cần** vector DB chuyên dụng (Milvus/Qdrant/Pinecone) hay extension `pgvector` — chỉ cân nhắc khi lên tới hàng nghìn nhân viên hoặc cần so khớp real-time rất thấp độ trễ.

### 7.3. Yêu cầu pháp lý/tuân thủ (Việt Nam)

**Nghị định 13/2023/NĐ-CP về bảo vệ dữ liệu cá nhân** xếp "dữ liệu sinh trắc học" vào nhóm **dữ liệu cá nhân nhạy cảm**, kéo theo (tham khảo, cần bộ phận pháp lý xác nhận chi tiết áp dụng cho quy mô/ngành cụ thể):
- Cần **sự đồng ý rõ ràng, được thông báo trước** của nhân viên khi đăng ký khuôn mặt (không mặc định bật, không "coi như đồng ý" nếu đi làm).
- Có thể cần **Đánh giá tác động xử lý dữ liệu cá nhân (ĐTD)** trước khi triển khai.
- Nghĩa vụ **xoá dữ liệu** khi nhân viên nghỉ việc hoặc rút lại đồng ý.
- Yêu cầu bảo mật cao hơn dữ liệu thường (mã hoá at-rest, kiểm soát truy cập chặt, thông báo khi có sự cố lộ dữ liệu).
- Nếu dùng **API nhận diện của bên thứ ba** (AWS Rekognition/Azure Face/Google Vision...) thay vì tự host model, cần nêu rõ trong nội dung đồng ý rằng dữ liệu được **chuyển cho bên thứ ba** xử lý — thêm một lớp trách nhiệm pháp lý nữa.

Đây là quyết định cần **ký duyệt từ pháp lý/nhân sự**, không phải thuần kỹ thuật — nên đưa vào giai đoạn 0 của kế hoạch triển khai, trước khi viết code enrollment.

### 7.4. Giới hạn kỹ thuật & phương án dự phòng

- **Chống giả mạo (anti-spoofing) yếu hơn Face ID thật**: camera web chỉ là ảnh 2D thường (`getUserMedia`), **không** truy cập được cảm biến TrueDepth của iPad qua Safari — dễ bị lừa bằng ảnh in hoặc màn hình điện thoại hơn Face ID gốc. Nếu mức độ nhạy cảm của phòng cao (kho tài sản, phòng máy chủ...), nên thêm **liveness detection** (yêu cầu chớp mắt/quay đầu theo hướng dẫn) hoặc kết hợp thêm 1 yếu tố nữa.
- **Không nên là phương thức duy nhất**: ánh sáng kém, đeo khẩu trang/kính râm, hoặc model nhận nhầm đều có thể khiến người có quyền bị từ chối — luôn cần **phương án dự phòng** (mã PIN, thẻ nhân viên/NFC, hoặc nút gọi hỗ trợ) song song, không để toàn bộ quyền ra vào phụ thuộc 1 mô hình ML.
- **Hiệu năng trên iPad qua Safari**: chạy model phát hiện khuôn mặt (vd BlazeFace/MediaPipe) trong trình duyệt là khả thi và đủ nhanh cho việc chụp 1 khung hình khi có người đứng trước máy (không cần xử lý video liên tục); model trích embedding chất lượng cao (ArcFace...) nặng hơn nhưng vẫn chấp nhận được vì chỉ chạy theo sự kiện (event-triggered), không chạy liên tục.
- **Điểm cộng bất ngờ:** tính năng này giải quyết luôn câu hỏi mở đã nêu ở mục 5.3/5.7 ("ai đứng trước iPad cũng mở được cửa, không quy trách nhiệm được") — nếu nhận diện thành công, `AuditLog` ghi được **đúng danh tính nhân viên**, không cần họ đăng nhập thủ công.

### 7.5. Thành phần cần thêm vào kiến trúc (Phần 4/5/6)

| Thành phần | Vai trò |
|---|---|
| **Face Enrollment UI** (Admin console) | HR/Admin thêm/xoá/vô hiệu hoá khuôn mặt nhân viên, thu thập đồng ý |
| **Face Verify Service** | Nhận embedding từ kiosk, so khớp `face_embeddings`, trả `{match, employee_id, confidence}` — có thể là 1 route trong backend Hybrid ở Phần 6, không cần microservice riêng ở quy mô nhỏ |
| **`face_embeddings` / `face_consent` schema riêng** | Tách quyền truy cập, mã hoá riêng, chính sách retention riêng khỏi DB thường ở mục 4.4 |
| **Model chạy client-side** (TensorFlow.js/MediaPipe, bundle vào kiosk PWA) | Trích embedding ngay trên iPad, không gửi ảnh gốc đi |

---

## 8. Đánh giá plan & Kế hoạch demo rút gọn 2 tuần

> Bổ sung 2026-09-15, sau khi mục tiêu đổi sang: **triển khai nhanh để demo, có thể lược bớt firmware, hạn 2 tuần.**
>
> **Cập nhật:** phần này ban đầu chỉ có mô tả/lịch làm việc, không có code — một người đọc mới không tự chạy được. Đã tách phần "làm" ra file riêng, có code đầy đủ, copy-paste chạy được: **[`poc-huong-dan-trien-khai.md`](poc-huong-dan-trien-khai.md)**. Mục 8.1-8.4 dưới đây giữ lại làm phần **đánh giá/lý do**, không phải hướng dẫn thực thi — hướng dẫn thực thi nằm ở file kia.

### 8.1. Đánh giá Phần 1–7 cho mục tiêu này: **4/10**

Phần 1–7 được viết cho **quy mô triển khai fleet production** (nhiều phòng, RBAC, Secret Store, MDM, audit, nhận diện khuôn mặt) — nếu bám đúng lộ trình 7 giai đoạn ở mục 4.8, 2 tuần không đủ xong nổi giai đoạn 1. Điểm thấp là vì **sai kích cỡ so với mục tiêu mới**, không phải vì phân tích sai.

**Vẫn dùng được, không phải bỏ đi:**
- Phần 2–3 (API endpoint, giao thức bảo mật) — đúng và cần cho mọi kích cỡ triển khai, kể cả demo.
- Phần 6 (bypass C1/C2, kiến trúc Direct) — chính là nền cho kế hoạch rút gọn dưới đây.
- Phát hiện **dev mode** có sẵn trong firmware (từ câu hỏi trước, chưa ghi vào file) — chìa khoá để bỏ hẳn phần khó nhất (RSA) mà **không sửa 1 dòng code firmware**.

**Không dùng cho demo (giữ lại cho giai đoạn sau, nếu demo được duyệt tiếp):** Gateway/DB/Secret Store, RBAC, audit log, OTA UI, face recognition (Phần 7 — còn kéo theo vấn đề pháp lý ở mục 7.3, chắc chắn không hợp cho 2 tuần), MDM/Guided Access.

### 8.2. Kiến trúc demo: Direct + Dev mode (không sửa firmware)

- **Kiến trúc:** Direct (mục 6.3, cột B) — web gọi thẳng `https://<esp32-ip>:8080`, không Gateway, không DB, không RBAC.
- **Firmware:** dùng nguyên bản hiện có, **không sửa code**. Đưa về **dev mode** bằng cách xoá `/ipad_pub.pem` trên thiết bị (`mpremote exec "import os; os.remove('/ipad_pub.pem')"`) hoặc chỉ cần gọi `/auth/unpair`/`/admin/unpair` một lần (hành động này tự xoá file, không nạp lại) — xem chi tiết cơ chế ở `auth/pairing.py:203-210, 342-352`.
  - Hệ quả: `/auth/pair` chỉ cần gửi `{nonce}` (không cần hash/signature thật); `/auth/trust` trả `enc_session_key` là **AES key thô, base64 trực tiếp** (`atob()` là xong, không cần RSA decrypt) — loại bỏ hoàn toàn rào cản C2, **0 công sức code, 0 rủi ro**, rẻ hơn nhiều so với việc sửa firmware để bỏ AES-GCM (tốn vài ngày code+test, không cần thiết bằng bước xoá 1 file).
  - Rào cản C1 (cert tự ký) **không đổi** — vẫn cần cài trust cert lên iPad demo 1 lần (đã có hướng dẫn ở mục 6.1.1, ~10 phút, gỡ được hoàn toàn sau demo).
  - AES-256-GCM cho các lệnh điều khiển hàng ngày (`/status`, `/relays/maglock`...) **giữ nguyên**, viết bằng `crypto.subtle` (Web Crypto native, không cần thư viện, không cần lách gì — phần này chưa bao giờ là vấn đề, chỉ RSA mới là vấn đề).
- **1 thay đổi cấu hình firmware duy nhất nên làm** (không phải sửa protocol, chỉ đổi giá trị trong `config/config.py` để an toàn demo): đặt `watchdog.peer.enabled = False` (và có thể `watchdog.link_down_reset_s = 0`) — tránh ESP32 tự **reboot giữa lúc demo** nếu WiFi hội trường chập chờn khiến `_peer_monitor` nghĩ "mất iPad"; reboot sẽ mở cửa uncontrolled lúc boot (`main.py`, maglock mở mặc định khi khởi động), rất dễ gây sự cố ngay trên sân khấu.

### 8.3. Lộ trình 10 ngày làm việc (1 người code chính)

| Ngày | Việc làm |
|---|---|
| 1 | Đưa ESP32 về dev mode; cài trust cert lên iPad demo (mục 6.1.1); verify toàn bộ luồng pairing bằng curl/Postman thủ công — chưa code, chỉ xác nhận hiểu đúng giao thức bằng tay |
| 2 | Viết `esp32Client.js`: hàm pairing (dev mode, chỉ `{nonce}`), wrapper AES-256-GCM (`crypto.subtle`), hàm gọi API chung `callEncrypted(path, payload)`. Test trên Chrome/PC trước (bypass cảnh báo cert dễ hơn Safari) để đỡ phải đụng iPad liên tục |
| 3–4 | Màn hình Pairing: nhập IP ESP32 → Pair → hướng dẫn đọc OTP trên LCD → nhập OTP → hoàn tất, lưu AES key vào `localStorage` |
| 5–6 | Màn hình điều khiển: poll `/status` mỗi 2–3s, nút mở/khoá cửa, nút bật/tắt sạc, hiển thị trạng thái PD |
| 7 | Test thật trên Safari/iPad, sửa lỗi UI — đặc biệt: lệnh PD (`enable_charging`/`disable_charging`) mất ~1.5–2s, cần loading state đúng để tránh double-click gây 409 |
| 8 | (Nếu còn giờ) `manifest.json` + Add to Home Screen cho demo đẹp mắt (mục 5.2) — không bắt buộc |
| 9–10 | Test end-to-end nhiều lần (kể cả tình huống lỗi: sai OTP, mất mạng, unpair/pair lại), chuẩn bị kịch bản demo, buffer |

### 8.4. Lưu ý khi trình diễn

Nên nói rõ với người xem: **đây là bản giản lược cho mục đích demo** (bỏ RSA qua dev mode có sẵn trong firmware, kết nối thẳng không qua Gateway) — bản production cần bổ sung lại theo Phần 4–7 (RSA pairing thật với `ipad_pub.pem` hợp lệ, Gateway/Hybrid, RBAC, audit) trước khi lắp đại trà. Tránh để người nghe hiểu nhầm "xong rồi, deploy luôn được".

---

## Phụ lục — Nguồn tham chiếu

| Chủ đề | File |
|---|---|
| Danh sách route + payload chính xác | `firmware/server/api.py` |
| Admin UI routes | `firmware/server/admin.py` |
| State machine pairing + crypto | `firmware/auth/pairing.py` |
| RSA PKCS1v1.5 sign/verify/encrypt | `firmware/auth/rsa.py` |
| AES-256-GCM (CTR+GHASH thuần Python) | `firmware/auth/aes_gcm.py` |
| HOTP (OTP 6 số) | `firmware/auth/otp.py` |
| OTA state machine | `firmware/server/ota.py` |
| TPS25751 / trạng thái sạc | `firmware/drivers/pd_controller.py` |
| Cấu hình mặc định, watchdog | `firmware/config/config.py` |
| Boot sequence | `firmware/main.py` |
| Client mô phỏng iPad (tham chiếu để viết Gateway) | `firmware/test/test_pairing.py`, `firmware/test/test_ota.py` |
| Danh sách hạng mục chưa xác nhận / lỗi đã biết của firmware | `checklist.md` |
