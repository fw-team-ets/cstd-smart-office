// chargingSettings.js — chế độ sạc do admin chọn: 'auto' (theo % pin, xem
// hooks/useChargingAutomation.js) hoặc 'manual' (luôn sạc, bỏ qua % pin).
//
// Lưu ở settings/charging — CỐ Ý KHÔNG dùng chung document deviceStatus/ipad:
// Shortcuts trên iPad ghi vào đó bằng PATCH không kèm updateMask, tức là mỗi
// lần báo pin sẽ THAY THẾ TOÀN BỘ document (xoá mọi field khác) — một field
// mode nằm chung sẽ bị Shortcuts xoá mất sau lần báo pin kế tiếp.

import { doc, setDoc, onSnapshot } from 'firebase/firestore';
import { db } from './firebase.js';

const REF = doc(db, 'settings', 'charging');

export function watchChargeMode(callback) {
  return onSnapshot(
    REF,
    (snap) => callback(snap.data()?.mode === 'manual' ? 'manual' : 'auto'),
    (err) => console.error('[chargingSettings] watch failed:', err)
  );
}

export async function setChargeMode(mode) {
  await setDoc(REF, { mode: mode === 'manual' ? 'manual' : 'auto' }, { merge: true });
}
