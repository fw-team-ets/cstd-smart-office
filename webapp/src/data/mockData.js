// mockData.js — CHỈ còn cấu hình tĩnh của kiosk (không đổi lúc chạy).
// Danh bạ (people) nằm trên Firestore (collection "people"). Lịch/booking nằm
// trên Firestore collection "bookings" — xem src/data/bookings.js.

export const room = {
  id: 'room-a',
  name: 'Meeting Room A',
  level: 5,
  capacity: 8,
};

export const durations = [
  { label: '15 minutes', minutes: 15 },
  { label: '30 minutes', minutes: 30 },
  { label: '1 hour', minutes: 60 },
  { label: '2 hours', minutes: 120 },
];

export const roomStatusLabel = {
  available: 'Available',
  in_meeting: 'In Meeting',
};

// Cấu hình khung giờ được đặt (Book Room) — GIAI ĐOẠN TEST: mở cả ngày.
// Sau này siết lại giờ hành chính chỉ cần đổi 2 hằng số phút bên dưới.
export const BOOKING_ADVANCE_DAYS = 7;   // đặt trước tối đa N ngày
export const TIME_STEP_MINUTES = 15;     // bước chọn giờ
export const DAY_START_MINUTES = 0;      // 00:00 — sau này đổi thành 8*60 (8:00 AM)
export const DAY_END_MINUTES = 24 * 60;  // 23:59 (mốc cuối, exclusive) — sau này đổi thành 18*60 (6:00 PM)
