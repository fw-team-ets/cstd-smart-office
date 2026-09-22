import { useEffect } from 'react';
import { applyAutomationTick } from '../data/bookings.js';

const TICK_MS = 20000;

// Chạy ở gốc app (App.jsx), độc lập với màn hình đang hiển thị — xử lý
// no-show, gia hạn/tự kết thúc, tự khoá cửa theo mốc thời gian thật.
export function useBookingAutomation() {
  useEffect(() => {
    let cancelled = false;

    async function tick() {
      try {
        await applyAutomationTick();
      } catch (err) {
        console.error('[automation] tick failed:', err);
      }
    }

    tick();
    const id = setInterval(() => {
      if (!cancelled) tick();
    }, TICK_MS);

    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, []);
}
