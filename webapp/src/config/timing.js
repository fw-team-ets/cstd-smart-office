// timing.js — mọi hằng số thời gian/ngưỡng dùng để tinh chỉnh logic nghiệp vụ
// và các vòng lặp tự động trong app. Gom về 1 file để lúc test/dev có thể rút
// ngắn thời gian chờ (đỡ phải đợi 15 phút thật để thấy no-show chạy), rồi khi
// ra production chỉ cần sửa lại đúng các số trong file này — không phải lục
// từng file rải rác khắp project.

// ── Vòng đời booking (data/bookings.js) ─────────────────────────────────────

// Sau startAt bao nhiêu phút mà không ai check-in thì tự huỷ (no_show).
export const NO_SHOW_GRACE_MIN = 3;

// Hết giờ (endAt) mà chưa ai checkout, lịch sau còn trống & chưa dùng suất
// gia hạn: tự cộng thêm bao nhiêu phút (chỉ 1 lần cho mỗi booking).
export const AUTO_EXTEND_MIN = 15;

// Check-in qua Book Room (đặt trước): sau lần mở cửa cuối bao nhiêu phút thì
// tự khoá lại nếu không có check-in/reentry mới.
export const SCHEDULED_LOCK_DELAY_MIN = 1;

// Check-in qua Quick Book: tương tự SCHEDULED_LOCK_DELAY_MIN nhưng cho slot
// họp nhanh (thường ngắn hơn nên để thời gian trước khi khoá dài hơn chút).
export const QUICK_LOCK_DELAY_MIN = 1;

// ── Ticker tự động (hooks/useBookingAutomation.js, useChargingAutomation.js) ─

// Chu kỳ chạy lại automation cho booking (no-show / gia hạn / tự khoá cửa).
export const BOOKING_TICK_MS = 20000;

// Chu kỳ chạy lại automation cho sạc pin.
export const CHARGING_TICK_MS = 30000;

// ── Ngưỡng sạc pin (hooks/useChargingAutomation.js, chỉ áp dụng khi mode = "auto") ─

// level < ngưỡng này -> tự bật sạc.
export const CHARGE_LOW_PCT = 30;

// level > ngưỡng này -> tự tắt sạc.
export const CHARGE_HIGH_PCT = 80;

// ── Mạng / thiết bị (data/esp32Client.js) ───────────────────────────────────

// Timeout cho MỌI request gọi tới ESP32 (info/status/pair/relay/charge/...).
// Mạng LAN chậm hay ESP32 phản hồi trễ lúc test thì tăng số này lên.
export const ESP32_REQUEST_TIMEOUT_MS = 8000;

// ── UI kiosk (pages/RoomHome.jsx) ───────────────────────────────────────────

// Chu kỳ tự refresh trạng thái phòng + agenda trên màn hình chính.
export const ROOM_REFRESH_MS = 15000;

// Banner flash (thành công/lỗi sau 1 action) tự tắt sau bao lâu.
export const FLASH_DURATION_MS = 4000;
