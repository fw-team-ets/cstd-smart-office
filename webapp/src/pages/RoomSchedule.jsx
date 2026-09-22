import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { ChevronLeftIcon } from '../components/icons.jsx';
import { room, BOOKING_ADVANCE_DAYS } from '../data/mockData.js';
import { getBookingsForDate } from '../data/bookings.js';
import { scheduleStatusBadge } from '../lib/scheduleBadge.js';

function buildDateOptions(days) {
  const out = [];
  const base = new Date();
  base.setHours(0, 0, 0, 0);
  for (let i = 0; i < days; i++) {
    const d = new Date(base);
    d.setDate(base.getDate() + i);
    const key = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    out.push({ key, dow: d.toLocaleDateString('en-US', { weekday: 'short' }), dom: d.getDate() });
  }
  return out;
}

function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

const DATE_OPTIONS = buildDateOptions(BOOKING_ADVANCE_DAYS);

export default function RoomSchedule() {
  const navigate = useNavigate();
  const [dateKey, setDateKey] = useState(DATE_OPTIONS[0].key);
  const [bookings, setBookings] = useState([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;
    setLoading(true);
    getBookingsForDate(dateKey).then((b) => { if (active) { setBookings(b); setLoading(false); } });
    return () => { active = false; };
  }, [dateKey]);

  return (
    <div className="kiosk">
      <div className="kiosk-topbar">
        <div className="kiosk-topbar-left">
          <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
          <h1 className="kiosk-room-name">Room Schedule</h1>
        </div>
      </div>

      <div className="kiosk-page-body">
        <div className="kiosk-page-inner" style={{ maxWidth: 640 }}>
          <div className="card schedule-card schedule-card-tall">
            <p className="card-title" style={{ marginBottom: 10 }}>{room.name}</p>
            <div className="date-chip-row">
              {DATE_OPTIONS.map((d, i) => (
                <div key={d.key} className={`date-chip ${dateKey === d.key ? 'active' : ''}`} onClick={() => setDateKey(d.key)}>
                  <span className="dow">{i === 0 ? 'Today' : d.dow}</span>
                  <span className="dom">{d.dom}</span>
                </div>
              ))}
            </div>

            <div className="schedule-card-list">
              {loading ? (
                <p className="card-sub">Loading…</p>
              ) : bookings.length === 0 ? (
                <p className="card-sub">No bookings for this day.</p>
              ) : (
                bookings.map((b) => {
                  const badge = scheduleStatusBadge(b.status);
                  return (
                    <div key={b.id} className={`agenda-row status-${b.status}`}>
                      <div className="agenda-time">{fmtTime(b.startAt)}<br />{fmtTime(b.endAt)}</div>
                      <div className="agenda-body">
                        <p className="agenda-title">{b.title}</p>
                        <p className="agenda-meta">{b.organizerName}{b.attendeeNames?.length ? ` +${b.attendeeNames.length}` : ''}</p>
                      </div>
                      <span className={`agenda-status-badge ${badge.cls}`}>{badge.label}</span>
                    </div>
                  );
                })
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
