import { lazy, Suspense } from 'react';
import { Routes, Route } from 'react-router-dom';
import { useBookingAutomation } from './hooks/useBookingAutomation.js';

// Tách nhỏ theo từng trang (code-splitting) — quan trọng nhất là CheckIn/
// Checkout/Admin, vì 3 trang này mới import face-api.js (kéo theo
// TensorFlow.js, nặng nhất trong cả app). Không lazy-load thì Home/Book Room/
// Quick Book cũng phải tải hết chỗ đó dù không dùng tới, load rất chậm qua
// mạng thật (chỉ nhanh lúc test trên localhost).
const RoomHome = lazy(() => import('./pages/RoomHome.jsx'));
const BookRoom = lazy(() => import('./pages/BookRoom.jsx'));
const QuickBook = lazy(() => import('./pages/QuickBook.jsx'));
const CheckIn = lazy(() => import('./pages/CheckIn.jsx'));
const Checkout = lazy(() => import('./pages/Checkout.jsx'));
const RoomSchedule = lazy(() => import('./pages/RoomSchedule.jsx'));
const Admin = lazy(() => import('./pages/Admin.jsx'));

function BrandMark() {
  return (
    <div className="brand-mark">
      <span className="brand-mark-name">eteams</span>
      <span className="brand-mark-sep">·</span>
      <span className="brand-mark-sub">THE INNOVATION GROUP</span>
    </div>
  );
}

export default function App() {
  useBookingAutomation();

  return (
    <>
      <Suspense fallback={<div className="kiosk"><div className="kiosk-center"><p className="card-sub">Loading…</p></div></div>}>
        <Routes>
          <Route path="/" element={<RoomHome />} />
          <Route path="/book" element={<BookRoom />} />
          <Route path="/quick-book" element={<QuickBook />} />
          <Route path="/check-in" element={<CheckIn />} />
          <Route path="/checkout" element={<Checkout />} />
          <Route path="/schedule" element={<RoomSchedule />} />
          <Route path="/admin" element={<Admin />} />
        </Routes>
      </Suspense>
      <BrandMark />
    </>
  );
}
