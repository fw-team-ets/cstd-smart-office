import { describe, it, expect } from 'vitest';
import {
  computeAutomationActions,
  NO_SHOW_GRACE_MIN,
  AUTO_EXTEND_MIN,
  SCHEDULED_LOCK_DELAY_MIN,
  QUICK_LOCK_DELAY_MIN,
} from './bookings.js';

const MIN = 60000;
const T0 = new Date(2026, 0, 1, 9, 0, 0).getTime(); // 09:00, mốc gốc cho mọi test

function booking(overrides) {
  return {
    id: 'b1',
    status: 'scheduled',
    type: 'scheduled',
    startAt: T0,
    endAt: T0 + 60 * MIN,
    autoExtended: false,
    doorLockedAt: null,
    lastUnlockAt: null,
    checkInAt: null,
    ...overrides,
  };
}

describe('computeAutomationActions — no-show', () => {
  it('không làm gì khi chưa hết hạn no-show', () => {
    const b = booking({ status: 'scheduled' });
    const actions = computeAutomationActions([b], T0 + (NO_SHOW_GRACE_MIN - 1) * MIN);
    expect(actions).toEqual([]);
  });

  it('huỷ no-show đúng lúc chạm mốc startAt + grace', () => {
    const b = booking({ status: 'scheduled' });
    const actions = computeAutomationActions([b], T0 + NO_SHOW_GRACE_MIN * MIN);
    expect(actions).toEqual([{ id: 'b1', patch: { status: 'no_show' } }]);
  });

  it('không no-show nếu đã checked_in', () => {
    const b = booking({ status: 'checked_in', lastUnlockAt: T0, endAt: T0 + 60 * MIN });
    const actions = computeAutomationActions([b], T0 + NO_SHOW_GRACE_MIN * MIN);
    expect(actions.find((a) => a.patch?.status === 'no_show')).toBeUndefined();
  });
});

describe('computeAutomationActions — gia hạn / tự kết thúc', () => {
  it('gia hạn +15 một lần khi hết giờ mà lịch sau còn trống', () => {
    const end = T0 + 60 * MIN;
    const b = booking({ status: 'checked_in', endAt: end, lastUnlockAt: T0, autoExtended: false });
    const actions = computeAutomationActions([b], end);
    expect(actions).toEqual([
      { id: 'b1', patch: { endAt: end + AUTO_EXTEND_MIN * MIN, autoExtended: true } },
    ]);
  });

  it('tự kết thúc ngay nếu lịch sau đã có người đặt (không đủ chỗ gia hạn)', () => {
    const end = T0 + 60 * MIN;
    const current = booking({ id: 'cur', status: 'checked_in', endAt: end, lastUnlockAt: T0, autoExtended: false });
    const next = booking({ id: 'next', status: 'scheduled', startAt: end + 5 * MIN, endAt: end + 65 * MIN });
    const actions = computeAutomationActions([current, next], end);
    const forCurrent = actions.find((a) => a.id === 'cur');
    expect(forCurrent.patch.status).toBe('completed');
  });

  it('tự kết thúc khi đã dùng suất gia hạn mà vẫn không ai xử lý', () => {
    const end = T0 + 60 * MIN;
    const b = booking({ status: 'checked_in', endAt: end, lastUnlockAt: T0, autoExtended: true });
    const actions = computeAutomationActions([b], end);
    expect(actions).toEqual([{ id: 'b1', patch: { status: 'completed', checkOutAt: end } }]);
  });

  it('không đụng gì khi cuộc họp còn đang trong giờ', () => {
    const end = T0 + 60 * MIN;
    const nowMs = T0 + 30 * MIN;
    // lastUnlockAt vừa mới đó (còn trong 5' chưa tới hạn khoá cửa) để tách
    // riêng nhánh "hết giờ chưa" khỏi nhánh tự khoá cửa (test riêng ở dưới).
    const b = booking({ status: 'checked_in', endAt: end, lastUnlockAt: nowMs, autoExtended: false });
    const actions = computeAutomationActions([b], nowMs);
    expect(actions).toEqual([]);
  });
});

describe('computeAutomationActions — tự khoá cửa', () => {
  it('khoá cửa cuộc họp scheduled sau đúng 5 phút kể từ lần mở gần nhất', () => {
    const b = booking({ status: 'checked_in', type: 'scheduled', lastUnlockAt: T0, doorLockedAt: null, endAt: T0 + 60 * MIN });
    const before = computeAutomationActions([b], T0 + (SCHEDULED_LOCK_DELAY_MIN - 1) * MIN);
    expect(before.find((a) => a.lockDoor)).toBeUndefined();

    const after = computeAutomationActions([b], T0 + SCHEDULED_LOCK_DELAY_MIN * MIN);
    expect(after).toEqual([{ id: 'b1', patch: { doorLockedAt: T0 + SCHEDULED_LOCK_DELAY_MIN * MIN }, lockDoor: true }]);
  });

  it('khoá cửa Quick Book sau đúng 10 phút, không phải 5 phút', () => {
    const b = booking({ status: 'checked_in', type: 'quick', lastUnlockAt: T0, doorLockedAt: null, endAt: T0 + 60 * MIN });
    const at5 = computeAutomationActions([b], T0 + SCHEDULED_LOCK_DELAY_MIN * MIN);
    expect(at5.find((a) => a.lockDoor)).toBeUndefined();

    const at10 = computeAutomationActions([b], T0 + QUICK_LOCK_DELAY_MIN * MIN);
    expect(at10.find((a) => a.lockDoor)).toBeDefined();
  });

  it('không khoá lại nếu doorLockedAt đã có giá trị', () => {
    const b = booking({ status: 'checked_in', lastUnlockAt: T0, doorLockedAt: T0 + 5 * MIN, endAt: T0 + 60 * MIN });
    const actions = computeAutomationActions([b], T0 + 20 * MIN);
    expect(actions.find((a) => a.lockDoor)).toBeUndefined();
  });

  it('reset mốc khoá sau khi có người check-in muộn (lastUnlockAt mới hơn)', () => {
    const reentryUnlock = T0 + 40 * MIN;
    const b = booking({ status: 'checked_in', lastUnlockAt: reentryUnlock, doorLockedAt: null, endAt: T0 + 60 * MIN });
    const actions = computeAutomationActions([b], reentryUnlock + SCHEDULED_LOCK_DELAY_MIN * MIN);
    expect(actions).toEqual([{ id: 'b1', patch: { doorLockedAt: reentryUnlock + SCHEDULED_LOCK_DELAY_MIN * MIN }, lockDoor: true }]);
  });
});
