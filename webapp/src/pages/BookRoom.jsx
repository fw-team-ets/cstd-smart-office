import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { ChevronLeftIcon, CheckIcon, XIcon } from '../components/icons.jsx';
import PersonPicker from '../components/PersonPicker.jsx';
import { room, BOOKING_ADVANCE_DAYS, TIME_STEP_MINUTES, DAY_START_MINUTES, DAY_END_MINUTES } from '../data/mockData.js';
import { getPeople } from '../data/people.js';
import { createScheduledBooking, getBookingsForDate } from '../data/bookings.js';
import { scheduleStatusBadge } from '../lib/scheduleBadge.js';

function minutesToLabel(totalMin) {
  if (totalMin === 1440) return '12:00 AM (next day)';
  let h = Math.floor(totalMin / 60);
  const m = totalMin % 60;
  const ap = h >= 12 ? 'PM' : 'AM';
  h = h % 12;
  if (h === 0) h = 12;
  return `${h}:${String(m).padStart(2, '0')} ${ap}`;
}

// Danh sách các mốc giờ HỢP LỆ (đúng bội số TIME_STEP_MINUTES) trong khoảng
// [minMinutes, maxMinutes] — dùng cho <select> để không thể chọn sai giờ:phút
// (input[type=time] của 1 số trình duyệt vẫn cho chọn tự do 0-59 phút rồi mới
// làm tròn, gây cảm giác "chọn 1 đằng ra 1 nẻo").
function buildTimeOptions(minMinutes, maxMinutes) {
  const out = [];
  for (let m = minMinutes; m <= maxMinutes; m += TIME_STEP_MINUTES) {
    out.push(m);
  }
  return out;
}

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

function dateAndMinutesToMs(dateKey, minutes) {
  const [y, m, d] = dateKey.split('-').map(Number);
  return new Date(y, m - 1, d, 0, 0, 0, 0).getTime() + minutes * 60000;
}

function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

function minutesNowRounded() {
  const now = new Date();
  const mins = now.getHours() * 60 + now.getMinutes();
  return Math.ceil(mins / TIME_STEP_MINUTES) * TIME_STEP_MINUTES;
}

const DATE_OPTIONS = buildDateOptions(BOOKING_ADVANCE_DAYS);
const TODAY_KEY = DATE_OPTIONS[0].key;

export default function BookRoom() {
  const navigate = useNavigate();

  const [people, setPeople] = useState([]);
  const [dateKey, setDateKey] = useState(DATE_OPTIONS[0].key);
  const [startMin, setStartMin] = useState(9 * 60);
  const [endMin, setEndMin] = useState(10 * 60);
  const [title, setTitle] = useState('');
  const [organizer, setOrganizer] = useState(null);
  const [attendees, setAttendees] = useState([]);
  const [dayAgenda, setDayAgenda] = useState([]);
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [submitted, setSubmitted] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;
    getPeople().then((ppl) => {
      if (!active) return;
      setPeople(ppl);
      setOrganizer(ppl[0] || null);
      setLoading(false);
    });
    return () => { active = false; };
  }, []);

  useEffect(() => {
    let active = true;
    getBookingsForDate(dateKey).then((b) => { if (active) setDayAgenda(b); });
    return () => { active = false; };
  }, [dateKey]);

  // Nếu ngày mặc định (hôm nay) mà giờ mặc định 9:00 AM đã qua rồi, tự đẩy lên.
  useEffect(() => {
    const floor = minutesNowRounded();
    if (startMin < floor) {
      setStartMin(floor);
      setEndMin((e) => (floor >= e ? Math.min(floor + TIME_STEP_MINUTES, DAY_END_MINUTES) : e));
    }
  }, []);

  const isToday = dateKey === TODAY_KEY;
  const minStartMin = isToday ? Math.min(Math.max(minutesNowRounded(), DAY_START_MINUTES), DAY_END_MINUTES - TIME_STEP_MINUTES) : DAY_START_MINUTES;

  function handleSelectDate(key) {
    setDateKey(key);
    if (key === TODAY_KEY) {
      const floor = minutesNowRounded();
      setStartMin((v) => {
        const next = Math.max(v, floor);
        if (next >= endMin) setEndMin(Math.min(next + TIME_STEP_MINUTES, DAY_END_MINUTES));
        return next;
      });
    }
  }

  function stepStart(delta) {
    setStartMin((v) => {
      const next = Math.min(Math.max(v + delta, minStartMin), DAY_END_MINUTES - TIME_STEP_MINUTES);
      if (next >= endMin) setEndMin(Math.min(next + TIME_STEP_MINUTES, DAY_END_MINUTES));
      return next;
    });
  }
  function stepEnd(delta) {
    setEndMin((v) => Math.min(Math.max(v + delta, startMin + TIME_STEP_MINUTES), DAY_END_MINUTES));
  }

  // <select> chỉ chứa mốc hợp lệ nên không cần làm tròn — chọn gì ra đúng đó.
  function handleStartSelect(value) {
    const next = Number(value);
    setStartMin(next);
    if (next >= endMin) setEndMin(Math.min(next + TIME_STEP_MINUTES, DAY_END_MINUTES));
  }
  function handleEndSelect(value) {
    setEndMin(Number(value));
  }

  function addAttendee(p) {
    if (!attendees.some((a) => a.id === p.id)) setAttendees([...attendees, p]);
  }
  function removeAttendee(id) {
    setAttendees(attendees.filter((a) => a.id !== id));
  }

  async function handleSubmit(e) {
    e.preventDefault();
    if (!organizer) { setError('Choose who is booking.'); return; }
    setSubmitting(true);
    const startAt = dateAndMinutesToMs(dateKey, startMin);
    const endAt = dateAndMinutesToMs(dateKey, endMin);
    const res = await createScheduledBooking({
      startAt, endAt,
      title: title.trim() || 'Untitled meeting',
      organizerId: organizer.id, organizerName: organizer.name,
      attendeeIds: attendees.map((a) => a.id), attendeeNames: attendees.map((a) => a.name),
    });
    setSubmitting(false);
    if (!res.ok) { setError(res.error); return; }
    setError('');
    setSubmitted({ startAt, endAt, title: title.trim() || 'Untitled meeting' });
    setTimeout(() => navigate('/'), 1200);
  }

  if (loading) {
    return <div className="kiosk"><div className="kiosk-center"><p className="card-sub">Loading…</p></div></div>;
  }

  if (submitted) {
    return (
      <div className="kiosk">
        <div className="kiosk-center">
          <div className="status-hero-icon-circle success"><CheckIcon /></div>
          <h2 className="result-title">Room Booked!</h2>
          <p className="result-sub">{room.name} · {fmtTime(submitted.startAt)} – {fmtTime(submitted.endAt)} · {submitted.title}</p>
        </div>
      </div>
    );
  }

  return (
    <div className="kiosk">
      <div className="kiosk-topbar">
        <div className="kiosk-topbar-left">
          <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
          <h1 className="kiosk-room-name">Book {room.name}</h1>
        </div>
      </div>

      <div className="kiosk-page-body">
        <div className="kiosk-page-inner">
          <div className="kiosk-columns">
            <form onSubmit={handleSubmit}>
              <div className="card">
                <div className="field">
                  <label>Date</label>
                  <div className="date-chip-row">
                    {DATE_OPTIONS.map((d, i) => (
                      <div key={d.key} className={`date-chip ${dateKey === d.key ? 'active' : ''}`} onClick={() => handleSelectDate(d.key)}>
                        <span className="dow">{i === 0 ? 'Today' : d.dow}</span>
                        <span className="dom">{d.dom}</span>
                      </div>
                    ))}
                  </div>
                </div>

                <div className="field-row">
                  <div className="field">
                    <label>Start</label>
                    <div className="time-stepper">
                      <button type="button" className="time-stepper-btn" onClick={() => stepStart(-TIME_STEP_MINUTES)}>–</button>
                      <select
                        className="time-stepper-value time-stepper-input"
                        value={startMin}
                        onChange={(e) => handleStartSelect(e.target.value)}
                      >
                        {buildTimeOptions(minStartMin, DAY_END_MINUTES - TIME_STEP_MINUTES).map((m) => (
                          <option key={m} value={m}>{minutesToLabel(m)}</option>
                        ))}
                      </select>
                      <button type="button" className="time-stepper-btn" onClick={() => stepStart(TIME_STEP_MINUTES)}>+</button>
                    </div>
                  </div>
                  <div className="field">
                    <label>End</label>
                    <div className="time-stepper">
                      <button type="button" className="time-stepper-btn" onClick={() => stepEnd(-TIME_STEP_MINUTES)}>–</button>
                      <select
                        className="time-stepper-value time-stepper-input"
                        value={endMin}
                        onChange={(e) => handleEndSelect(e.target.value)}
                      >
                        {buildTimeOptions(startMin + TIME_STEP_MINUTES, DAY_END_MINUTES).map((m) => (
                          <option key={m} value={m}>{minutesToLabel(m)}</option>
                        ))}
                      </select>
                      <button type="button" className="time-stepper-btn" onClick={() => stepEnd(TIME_STEP_MINUTES)}>+</button>
                    </div>
                  </div>
                </div>

                <div className="field">
                  <label htmlFor="title">Meeting Title</label>
                  <input id="title" className="input" placeholder="e.g. Project Sync" value={title} onChange={(e) => setTitle(e.target.value)} />
                </div>

                <div className="field">
                  <label>Booked by</label>
                  <PersonPicker people={people} value={organizer} onSelect={setOrganizer} />
                </div>

                <div className="field">
                  <label>Attendees (optional, search by name)</label>
                  <PersonPicker
                    people={people}
                    value={null}
                    onSelect={addAttendee}
                    placeholder="Add attendee…"
                    excludeIds={[organizer?.id, ...attendees.map((a) => a.id)].filter(Boolean)}
                  />
                  <div className="attendee-row">
                    {attendees.map((a) => (
                      <div key={a.id} className="attendee-chip" onClick={() => removeAttendee(a.id)}>
                        <span className="avatar sm">{a.initials}</span>{a.name}
                      </div>
                    ))}
                  </div>
                </div>

                {error && (
                  <div className="banner error" style={{ marginBottom: 0, marginTop: 12 }}>
                    <XIcon /><span>{error}</span>
                  </div>
                )}
              </div>

              <button type="submit" className="btn primary block" disabled={submitting}>
                {submitting ? 'Booking…' : 'Book Room'}
              </button>
            </form>

            <div className="card schedule-card">
              <p className="card-title" style={{ marginBottom: 10 }}>Schedule for this day</p>
              <div className="schedule-card-list">
                {dayAgenda.length === 0 && <p className="card-sub">No bookings yet.</p>}
                {dayAgenda.map((b) => {
                  const badge = scheduleStatusBadge(b.status);
                  return (
                    <div key={b.id} className={`agenda-row status-${b.status}`}>
                      <div className="agenda-time">{fmtTime(b.startAt)}<br />{fmtTime(b.endAt)}</div>
                      <div className="agenda-body">
                        <p className="agenda-title">{b.title}</p>
                        <p className="agenda-meta">{b.organizerName}</p>
                      </div>
                      <span className={`agenda-status-badge ${badge.cls}`}>{badge.label}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
