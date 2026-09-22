// useChargingAutomation.js — theo dõi pin iPad (Firestore deviceStatus/ipad,
// field "level", ghi bởi Shortcuts automation trên iPad) và tự bật/tắt sạc
// qua ESP32. Chỉ áp dụng khi chế độ là "auto" (settings/charging, đổi được ở
// trang Admin) — ngưỡng: level < 30 -> bật sạc, level > 80 -> tắt sạc
// (khoảng giữa cố tình để trống, hysteresis, tránh nhấp nháy on/off ở biên).
// Chế độ "manual" bỏ qua % pin, luôn giữ sạc bật.
//
// Đọc trạng thái "đang sạc" THẬT từ /api/status mỗi tick, không tự nhớ ở
// client — để không lệch pha nếu trang bị tải lại giữa lúc đang tự động hoá,
// hoặc nếu ai đó vừa bấm nút sạc tay ở trang Admin.

import { useEffect } from 'react';
import { doc, onSnapshot } from 'firebase/firestore';
import { db } from '../data/firebase.js';
import { getStatus, setCharging, isPairedLocally } from '../data/esp32Client.js';
import { watchChargeMode } from '../data/chargingSettings.js';
import {
  CHARGING_TICK_MS as TICK_MS,
  CHARGE_LOW_PCT as LOW_PCT,
  CHARGE_HIGH_PCT as HIGH_PCT,
} from '../config/timing.js';

export function useChargingAutomation() {
  useEffect(() => {
    let cancelled = false;
    let lastLevel = null;
    let mode = 'auto';

    const unsubLevel = onSnapshot(
      doc(db, 'deviceStatus', 'ipad'),
      (snap) => {
        const level = snap.data()?.level;
        if (typeof level === 'number') lastLevel = level;
      },
      (err) => console.error('[charging] deviceStatus listen failed:', err)
    );
    const unsubMode = watchChargeMode((m) => { mode = m; });

    async function tick() {
      if (cancelled || !isPairedLocally()) return;
      try {
        const status = await getStatus();
        if (!status.ok) return;
        const charging = !!status.data.charging;

        if (mode === 'manual') {
          if (!charging) await setCharging(true);
          return;
        }
        if (lastLevel == null) return;
        if (lastLevel < LOW_PCT && !charging) {
          await setCharging(true);
        } else if (lastLevel > HIGH_PCT && charging) {
          await setCharging(false);
        }
      } catch (err) {
        console.error('[charging] tick failed:', err);
      }
    }

    const id = setInterval(tick, TICK_MS);
    return () => {
      cancelled = true;
      clearInterval(id);
      unsubLevel();
      unsubMode();
    };
  }, []);
}
