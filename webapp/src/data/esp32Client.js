// esp32Client.js — client gọi HTTPS API của firmware C (xem
// firmware/components/api/api.c và api.h). Không mã hoá body — chỉ TLS +
// `Authorization: Bearer <token>` (bản MicroPython cũ dùng AES-GCM, bản này
// bỏ hẳn, xem comment trong pairing.h "spec v0.1 requirement 10").
//
// Base URL + token lưu trong localStorage của CHÍNH trình duyệt kiosk này,
// KHÔNG lưu lên Firestore: mỗi kiosk pair với đúng 1 thiết bị, token là bí
// mật duy nhất chứng minh quyền điều khiển cửa — xem nó trên Firestore (nơi
// nhiều người có thể đọc được) sẽ vô hiệu hoá toàn bộ ý nghĩa của bearer
// token. Xoá dữ liệu site trên iPad = mất token, phải unpair (bằng token cũ
// nếu còn, hoặc bằng /admin/unpair firmware qua serial) rồi pair lại.

import { ESP32_REQUEST_TIMEOUT_MS } from '../config/timing.js';

const LS_BASE_URL = 'esp32_base_url';
const LS_TOKEN = 'esp32_token';

export function getBaseUrl() {
  try {
    return localStorage.getItem(LS_BASE_URL) || import.meta.env.VITE_ESP32_BASE_URL || '';
  } catch {
    return import.meta.env.VITE_ESP32_BASE_URL || '';
  }
}

export function setBaseUrl(url) {
  try { localStorage.setItem(LS_BASE_URL, url.trim().replace(/\/$/, '')); } catch { /* ignore */ }
}

function getToken() {
  try { return localStorage.getItem(LS_TOKEN) || ''; } catch { return ''; }
}

function setToken(token) {
  try { localStorage.setItem(LS_TOKEN, token); } catch { /* ignore */ }
}

function clearToken() {
  try { localStorage.removeItem(LS_TOKEN); } catch { /* ignore */ }
}

// True nếu trình duyệt NÀY đang giữ token — không hỏi thiết bị. Dùng để
// quyết định có nên gọi các API cần Bearer hay không mà chưa cần round-trip.
export function isPairedLocally() {
  return !!getToken();
}

async function call(path, { method = 'GET', body, auth = false, timeoutMs = ESP32_REQUEST_TIMEOUT_MS } = {}) {
  const base = getBaseUrl();
  if (!base) return { ok: false, error: 'device_url_not_set' };

  const headers = { 'Content-Type': 'application/json' };
  if (auth) {
    const token = getToken();
    if (!token) return { ok: false, error: 'not_paired' };
    headers.Authorization = `Bearer ${token}`;
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(base + path, {
      method,
      headers,
      body: body !== undefined ? JSON.stringify(body) : undefined,
      signal: controller.signal,
    });
    const json = await res.json().catch(() => null);
    // Envelope của firmware đã đúng dạng {ok:true,data} | {ok:false,error} —
    // không cần bọc lại, chỉ cần trả thẳng.
    if (!json || typeof json.ok !== 'boolean') {
      return { ok: false, error: 'bad_response' };
    }
    return json;
  } catch (err) {
    return { ok: false, error: err.name === 'AbortError' ? 'timeout' : 'network_error' };
  } finally {
    clearTimeout(timer);
  }
}

export const getInfo = () => call('/api/info');
export const getStatus = () => call('/api/status', { auth: true });
export const getLogs = (limit) => call(`/api/logs${limit ? `?limit=${limit}` : ''}`, { auth: true });

// Bước 1 pairing: không cần quyền gì (thiết bị phải đang ở trạng thái chưa
// pair). OTP hiện trên OLED của thiết bị, KHÔNG có trong response này.
export const pairStart = () => call('/api/pair/start', { method: 'POST' });

// Bước 2 pairing: đọc OTP từ màn thiết bị rồi gõ vào đây. Thành công thì
// token được lưu lại — đây là LẦN DUY NHẤT token này hiện dưới dạng rõ.
export async function pairConfirm(otp) {
  const res = await call('/api/pair/confirm', { method: 'POST', body: { otp } });
  if (res.ok && res.data?.token) setToken(res.data.token);
  return res;
}

export async function unpairDevice() {
  const res = await call('/api/unpair', { method: 'POST', auth: true });
  if (res.ok) clearToken();
  return res;
}

// open=true -> mở (relay=1), open=false -> khoá (relay=0). Không có
// duration_s — firmware không tự khoá lại, web phải tự gọi khi cần khoá.
export const setMaglock = (open) =>
  call('/api/relay/maglock', { method: 'POST', auth: true, body: { value: open ? 1 : 0 } });

// Firmware chỉ thực thi, không tính phần trăm gì cả — ngưỡng 30/80% là logic
// phía webapp, xem hooks/useChargingAutomation.js.
export const setCharging = (on) =>
  call('/api/charge', { method: 'POST', auth: true, body: { on: !!on } });
