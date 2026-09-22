// doorControl.js — STUB firmware cho Phase 1. Chỉ console.log, CHƯA gọi ESP32
// thật. Khi integrate Phase 3: thay nội dung 2 hàm này bằng gọi API thật
// (xem research/eteams-ste-api-va-ke-hoach-web.md), giữ nguyên tên hàm +
// chữ ký để không phải sửa lại nơi gọi.

export function unlockDoor(meta = {}) {
  console.log('[FIRMWARE STUB] unlockDoor', { time: new Date().toISOString(), ...meta });
}

export function lockDoor(meta = {}) {
  console.log('[FIRMWARE STUB] lockDoor', { time: new Date().toISOString(), ...meta });
}
