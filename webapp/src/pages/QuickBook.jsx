import { useEffect, useMemo, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { ChevronLeftIcon, BoltIcon, InfoIcon, CheckIcon, XIcon } from '../components/icons.jsx';
import PersonPicker from '../components/PersonPicker.jsx';
import { room, durations } from '../data/mockData.js';
import { getPeople } from '../data/people.js';
import { createQuickBooking } from '../data/bookings.js';

function fmt(d) {
  return d.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

export default function QuickBook() {
  const navigate = useNavigate();

  const [people, setPeople] = useState([]);
  const [minutes, setMinutes] = useState(60);
  const [title, setTitle] = useState('Quick Meeting');
  const [bookedBy, setBookedBy] = useState(null);
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [result, setResult] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;
    getPeople().then((ppl) => {
      if (!active) return;
      setPeople(ppl);
      setBookedBy(ppl[0] || null);
      setLoading(false);
    });
    return () => { active = false; };
  }, []);

  const preview = useMemo(() => {
    const start = new Date();
    const end = new Date(start.getTime() + minutes * 60000);
    return { startLabel: fmt(start), endLabel: fmt(end) };
  }, [minutes]);

  async function handleBookNow() {
    if (!bookedBy) { setError('Choose who is booking.'); return; }
    setSubmitting(true);
    const res = await createQuickBooking({ minutes, title: title.trim() || 'Quick Meeting', organizerId: bookedBy.id, organizerName: bookedBy.name });
    setSubmitting(false);
    if (!res.ok) { setError(res.error); return; }
    setError('');
    setResult(res);
    setTimeout(() => navigate('/'), 1200);
  }

  if (loading) {
    return <div className="kiosk"><div className="kiosk-center"><p className="card-sub">Loading…</p></div></div>;
  }

  if (result) {
    return (
      <div className="kiosk">
        <div className="kiosk-center">
          <div className="status-hero-icon-circle success"><CheckIcon /></div>
          <h2 className="result-title">Room Booked!</h2>
          <p className="result-sub">{room.name} · {preview.startLabel} – {preview.endLabel} · {title} · by {bookedBy.name}</p>
          <p className="card-sub" style={{ marginTop: 8 }}>Door unlocked — no check-in needed.</p>
        </div>
      </div>
    );
  }

  return (
    <div className="kiosk">
      <div className="kiosk-topbar">
        <div className="kiosk-topbar-left">
          <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
          <h1 className="kiosk-room-name">Quick Book</h1>
        </div>
      </div>

      <div className="kiosk-page-body">
        <div className="kiosk-page-inner" style={{ maxWidth: 480 }}>
          <div className="card">
            <p className="card-title">Book {room.name} now</p>
            <p className="card-sub" style={{ marginBottom: 16 }}>Instantly reserve this room starting from now — door unlocks right away, no check-in needed.</p>

            <label style={{ display: 'block', fontSize: 12, fontWeight: 600, color: 'var(--text-dim)', marginBottom: 8 }}>Select Duration</label>
            <div className="chip-row">
              {durations.map((d) => (
                <div key={d.minutes} className={`chip ${minutes === d.minutes ? 'active' : ''}`} onClick={() => setMinutes(d.minutes)}>
                  {d.label}
                </div>
              ))}
            </div>

            <div className="field">
              <label htmlFor="qtitle">Meeting Title</label>
              <input id="qtitle" className="input" value={title} onChange={(e) => setTitle(e.target.value)} />
            </div>

            <div className="field">
              <label>Booked by</label>
              <PersonPicker people={people} value={bookedBy} onSelect={setBookedBy} />
            </div>

            {error && <div className="banner error"><XIcon /><span>{error}</span></div>}

            <button className="btn primary block" onClick={handleBookNow} disabled={submitting}>
              <BoltIcon /> {submitting ? 'Booking…' : 'Book Now'}
            </button>

            <div className="info-box">
              <InfoIcon />
              <span>The room will be booked immediately from {preview.startLabel} to {preview.endLabel}.</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
