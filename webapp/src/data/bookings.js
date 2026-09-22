// bookings.js — lớp dữ liệu + state machine cho vòng đời 1 booking.
// Firestore collection "bookings": mỗi document là 1 cuộc họp (scheduled hoặc quick).
// Dùng epoch ms (number) cho mọi mốc thời gian — không dùng Firestore Timestamp,
// vì Phase 1 không có Cloud Functions/server time, chỉ cần tính toán đơn giản
// ngay trên client.

import { collection, addDoc, doc, updateDoc, query, where, getDocs } from 'firebase/firestore';
import { db } from './firebase.js';
import { room } from './mockData.js';
import { lockDoor, unlockDoor } from './doorControl.js';

const ROOM_ID = room.id;

export const NO_SHOW_GRACE_MIN = 15;
export const AUTO_EXTEND_MIN = 15;
export const SCHEDULED_LOCK_DELAY_MIN = 5;
export const QUICK_LOCK_DELAY_MIN = 10;

function overlaps(aStart, aEnd, bStart, bEnd) {
  return aStart < bEnd && aEnd > bStart;
}

function dateKeyFromMs(ms) {
  const d = new Date(ms);
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}

function formatTime(ms) {
  return new Date(ms).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

function formatRange(b) {
  return `${formatTime(b.startAt)} – ${formatTime(b.endAt)}`;
}

export function isPersonAllowed(booking, personId) {
  return booking.organizerId === personId || booking.attendeeIds.includes(personId);
}

// ── Đọc dữ liệu ────────────────────────────────────────────────────────────

// Mọi booking còn "sống" (chặn lịch) của phòng — chỉ 1 phòng, tập dữ liệu nhỏ
// nên lấy hết rồi lọc/so overlap ngay trên client, không cần query phức tạp.
export async function getActiveBookings() {
  const q = query(
    collection(db, 'bookings'),
    where('roomId', '==', ROOM_ID),
    where('status', 'in', ['scheduled', 'checked_in'])
  );
  const snap = await getDocs(q);
  return snap.docs.map((d) => ({ id: d.id, ...d.data() }));
}

// Toàn bộ booking (mọi trạng thái) của 1 ngày — dùng cho agenda hiển thị.
export async function getBookingsForDate(dateKey) {
  const q = query(collection(db, 'bookings'), where('roomId', '==', ROOM_ID), where('date', '==', dateKey));
  const snap = await getDocs(q);
  return snap.docs.map((d) => ({ id: d.id, ...d.data() })).sort((a, b) => a.startAt - b.startAt);
}

export async function getCurrentMeeting() {
  const active = await getActiveBookings();
  return active.find((b) => b.status === 'checked_in') || null;
}

async function findConflict(startAt, endAt, excludeId = null) {
  const active = await getActiveBookings();
  return active.find((b) => b.id !== excludeId && overlaps(b.startAt, b.endAt, startAt, endAt)) || null;
}

// ── Tạo booking ────────────────────────────────────────────────────────────

export async function createScheduledBooking({ startAt, endAt, title, organizerId, organizerName, attendeeIds = [], attendeeNames = [] }) {
  if (startAt < Date.now()) {
    return { ok: false, error: 'Start time cannot be in the past.' };
  }
  if (endAt <= startAt) {
    return { ok: false, error: 'End time must be after start time.' };
  }
  const conflict = await findConflict(startAt, endAt);
  if (conflict) {
    return { ok: false, error: `Time conflict with "${conflict.title}" (${formatRange(conflict)}).` };
  }
  const now = Date.now();
  const ref = await addDoc(collection(db, 'bookings'), {
    roomId: ROOM_ID,
    date: dateKeyFromMs(startAt),
    startAt, endAt, title,
    type: 'scheduled',
    organizerId, organizerName,
    attendeeIds, attendeeNames,
    status: 'scheduled',
    checkedInById: null, checkedInByName: null, checkInAt: null, checkOutAt: null,
    lastUnlockAt: null, doorLockedAt: null, autoExtended: false,
    createdAt: now,
  });
  return { ok: true, id: ref.id };
}

// Quick Book tự check-in ngay lúc tạo — không cần bước quét khuôn mặt riêng,
// vì người bấm nút chính là người vào phòng ngay lúc đó.
export async function createQuickBooking({ minutes, title, organizerId, organizerName }) {
  const startAt = Date.now();
  const endAt = startAt + minutes * 60000;
  const conflict = await findConflict(startAt, endAt);
  if (conflict) {
    return { ok: false, error: `Room is booked during this time: "${conflict.title}" (${formatRange(conflict)}).` };
  }
  const ref = await addDoc(collection(db, 'bookings'), {
    roomId: ROOM_ID,
    date: dateKeyFromMs(startAt),
    startAt, endAt, title,
    type: 'quick',
    organizerId, organizerName,
    attendeeIds: [], attendeeNames: [],
    status: 'checked_in',
    checkedInById: organizerId, checkedInByName: organizerName, checkInAt: startAt, checkOutAt: null,
    lastUnlockAt: startAt, doorLockedAt: null, autoExtended: false,
    createdAt: startAt,
  });
  unlockDoor({ bookingId: ref.id, type: 'quick', title });
  return { ok: true, id: ref.id };
}

// ── Check-in / Check-out / Extend ───────────────────────────────────────────

// 2 trường hợp:
// 1) Booking đang 'scheduled', đúng cửa sổ check-in đầu tiên (startAt →
//    startAt+NO_SHOW_GRACE_MIN) → mở cửa LẦN ĐẦU, chuyển sang 'checked_in'.
// 2) Cuộc họp đang 'checked_in' (đã có người vào trước) — 1 attendee/organizer
//    khác đến muộn vẫn bấm Check In để tự mở cửa cho mình, không đổi trạng
//    thái/organizer gốc của cuộc họp, chỉ mở lại cửa + reset mốc tự khoá.
export async function checkIn({ personId, personName, nowMs = Date.now() }) {
  const active = await getActiveBookings();

  const firstEntry = active.find(
    (b) =>
      b.status === 'scheduled' &&
      isPersonAllowed(b, personId) &&
      nowMs >= b.startAt &&
      nowMs < Math.min(b.startAt + NO_SHOW_GRACE_MIN * 60000, b.endAt)
  );
  if (firstEntry) {
    await updateDoc(doc(db, 'bookings', firstEntry.id), {
      status: 'checked_in', checkedInById: personId, checkedInByName: personName,
      checkInAt: nowMs, lastUnlockAt: nowMs, doorLockedAt: null,
    });
    unlockDoor({ bookingId: firstEntry.id, type: 'scheduled', title: firstEntry.title, person: personName });
    return { ok: true, booking: { ...firstEntry, status: 'checked_in', checkedInById: personId, checkInAt: nowMs } };
  }

  const reentry = active.find(
    (b) => b.status === 'checked_in' && isPersonAllowed(b, personId) && nowMs < b.endAt
  );
  if (reentry) {
    await updateDoc(doc(db, 'bookings', reentry.id), { lastUnlockAt: nowMs, doorLockedAt: null });
    unlockDoor({ bookingId: reentry.id, type: 'reentry', title: reentry.title, person: personName });
    return { ok: true, booking: reentry, reentry: true };
  }

  return { ok: false, error: 'No valid booking to check into right now for this person.' };
}

// Chỉ organizer mới được kết thúc cuộc họp đang diễn ra.
export async function checkOut({ personId, nowMs = Date.now() }) {
  const current = await getCurrentMeeting();
  if (!current) return { ok: false, error: 'No meeting in progress.' };
  if (current.organizerId !== personId) return { ok: false, error: 'Only the organizer can end this meeting.' };
  await updateDoc(doc(db, 'bookings', current.id), { status: 'completed', checkOutAt: nowMs, endAt: nowMs });
  return { ok: true };
}

// Chỉ organizer mới được gia hạn, và chỉ được tới trước booking kế tiếp (nếu có).
export async function extend({ personId, addMinutes }) {
  const active = await getActiveBookings();
  const current = active.find((b) => b.status === 'checked_in');
  if (!current) return { ok: false, error: 'No meeting in progress.' };
  if (current.organizerId !== personId) return { ok: false, error: 'Only the organizer can extend this meeting.' };

  const wanted = current.endAt + addMinutes * 60000;
  const next = active
    .filter((b) => b.id !== current.id && b.startAt >= current.endAt)
    .sort((a, b) => a.startAt - b.startAt)[0];
  const cappedEnd = next ? Math.min(wanted, next.startAt) : wanted;

  if (cappedEnd <= current.endAt) {
    return { ok: false, error: 'No room to extend — another booking starts right after.' };
  }
  await updateDoc(doc(db, 'bookings', current.id), { endAt: cappedEnd });
  return { ok: true, endAt: cappedEnd, cappedShort: cappedEnd < wanted };
}

// ── Tự động hoá (no-show / gia hạn / tự kết thúc / tự khoá cửa) ────────────

// Hàm THUẦN (không đụng Firestore) — nhận sẵn danh sách booking active +
// nowMs, trả ra danh sách hành động cần áp. Tách riêng để unit test được với
// thời gian giả lập, không phải đợi đồng hồ thật hay chỉnh tay qua Firestore.
export function computeAutomationActions(activeBookings, nowMs) {
  const actions = [];

  for (const b of activeBookings) {
    if (b.status === 'scheduled') {
      const deadline = Math.min(b.startAt + NO_SHOW_GRACE_MIN * 60000, b.endAt);
      if (nowMs >= deadline) {
        actions.push({ id: b.id, patch: { status: 'no_show' } });
      }
      continue;
    }

    if (b.status === 'checked_in') {
      if (nowMs >= b.endAt) {
        const nextBooking = activeBookings
          .filter((o) => o.id !== b.id && o.startAt >= b.endAt)
          .sort((a, c) => a.startAt - c.startAt)[0];
        const wouldExtendTo = b.endAt + AUTO_EXTEND_MIN * 60000;

        if (nextBooking && nextBooking.startAt < wouldExtendTo) {
          // Lịch kế tiếp đã có người đặt, không đủ chỗ gia hạn → nhường ngay.
          actions.push({ id: b.id, patch: { status: 'completed', checkOutAt: nowMs } });
        } else if (!b.autoExtended) {
          // Lịch kế tiếp còn trống & chưa dùng suất gia hạn tự động → +15' một lần.
          actions.push({ id: b.id, patch: { endAt: wouldExtendTo, autoExtended: true } });
        } else {
          // Đã dùng suất gia hạn mà vẫn không ai xử lý → buộc kết thúc.
          actions.push({ id: b.id, patch: { status: 'completed', checkOutAt: nowMs } });
        }
        continue;
      }

      if (!b.doorLockedAt && b.lastUnlockAt) {
        const lockDelayMs = (b.type === 'quick' ? QUICK_LOCK_DELAY_MIN : SCHEDULED_LOCK_DELAY_MIN) * 60000;
        if (nowMs >= b.lastUnlockAt + lockDelayMs) {
          actions.push({ id: b.id, patch: { doorLockedAt: nowMs }, lockDoor: true });
        }
      }
    }
  }

  return actions;
}

// Wrapper I/O thật — gọi từ ticker trong app.
export async function applyAutomationTick(nowMs = Date.now()) {
  const active = await getActiveBookings();
  const actions = computeAutomationActions(active, nowMs);
  for (const action of actions) {
    if (action.lockDoor) lockDoor({ bookingId: action.id });
    await updateDoc(doc(db, 'bookings', action.id), action.patch);
  }
  return actions;
}
