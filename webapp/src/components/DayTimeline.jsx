// DayTimeline — thanh dòng thời gian ngang cho khung giờ "văn phòng" điển hình
// (7:00–20:00), hiển thị các cuộc họp hôm nay + mốc "hiện tại". Thuần hiển thị,
// không phải khung giờ đặt được (đặt phòng vẫn theo DAY_START/END_MINUTES).
const DISPLAY_START = 7 * 60;
const DISPLAY_END = 20 * 60;
const HOURS = Array.from({ length: (DISPLAY_END - DISPLAY_START) / 60 + 1 }, (_, i) => DISPLAY_START / 60 + i);

function pct(min) {
  const clamped = Math.min(Math.max(min, DISPLAY_START), DISPLAY_END);
  return ((clamped - DISPLAY_START) / (DISPLAY_END - DISPLAY_START)) * 100;
}

function minutesOfDay(ms) {
  const d = new Date(ms);
  return d.getHours() * 60 + d.getMinutes();
}

export default function DayTimeline({ bookings, now }) {
  const nowMin = now.getHours() * 60 + now.getMinutes();
  const segments = bookings
    .filter((b) => b.status === 'checked_in' || b.status === 'scheduled' || b.status === 'completed')
    .map((b) => ({ ...b, startMin: minutesOfDay(b.startAt), endMin: minutesOfDay(b.endAt) }))
    .filter((b) => b.endMin > DISPLAY_START && b.startMin < DISPLAY_END);

  return (
    <div className="day-timeline">
      <div className="day-timeline-track">
        {segments.map((b) => (
          <div
            key={b.id}
            className={`day-timeline-seg status-${b.status}`}
            style={{ left: `${pct(b.startMin)}%`, width: `${Math.max(pct(b.endMin) - pct(b.startMin), 0.6)}%` }}
            title={`${b.title} — ${b.organizerName}`}
          />
        ))}
        {nowMin >= DISPLAY_START && nowMin <= DISPLAY_END && (
          <div className="day-timeline-now" style={{ left: `${pct(nowMin)}%` }} />
        )}
      </div>
      <div className="day-timeline-ticks">
        {HOURS.map((h) => (
          <span key={h} className="day-timeline-tick" style={{ left: `${pct(h * 60)}%` }}>
            {h % 3 === 0 ? String(h).padStart(2, '0') : ''}
          </span>
        ))}
      </div>
    </div>
  );
}
