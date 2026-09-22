# POC — Hướng dẫn triển khai (Web điều khiển ESP32 qua iPad, không backend)

> **Đây là tài liệu "làm", không phải tài liệu "phân tích".** Đọc từ trên xuống, làm theo, chạy được — không cần đọc `eteams-ste-api-va-ke-hoach-web.md` (file đó chỉ cần khi muốn hiểu "vì sao lại làm thế này" hoặc khi mở rộng lên production sau này).

---

## 0. Phạm vi & giả định (đọc trước khi bắt đầu)

- Có sẵn **1 ESP32-S3** đã flash firmware (`dev` hoặc `release`), biết IP của nó trong LAN (vd `192.168.2.50`), cổng API là `8080` (HTTPS).
- Có **1 máy để test** (khuyến nghị bắt đầu bằng PC/Chrome cho nhanh, chỉ cuối cùng mới test trên iPad thật).
- **Không có backend, không build tool, không framework.** Chỉ HTML/CSS/JS thuần — chạy bằng cách mở file hoặc serve tĩnh.
- **Không làm** trong POC này: pairing RSA thật, nhiều phòng/nhiều thiết bị, đăng nhập user, audit log, OTA, PWA/Add-to-Home-Screen (có thể thêm sau nếu còn giờ, không phải việc bắt buộc).
- **Có làm, nhưng ở mức POC:** nhận diện khuôn mặt thật (model chạy trong trình duyệt, không backend) — xem mục 8. Lưu trữ khuôn mặt bằng `localStorage` (chỉ máy này, mất khi xoá dữ liệu trình duyệt), **không** phải cách lưu cho production (xem cảnh báo pháp lý ở mục 8 và Phần 7 file kiến trúc lớn trước khi đăng ký khuôn mặt nhân viên thật).
- Kết quả cuối: 1 trang web, bấm nút, cửa thật mở/khoá, sạc thật bật/tắt.

> **Chưa muốn đụng gì tới ESP32 lúc này?** Bỏ qua mục 1-2, làm theo **mục A** ngay sau đây — chạy toàn bộ app (bấm Pair/Trust/mở cửa/sạc) bằng dữ liệu giả lập, không cần thiết bị, không cần mạng. Khi nào có ESP32 sẵn sàng (đã dev mode + cài trust cert), chỉ cần tắt 1 dòng `MOCK_MODE = true` → `false`, không phải viết lại gì.

---

## A. Chạy app ngay — Mock Mode (chưa cần ESP32)

Bộ code ở mục 5-7 đã có sẵn `MOCK_MODE`. Khi `MOCK_MODE = true`, mọi hàm gọi mạng (`pairDevMode`, `trustDevMode`, `callEncrypted`) được thay bằng hàm giả lập **chạy hoàn toàn trong trình duyệt**, tự nhớ trạng thái cửa/sạc trong biến JS (không có gì thật, mất khi tải lại trang) — đủ để build và demo UI/luồng bấm nút.

**Làm ngay:**
1. Copy 3 file ở mục 5-7 vào `poc-web/`, giữ nguyên `MOCK_MODE = true` (giá trị mặc định).
2. `cd poc-web && python -m http.server 5500`, mở `http://127.0.0.1:5500`.
3. Bấm **Pair** → mở DevTools Console (F12), sẽ thấy dòng cam `[MOCK] OTP giả lập: XXXXXX` — copy 6 số đó.
4. Dán vào ô OTP, bấm **Trust** → khu vực điều khiển hiện ra, dùng bình thường như thiết bị thật (cửa/sạc chỉ là biến số trong JS, đổi qua đổi lại được, in ra log).
5. Build/chỉnh UI, thêm màn hình, thêm nút... tuỳ ý — tất cả vẫn chạy được, không cần mạng, không cần ESP32.

**Khi có ESP32 thật và đã sẵn sàng (mục 1-2 đã làm):** mở `esp32-client.js`, đổi `const MOCK_MODE = true;` thành `false`, nhập đúng IP vào ô Base URL trên UI — mọi nút bấm tự động gọi API thật, không phải sửa `app.js`/`index.html`.

---

## 1. Đưa ESP32 về "dev mode" (chỉ cần khi chuyển từ Mock Mode sang thiết bị thật)

Dev mode = firmware **không có file `/ipad_pub.pem`** → tự bỏ qua toàn bộ kiểm tra chữ ký RSA lúc pairing (xem `firmware/auth/pairing.py:203-210` và `:342-352` nếu muốn đọc code gốc).

**Nếu thiết bị chưa từng pair:** chỉ cần đảm bảo không có file đó. Nếu không chắc, vào REPL:
```bash
mpremote connect <PORT> exec "import os; print(os.listdir('/'))"
```
Nếu thấy `ipad_pub.pem` trong danh sách:
```bash
mpremote connect <PORT> exec "import os; os.remove('/ipad_pub.pem')"
mpremote connect <PORT> reset
```

**Nếu thiết bị đã pair từ trước:** vào `http://<ip-esp32>/admin` (mật khẩu mặc định `admin` trừ khi đã đổi) → bấm **Unpair** — hành động này tự xoá `/ipad_pub.pem`, đưa thiết bị về dev mode luôn, không cần làm gì thêm.

**Xác nhận đã vào dev mode:** xem log lúc boot (serial/`mpremote connect <PORT>` rồi để ở REPL) phải có dòng:
```
[WARNING] pairing: no /ipad_pub.pem — RSA checks skipped (dev mode)
```

---

## 2. Cài trust chứng chỉ TLS (chỉ cần khi test trên Safari/iPad)

Nếu test trên **Chrome/Edge (PC)**: bỏ qua bước này, chỉ cần bấm "Advanced → Proceed to ... (unsafe)" khi trình duyệt cảnh báo — không cần cài gì.

Nếu test trên **Safari/iPad**, làm đúng 6 bước sau (tóm tắt từ mục 6.1.1 file kiến trúc lớn, không cần mở file đó):

```
1. Lấy file release/tls_server_cert.pem trong repo, đổi đuôi thành .cer
     cp release/tls_server_cert.pem release/tls_server_cert.cer
2. Serve tạm qua HTTP (trên máy dev, tắt đi sau khi cài xong):
     python -m http.server 8765
   Lấy IP LAN máy dev (Windows: `ipconfig`).
3. Trên iPad (cùng WiFi), Safari → gõ:
     http://<ip-máy-dev>:8765/tls_server_cert.cer
   → hiện "Profile Downloaded".
4. Cài đặt → General → VPN & Device Management → chọn "esp32" → Install
   → nhập mã khoá màn hình → xác nhận Install lần 2.
5. Cài đặt → General → About → Certificate Trust Settings
   → bật "Enable Full Trust for Root Certificates" cho dòng "esp32".
6. Test: mở https://<ip-esp32>:8080/version trong Safari — ra JSON là xong.
```
Gỡ khi xong: Cài đặt → General → VPN & Device Management → "esp32" → Remove Profile.

---

## 3. 6 endpoint duy nhất cần cho POC (bỏ qua toàn bộ endpoint còn lại)

| # | Gọi | Payload gửi (JSON, trước khi mã hoá nếu có) | Payload nhận về |
|---|---|---|---|
| 1 | `POST /auth/pair` | `{"nonce": "<chuỗi ngẫu nhiên>", "hash": "", "signature": ""}` | `{"session_token": "..."}` |
| 2 | `POST /auth/trust` | `{"otp": "<6 số đọc từ LCD>", "session_token": "...", "nonce": "...", "hash": "", "signature": ""}` | `{"enc_session_key": "<base64>"}` — dev mode: chính là AES key thô, không phải mã hoá RSA |
| 3 | `POST /status` | `{}` **(mã hoá AES-GCM)** | `{paired, connected, ip, relays, pd}` |
| 4 | `POST /relays/maglock` | `{"value": 0\|1}` **(mã hoá)** | `{status, relay, value}` |
| 5 | `POST /power/charging` | `{"value": 0\|1}` **(mã hoá)** | `{status, charging}` |
| 6 | `POST /power/pd/status` | `{}` **(mã hoá)** | `{status, pd:{...}}` |

Endpoint 1-2 gửi **thẳng JSON, không mã hoá**. Endpoint 3-6 phải **bọc trong envelope AES-256-GCM** — xem code ở mục 5.

---

## 4. Cấu trúc project

```
poc-web/
├── index.html
├── esp32-client.js       ← toàn bộ crypto + gọi API — copy nguyên từ mục 5, không cần sửa
├── app.js                ← nối nút bấm với esp32-client.js — mục 6
├── face-api.min.js       ← thư viện nhận diện khuôn mặt — tải ở mục 8, không tự viết
├── models/               ← file trọng số model — tải ở mục 8
└── style.css              ← tuỳ chọn, không bắt buộc
```

Chạy: `cd poc-web && python -m http.server 5500`, mở `http://<ip-máy-dev>:5500` (cùng WiFi với thiết bị test).

---

## 5. `esp32-client.js` — copy nguyên, đây là phần "khó" đã viết sẵn

```javascript
// esp32-client.js — POC only. Dev mode (không RSA), gọi thẳng ESP32.
// Không dùng cho production — xem file kiến trúc lớn trước khi mở rộng.

// true  = mọi hàm dưới đây trả dữ liệu giả lập, KHÔNG gọi mạng — build/demo UI ngay.
// false = gọi thật tới ESP32 (yêu cầu đã làm mục 1-2: dev mode + trust cert).
const MOCK_MODE = true;

const IV_LEN  = 12;  // byte
const TAG_LEN = 16;  // byte (128 bit)

// ── Mock: giả lập toàn bộ hành vi thiết bị, chạy trong bộ nhớ trình duyệt ─────
const _mock = {
  paired: false,
  sessionToken: null,
  otpExpected: null,
  maglock: 0,          // 0 = khoá, 1 = mở — khớp polarity thật
  charging: 1,
  sourcing: true,
};

function _mockDelay(ms) { return new Promise(r => setTimeout(r, ms)); }

async function _mockPair() {
  await _mockDelay(400);
  _mock.sessionToken = 'mock-' + Math.random().toString(36).slice(2);
  _mock.otpExpected  = String(Math.floor(100000 + Math.random() * 900000));
  console.log('%c[MOCK] OTP giả lập (thay cho đọc LCD): ' + _mock.otpExpected,
              'color:#e67e22;font-weight:bold;font-size:14px');
  return _mock.sessionToken;
}

async function _mockTrust(otp, sessionToken) {
  await _mockDelay(400);
  if (sessionToken !== _mock.sessionToken) throw new Error('mock: session_token không khớp');
  if (otp !== _mock.otpExpected) throw new Error('mock: OTP sai — xem Console để lấy OTP giả lập');
  _mock.paired = true;
  return 'MOCK_CRYPTO_KEY';   // placeholder — không phải CryptoKey thật, chỉ mock mới hiểu giá trị này
}

async function _mockCall(path, payload) {
  await _mockDelay(300);
  if (!_mock.paired) throw new Error('mock: chưa paired — bấm Pair rồi Trust trước');
  switch (path) {
    case '/status':
      return { status: 'ok', paired: true, connected: true, ip: '(mock)',
               relays: { maglock: _mock.maglock },
               pd: { sourcing: _mock.sourcing, mode: 'APP',
                     power_profile_w: _mock.charging ? 20 : 0 } };
    case '/relays/maglock':
      _mock.maglock = payload.value;
      return { status: 'ok', relay: 'maglock', value: _mock.maglock };
    case '/power/charging':
      _mock.charging = payload.value;
      _mock.sourcing = !!payload.value;
      await _mockDelay(1500);   // giả lập ~1.5-2s port disconnect/reconnect như PD thật
      return { status: 'ok', charging: _mock.charging };
    case '/power/pd/status':
      return { status: 'ok', pd: { sourcing: _mock.sourcing, mode: 'APP' } };
    default:
      throw new Error('mock: chưa hỗ trợ path ' + path);
  }
}

function b64encode(bytes) {
  return btoa(String.fromCharCode(...new Uint8Array(bytes)));
}
function b64decode(b64) {
  return Uint8Array.from(atob(b64), c => c.charCodeAt(0));
}
// Không cần đúng chuẩn UUID — firmware chỉ cần 1 chuỗi ngẫu nhiên, chưa dùng lần nào.
function randomNonceHex() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

async function importAesKey(rawKeyBytes) {
  return crypto.subtle.importKey('raw', rawKeyBytes, 'AES-GCM', false, ['encrypt', 'decrypt']);
}

// Request: AAD = nonce (khớp firmware: aad = nonce.encode())
async function encryptEnvelope(cryptoKey, plainObj, nonceStr) {
  const iv  = crypto.getRandomValues(new Uint8Array(IV_LEN));
  const aad = new TextEncoder().encode(nonceStr);
  const plaintext = new TextEncoder().encode(JSON.stringify(plainObj));
  const cipherBuf = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: aad, tagLength: TAG_LEN * 8 },
    cryptoKey, plaintext
  );
  const full = new Uint8Array(cipherBuf);           // Web Crypto trả ciphertext||tag dính liền
  const tag        = full.slice(full.length - TAG_LEN);
  const ciphertext = full.slice(0, full.length - TAG_LEN);
  return {
    iv: b64encode(iv), ciphertext: b64encode(ciphertext), tag: b64encode(tag),
    nonce: nonceStr,
  };
}

// Response: KHÔNG có AAD (khớp firmware: encrypt_api_response không có AAD)
async function decryptEnvelope(cryptoKey, envelope) {
  const iv         = b64decode(envelope.iv);
  const ciphertext = b64decode(envelope.ciphertext);
  const tag        = b64decode(envelope.tag);
  const combined = new Uint8Array(ciphertext.length + tag.length);
  combined.set(ciphertext, 0);
  combined.set(tag, ciphertext.length);
  const plainBuf = await crypto.subtle.decrypt(
    { name: 'AES-GCM', iv, tagLength: TAG_LEN * 8 },
    cryptoKey, combined
  );
  return JSON.parse(new TextDecoder().decode(plainBuf));
}

// ── Bước 1: POST /auth/pair (dev mode — không cần hash/signature thật) ────────
async function pairDevMode(baseUrl) {
  if (MOCK_MODE) return _mockPair();
  const nonce = randomNonceHex();
  const res = await fetch(`${baseUrl}/auth/pair`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ nonce, hash: '', signature: '' }),
  });
  const data = await res.json();
  if (!res.ok) throw new Error('pair thất bại: ' + JSON.stringify(data));
  return data.session_token;
}

// ── Bước 2: POST /auth/trust (otp đọc từ LCD) ─────────────────────────────────
async function trustDevMode(baseUrl, otp, sessionToken) {
  if (MOCK_MODE) return _mockTrust(otp, sessionToken);
  const nonce = randomNonceHex();
  const res = await fetch(`${baseUrl}/auth/trust`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ otp, session_token: sessionToken, nonce, hash: '', signature: '' }),
  });
  const data = await res.json();
  if (!res.ok) throw new Error('trust thất bại: ' + JSON.stringify(data));
  const rawKeyBytes = b64decode(data.enc_session_key);   // dev mode: AES key thô, không RSA
  return importAesKey(rawKeyBytes);
}

// ── Gọi mọi endpoint mã hoá (status, relays/maglock, power/charging, ...) ────
async function callEncrypted(baseUrl, path, cryptoKey, payloadObj = {}) {
  if (MOCK_MODE) return _mockCall(path, payloadObj);
  const nonce    = randomNonceHex();
  const envelope = await encryptEnvelope(cryptoKey, payloadObj, nonce);
  const res = await fetch(`${baseUrl}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(envelope),
  });
  const respEnvelope = await res.json();
  if (!res.ok) throw new Error(`${path} thất bại: HTTP ${res.status} ${JSON.stringify(respEnvelope)}`);
  return decryptEnvelope(cryptoKey, respEnvelope);
}
```

---

## 6. `app.js` — nối nút bấm với client

```javascript
// app.js — nối UI với esp32-client.js. Sửa selector nếu đổi id trong index.html.

let cryptoKey  = null;
let sessionTok = null;

function log(msg) {
  const el = document.getElementById('log');
  el.textContent = new Date().toLocaleTimeString() + '  ' + msg + '\n' + el.textContent;
}

document.getElementById('btnPair').onclick = async () => {
  const baseUrl = document.getElementById('baseUrl').value.trim();
  try {
    sessionTok = await pairDevMode(baseUrl);
    log('Pair OK — đọc 6 số trên màn LCD, nhập vào ô OTP.');
    document.getElementById('otpBox').style.display = 'block';
  } catch (e) { log('LỖI pair: ' + e.message); }
};

document.getElementById('btnTrust').onclick = async () => {
  const baseUrl = document.getElementById('baseUrl').value.trim();
  const otp     = document.getElementById('otp').value.trim();
  try {
    cryptoKey = await trustDevMode(baseUrl, otp, sessionTok);
    log('Trust OK — đã ghép đôi, có thể điều khiển.');
    document.getElementById('controls').style.display = 'block';
  } catch (e) { log('LỖI trust: ' + e.message); }
};

function baseUrlVal() { return document.getElementById('baseUrl').value.trim(); }

document.getElementById('btnStatus').onclick = async () => {
  try { log('Status: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/status', cryptoKey))); }
  catch (e) { log('LỖI status: ' + e.message); }
};
document.getElementById('btnOpen').onclick = async () => {
  try { log('Mở cửa: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/relays/maglock', cryptoKey, {value: 1}))); }
  catch (e) { log('LỖI mở cửa: ' + e.message); }
};
document.getElementById('btnClose').onclick = async () => {
  try { log('Khoá cửa: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/relays/maglock', cryptoKey, {value: 0}))); }
  catch (e) { log('LỖI khoá cửa: ' + e.message); }
};
document.getElementById('btnChargeOn').onclick = async () => {
  try { log('Sạc ON: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/power/charging', cryptoKey, {value: 1}))); }
  catch (e) { log('LỖI sạc ON: ' + e.message + ' (409 = đang bận, đợi 2s rồi bấm lại)'); }
};
document.getElementById('btnChargeOff').onclick = async () => {
  try { log('Sạc OFF: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/power/charging', cryptoKey, {value: 0}))); }
  catch (e) { log('LỖI sạc OFF: ' + e.message + ' (409 = đang bận, đợi 2s rồi bấm lại)'); }
};
document.getElementById('btnPdStatus').onclick = async () => {
  try { log('PD status: ' + JSON.stringify(await callEncrypted(baseUrlVal(), '/power/pd/status', cryptoKey))); }
  catch (e) { log('LỖI pd status: ' + e.message); }
};

// ── Camera + nhận diện khuôn mặt THẬT (face-api.js, chạy trong trình duyệt) ──
// Model + logic so khớp nằm ở face-recognition.js (mục 8). File này chỉ nối UI.
let camStream = null;

document.getElementById('btnCamStart').onclick = async () => {
  try {
    camStream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user' }, audio: false });
    document.getElementById('camPreview').srcObject = camStream;
    log('Camera: đã bật. Đang tải model nhận diện (vài giây, chỉ lần đầu)...');
    await loadFaceModels();   // định nghĩa trong face-recognition.js
    log('Model nhận diện: đã sẵn sàng.');
    document.getElementById('btnFaceEnroll').disabled = false;
    document.getElementById('btnFaceCheck').disabled = false;
  } catch (e) {
    log('LỖI camera/model: ' + e.message + ' — trên iPad/LAN cần HTTPS mới cấp quyền camera, xem mục 10.');
  }
};

document.getElementById('btnFaceEnroll').onclick = async () => {
  const name = prompt('Tên gắn với khuôn mặt này (chỉ lưu tạm trong trình duyệt máy này — dùng tên demo/của bạn, KHÔNG dùng dữ liệu nhân viên thật cho POC):');
  if (!name) return;
  try {
    const video = document.getElementById('camPreview');
    const count = await enrollFace(video, name);
    log(`Đã đăng ký khuôn mặt "${name}" — hiện có ${count} người trong DB (localStorage, chỉ máy này).`);
  } catch (e) { log('LỖI đăng ký: ' + e.message); }
};

document.getElementById('btnFaceCheck').onclick = async () => {
  log('Đang nhận diện...');
  try {
    const video  = document.getElementById('camPreview');
    const result = await recognizeFace(video);
    if (result.match) {
      log(`✅ Nhận diện: "${result.name}" (khoảng cách ${result.distance.toFixed(3)}, ngưỡng ${FACE_MATCH_THRESHOLD}) — đang mở cửa...`);
      await callEncrypted(baseUrlVal(), '/relays/maglock', cryptoKey, { value: 1 });
      log('Cửa đã mở qua nhận diện khuôn mặt.');
    } else {
      const distTxt = result.distance !== undefined ? ` (khoảng cách ${result.distance.toFixed(3)})` : '';
      log(`❌ Không nhận diện được — lý do: ${result.reason}${distTxt}. Dùng phương án dự phòng (mở tay).`);
    }
  } catch (e) { log('LỖI nhận diện: ' + e.message); }
};
```

---

## 7. `index.html`

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 STE — POC</title>
  <style>
    body { font-family: sans-serif; max-width: 480px; margin: 20px auto; padding: 0 12px; }
    input, button { font-size: 16px; padding: 8px; margin: 4px 0; width: 100%; box-sizing: border-box; }
    button { cursor: pointer; }
    #log { white-space: pre-wrap; background: #111; color: #ddd; padding: 10px; height: 240px;
           overflow-y: auto; font-size: 12px; border-radius: 4px; }
  </style>
</head>
<body>
  <h2>ESP32 STE — POC (dev mode)</h2>

  <input id="baseUrl" placeholder="https://192.168.x.x:8080" value="https://192.168.2.50:8080">
  <button id="btnPair">1. Pair</button>

  <div id="otpBox" style="display:none">
    <input id="otp" placeholder="6 số đọc từ LCD">
    <button id="btnTrust">2. Trust</button>
  </div>

  <div id="controls" style="display:none">
    <hr>
    <button id="btnStatus">Status</button>
    <button id="btnOpen">Mở cửa</button>
    <button id="btnClose">Khoá cửa</button>
    <button id="btnChargeOn">Sạc ON</button>
    <button id="btnChargeOff">Sạc OFF</button>
    <button id="btnPdStatus">PD Status</button>

    <hr>
    <h3 style="margin-bottom:4px">Camera &amp; Nhận diện khuôn mặt</h3>
    <video id="camPreview" autoplay playsinline muted
           style="width:100%;border-radius:6px;background:#000;display:block"></video>
    <canvas id="camCanvas" style="display:none"></canvas>
    <button id="btnCamStart">Bật Camera</button>
    <button id="btnFaceEnroll" disabled>Đăng ký khuôn mặt này</button>
    <button id="btnFaceCheck" disabled>Nhận diện &amp; Mở cửa</button>
  </div>

  <hr>
  <pre id="log"></pre>

  <script src="face-api.min.js"></script>
  <script src="face-recognition.js"></script>
  <script src="esp32-client.js"></script>
  <script src="app.js"></script>
</body>
</html>
```

---

## 8. Nhúng model nhận diện khuôn mặt thật (face-api.js)

Dùng **face-api.js** (thư viện JS dựng trên TensorFlow.js, chuyên cho phát hiện + nhận diện khuôn mặt, chạy 100% trong trình duyệt, MIT license) — không cần backend, không cần huấn luyện gì, dùng model có sẵn.

### 8.1. Tải thư viện + model (làm 1 lần)

```bash
cd poc-web
mkdir -p models

# Thư viện
curl -o face-api.min.js https://cdn.jsdelivr.net/npm/face-api.js@0.22.2/dist/face-api.min.js

# Model: chỉ cần 3 bộ — phát hiện khuôn mặt (tiny, nhẹ), landmark, và nhận diện (embedding 128 chiều)
BASE=https://raw.githubusercontent.com/justadudewhohacks/face-api.js/master/weights
for f in tiny_face_detector_model-weights_manifest.json tiny_face_detector_model-shard1 \
         face_landmark_68_model-weights_manifest.json face_landmark_68_model-shard1 \
         face_recognition_model-weights_manifest.json face_recognition_model-shard1 \
         face_recognition_model-shard2; do
  curl -o models/$f $BASE/$f
done
```
Tổng dung lượng ~7MB (chủ yếu là `face_recognition_model`), tải 1 lần, dùng lại mãi — không cần internet lúc chạy demo sau đó (mọi thứ đã nằm trong `poc-web/`).

### 8.2. `face-recognition.js` — logic nhận diện, copy nguyên

```javascript
// face-recognition.js — nhận diện khuôn mặt THẬT bằng face-api.js, chạy hoàn toàn
// trong trình duyệt (không gửi ảnh/embedding đi đâu cả). Lưu trữ = localStorage,
// CHỈ DÙNG CHO POC — xem Phần 7 file kiến trúc lớn về cách lưu đúng cho production
// (schema riêng, mã hoá, đồng ý của nhân viên, Nghị định 13/2023/NĐ-CP).

const FACE_MATCH_THRESHOLD = 0.5;   // khoảng cách Euclidean; thấp hơn = khớp hơn. 0.5-0.6 là phổ biến.
const FACE_DB_KEY = 'poc_face_db';

let _faceModelsReady = false;

async function loadFaceModels() {
  if (_faceModelsReady) return;
  await faceapi.nets.tinyFaceDetector.loadFromUri('./models');
  await faceapi.nets.faceLandmark68Net.loadFromUri('./models');
  await faceapi.nets.faceRecognitionNet.loadFromUri('./models');
  _faceModelsReady = true;
}

function _getFaceDb() {
  try { return JSON.parse(localStorage.getItem(FACE_DB_KEY) || '[]'); }
  catch { return []; }
}
function _saveFaceDb(db) { localStorage.setItem(FACE_DB_KEY, JSON.stringify(db)); }

async function _detectDescriptor(videoEl) {
  const detection = await faceapi
    .detectSingleFace(videoEl, new faceapi.TinyFaceDetectorOptions())
    .withFaceLandmarks()
    .withFaceDescriptor();
  return detection ? detection.descriptor : null;   // Float32Array(128) hoặc null nếu không thấy mặt
}

// Đăng ký 1 khuôn mặt — lưu vào localStorage (chỉ máy/trình duyệt này)
async function enrollFace(videoEl, name) {
  const descriptor = await _detectDescriptor(videoEl);
  if (!descriptor) throw new Error('Không thấy khuôn mặt rõ trong khung hình — nhìn thẳng camera, đủ sáng, thử lại.');
  const db = _getFaceDb();
  db.push({ name, descriptor: Array.from(descriptor) });   // Float32Array -> array để JSON.stringify được
  _saveFaceDb(db);
  return db.length;
}

// So khớp 1 khung hình hiện tại với toàn bộ DB đã đăng ký
async function recognizeFace(videoEl) {
  const descriptor = await _detectDescriptor(videoEl);
  if (!descriptor) return { match: false, reason: 'không thấy khuôn mặt' };

  const db = _getFaceDb();
  if (db.length === 0) return { match: false, reason: 'chưa đăng ký ai — bấm "Đăng ký khuôn mặt này" trước' };

  let best = null, bestDist = Infinity;
  for (const entry of db) {
    const dist = faceapi.euclideanDistance(descriptor, entry.descriptor);
    if (dist < bestDist) { bestDist = dist; best = entry; }
  }
  if (bestDist <= FACE_MATCH_THRESHOLD) return { match: true, name: best.name, distance: bestDist };
  return { match: false, reason: 'không khớp ai trong DB', distance: bestDist };
}
```

### 8.3. Lưu ý bắt buộc phải đọc trước khi bấm "Đăng ký khuôn mặt"

- **Chỉ đăng ký khuôn mặt của chính bạn hoặc người đồng ý tham gia demo.** Đây vẫn là dữ liệu sinh trắc học nhạy cảm (xem Phần 7.3 file kiến trúc lớn — Nghị định 13/2023/NĐ-CP) dù chỉ nằm trong `localStorage` của 1 máy — **không** đăng ký khuôn mặt nhân viên thật cho POC khi chưa có sự đồng ý/duyệt từ pháp lý-nhân sự.
- `localStorage` là nơi lưu **tạm, chỉ cho POC** — mất khi xoá dữ liệu trình duyệt, không đồng bộ giữa các máy, không mã hoá. Production phải lưu theo đúng thiết kế ở Phần 7.2 (schema riêng, mã hoá, có bản ghi đồng ý).
- **Chống giả mạo yếu**: camera 2D thường qua `getUserMedia`, ảnh in/màn hình điện thoại có thể đánh lừa model — không dùng làm cách mở cửa **duy nhất** cho khu vực nhạy cảm (xem Phần 7.4).
- Muốn xoá toàn bộ DB khuôn mặt đã đăng ký (test lại từ đầu): mở Console, gõ `localStorage.removeItem('poc_face_db')`.

---

## 9. Chạy thử lần đầu — làm đúng thứ tự này

**Giai đoạn 1 — Mock Mode (làm ngay, không cần ESP32):**
1. Copy 3 file mục 5-7 vào `poc-web/`, giữ `MOCK_MODE = true`.
2. `cd poc-web && python -m http.server 5500`, mở `http://127.0.0.1:5500`.
3. Bấm **Pair** → mở Console (F12) lấy OTP giả lập → nhập vào ô OTP → bấm **Trust**.
4. Bấm thử **Status / Mở cửa / Khoá cửa / Sạc ON/OFF / PD Status** — build/chỉnh UI tới khi hài lòng, tất cả chỉ chạy trong JS, không cần mạng.

**Giai đoạn 2 — Nối ESP32 thật (khi sẵn sàng, đã làm mục 1-2):**
1. Trong `esp32-client.js`, đổi `const MOCK_MODE = true;` → `false`.
2. Sửa giá trị mặc định trong `index.html` (`value="https://192.168.2.50:8080"`) thành IP ESP32 thật (hoặc để trống, gõ tay lúc chạy).
3. Mở lại trang (test trên Chrome/PC trước, đỡ phải đụng iPad).
4. Bấm **Pair** → log hiện "Pair OK", ô OTP xuất hiện, **đồng thời màn LCD của ESP32 phải hiện 6 số**.
5. Đọc 6 số đó, nhập vào ô OTP, bấm **Trust** → log hiện "Trust OK", khu vực điều khiển xuất hiện.
6. Bấm **Status** → phải thấy JSON có `"paired": true`. Nếu không → dừng lại, kiểm tra bước 1-5, chưa qua bước tiếp.
7. Bấm **Mở cửa** → relay thật phải tách nghe được tiếng "tách", cửa mở. Bấm **Khoá cửa** → nghe tiếng ngược lại.
8. Bấm **Sạc ON** → đợi ~2 giây (PD role swap, xem log serial ESP32 để thấy đang chạy). Bấm **PD Status** → xem `sourcing: true`.
9. Nếu mọi bước trên qua trên Chrome/PC, lặp lại đúng như vậy trên Safari/iPad (đã cài trust ở mục 2).

**Bảng lỗi thường gặp:**

| Lỗi thấy | Nguyên nhân | Cách sửa |
|---|---|---|
| Cảnh báo "connection not private" khi mở trang esp32 | Chưa cài trust cert (chỉ xảy ra trên Safari) | Làm lại mục 2 |
| `pair thất bại: {"error":"already paired"}` (403) | Thiết bị đang paired với 1 session khác | Vào `/admin` → Unpair, rồi Pair lại |
| `trust thất bại: {"error":"invalid OTP"}` | Gõ sai 6 số, hoặc quá 300s kể từ lúc Pair | Bấm Pair lại từ đầu, đọc lại LCD, nhập ngay |
| `status thất bại: HTTP 401` | Sai `cryptoKey` (thường do bấm Status trước khi Trust xong) | Kiểm tra đã thấy log "Trust OK" chưa |
| `sạc ON thất bại ... 409` | Có lệnh PD khác đang chạy dở (~1.5-2s) | Đợi 2s, bấm lại |
| Mọi request báo lỗi mạng (network error), không có response | Sai IP/port, hoặc ESP32 không cùng mạng | Ping thử `https://<ip>:8080/version` trực tiếp trên trình duyệt trước |

---

## 10. Camera trên iPad cần HTTPS — lưu ý quan trọng

`getUserMedia` (bật camera) chỉ chạy trong **"secure context"**. Trình duyệt coi 2 trường hợp là an toàn:
- `http://localhost` hoặc `http://127.0.0.1` — **luôn được coi là an toàn**, dù không có HTTPS. Test trên Chrome/PC theo đúng mục 8 (`http://127.0.0.1:5500`) → camera chạy bình thường, không cần làm gì thêm.
- Mọi địa chỉ khác qua `http://` (vd mở từ iPad qua IP LAN của máy dev, `http://192.168.x.x:5500`) → **camera bị chặn thẳng**, không có cách bypass bằng JS.

**Muốn camera chạy trên iPad thật (không phải localhost), cần serve trang POC qua HTTPS:**

```bash
# 1 lần: tạo cert riêng cho trang POC (khác cert của ESP32, tránh nhầm lẫn)
cd poc-web
openssl req -x509 -newkey rsa:2048 -keyout poc_key.pem -out poc_cert.pem \
    -days 3650 -nodes -subj "/CN=poc-web"
```
```python
# serve_https.py — đặt cùng thư mục poc-web/, chạy: python serve_https.py
import http.server, ssl
server = http.server.HTTPServer(('0.0.0.0', 5500), http.server.SimpleHTTPRequestHandler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('poc_cert.pem', 'poc_key.pem')
server.socket = ctx.wrap_socket(server.socket, server_side=True)
print("Serving HTTPS on 0.0.0.0:5500")
server.serve_forever()
```
Sau đó cài trust cho `poc_cert.pem` trên iPad — **làm đúng 6 bước ở mục 2** (chỉ đổi tên file, quy trình y hệt). Mở `https://<ip-máy-dev>:5500` trên Safari → camera hoạt động.

*(Nếu chỉ cần build/test logic UI, không nhất thiết phải làm bước này ngay — camera chạy tốt trên Chrome/PC qua `127.0.0.1` như mục 9 đã đủ để thấy toàn bộ luồng.)*

---

## 11. Khi POC đã chạy được — việc tiếp theo

Đây là **điểm dừng của POC**, không phải điểm bắt đầu triển khai thật. Trước khi đưa cho nhiều phòng dùng thật, phải quay lại đọc `eteams-ste-api-va-ke-hoach-web.md`, tối thiểu các phần:
- **Phần 6** — vì sao không thể giữ nguyên "gọi thẳng ESP32 + dev mode" cho production (mất xác thực RSA thật), và các lựa chọn thay thế (Gateway/Hybrid).
- **Phần 4.7 / 5.3** — vì sao cần audit log, RBAC, cách quản lý nhiều phòng/nhiều kiosk.
- **Phần 3** — để hiểu rõ giao thức đầy đủ khi cần bật lại RSA thật (không còn dev mode).
- **Phần 7** — nhận diện khuôn mặt trong POC này **đã là model thật** (face-api.js, chạy trong trình duyệt, mục 8), nhưng lưu trữ (`localStorage`, không mã hoá, không đồng bộ, không có bản ghi đồng ý) và chống giả mạo (camera 2D thường, không liveness) **chưa đạt chuẩn production**. Trước khi đăng ký khuôn mặt nhân viên thật/dùng cho phòng thật: đọc mục 7.2 (schema `face_embeddings`/`face_consent` riêng, khuyến nghị so khớp phía backend thay vì phân phối cả DB xuống từng kiosk), mục 7.3 (Nghị định 13/2023/NĐ-CP — bắt buộc có đồng ý + có thể cần đánh giá tác động xử lý dữ liệu trước khi triển khai thật), và mục 7.4 (cần phương án dự phòng, không để khuôn mặt là cách mở cửa duy nhất).
