import { useCallback, useEffect, useState } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import {
  BuildingIcon,
  CalendarIcon,
  BoltIcon,
  CameraIcon,
  CheckIcon,
  XIcon,
  ScanFaceIcon,
  DoorOpenIcon,
} from '../components/icons.jsx';
import { room } from '../data/mockData.js';
import { getCurrentMeeting, getBookingsForDate } from '../data/bookings.js';

function todayKey() {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString('en-US', {
    hour: 'numeric',
    minute: '2-digit',
  });
}

function statusBadge(booking, nextBookingId) {
  if (booking.status === 'checked_in') return { cls: 'now', label: 'Now' };
  if (booking.status === 'completed') return { cls: 'completed', label: 'Completed' };
  if (booking.status === 'no_show') return { cls: 'cancelled', label: 'No-show' };
  if (booking.status === 'cancelled') return { cls: 'cancelled', label: 'Cancelled' };
  if (booking.id === nextBookingId) return { cls: 'next', label: 'Next' };
  return { cls: 'upcoming', label: 'Upcoming' };
}

const AGENDA_VISIBLE_COUNT = 4;

function pickVisibleAgenda(agenda, anchorId) {
  const anchorIdx = agenda.findIndex(
    (b) => b.status === 'checked_in' || b.id === anchorId,
  );

  if (anchorIdx === -1) {
    return agenda.slice(-AGENDA_VISIBLE_COUNT);
  }

  const start = Math.max(0, anchorIdx - 1);
  return agenda.slice(start, start + AGENDA_VISIBLE_COUNT);
}

export default function RoomHome() {
  const navigate = useNavigate();
  const location = useLocation();

  const [now, setNow] = useState(() => new Date());
  const [meeting, setMeeting] = useState(null);
  const [agenda, setAgenda] = useState([]);
  const [flash, setFlash] = useState(location.state?.flash || null);
  const [loading, setLoading] = useState(true);

  const refresh = useCallback(async () => {
    const [m, a] = await Promise.all([
      getCurrentMeeting(),
      getBookingsForDate(todayKey()),
    ]);

    setNow(new Date());
    setMeeting(m);
    setAgenda(a);
    setLoading(false);
  }, []);

  useEffect(() => {
    refresh();
    const t = setInterval(refresh, 15000);
    return () => clearInterval(t);
  }, [refresh]);

  useEffect(() => {
    if (!flash) return;
    const t = setTimeout(() => setFlash(null), 4000);
    return () => clearTimeout(t);
  }, [flash]);

  if (loading) {
    return (
      <div className="kiosk">
        <div className="kiosk-center">
          <p className="card-sub">Loading…</p>
        </div>
      </div>
    );
  }

  const busyState = !!meeting;

  const nextBooking = !busyState
    ? agenda.find(
        (b) => b.status === 'scheduled' && b.startAt > now.getTime(),
      )
    : null;

  const visibleAgenda = pickVisibleAgenda(agenda, nextBooking?.id);

  return (
    <div className="kiosk">
      <div className="kiosk-body">

        <main className="kiosk-main">
          <header className="kiosk-main-header">
            <div className="kiosk-topbar-left">
              <div className="icon-badge lg">
                <BuildingIcon />
              </div>

              <div>
                <h1 className="kiosk-room-name">{room.name}</h1>
                <p className="kiosk-room-sub">
                  Level {room.level} &nbsp;•&nbsp; {room.capacity} people
                </p>
              </div>
            </div>

            <div className="kiosk-clock">
              <span className="date">
                {now.toLocaleDateString('en-US', {
                  weekday: 'short',
                  month: 'short',
                  day: 'numeric',
                  year: 'numeric',
                })}
              </span>
              {now.toLocaleTimeString('en-US', {
                hour: 'numeric',
                minute: '2-digit',
              })}
            </div>
          </header>

          <div className="kiosk-main-body">
            {flash && (
              <div className={`kiosk-flash ${flash.type}`}>
                {flash.type === 'success' ? <CheckIcon /> : <XIcon />}
                {flash.text}
              </div>
            )}

            {!busyState ? (
              <>
                <div className="status-hero">
                  <div className="status-hero-icon-circle">
                    <DoorOpenIcon />
                  </div>

                  <h2 className="status-hero-title">Available</h2>

                  <p className="status-hero-sub">
                    {nextBooking
                      ? `Free until ${fmtTime(nextBooking.startAt)}`
                      : 'Free for the rest of today'}
                  </p>
                </div>

                <div className="kiosk-action-row">
                  <button
                    type="button"
                    className="kiosk-action-btn primary"
                    onClick={() => navigate('/book')}
                  >
                    <span className="icon-badge">
                      <CalendarIcon />
                    </span>
                    Book Room
                  </button>

                  <button
                    type="button"
                    className="kiosk-action-btn"
                    onClick={() => navigate('/quick-book')}
                  >
                    <span className="icon-badge">
                      <BoltIcon />
                    </span>
                    Quick Book
                  </button>

                  <button
                    type="button"
                    className="kiosk-action-btn"
                    onClick={() => navigate('/check-in')}
                  >
                    <span className="icon-badge">
                      <CameraIcon />
                    </span>
                    Check In
                  </button>
                </div>

                <button
                  type="button"
                  className="btn subtle pill"
                  onClick={() => navigate('/schedule')}
                >
                  <CalendarIcon /> View Full Schedule
                </button>
              </>
            ) : (
              <div className="kiosk-meeting-card">
                <div className="status-hero">
                  <div className="status-hero-icon-circle busy">
                    <DoorOpenIcon />
                  </div>

                  <span className="status-hero-badge">In Meeting</span>

                  <h2 className="status-hero-title meeting">
                    {meeting.title}
                  </h2>

                  <p className="status-hero-sub">
                    Organizer: {meeting.organizerName}
                    {meeting.attendeeNames?.length
                      ? ` · ${meeting.attendeeNames.length} attendees`
                      : ''}
                  </p>

                  <p className="status-hero-time">
                    {fmtTime(meeting.startAt)} – {fmtTime(meeting.endAt)}
                    {meeting.autoExtended ? ' (extended)' : ''}
                  </p>
                </div>

                <button
                  type="button"
                  className="btn danger block"
                  style={{ margin: '16px 0 12px' }}
                  onClick={() => navigate('/checkout')}
                >
                  <ScanFaceIcon /> Checkout / Extend (scan face)
                </button>

                <div className="btn-row">
                  <button
                    type="button"
                    className="btn subtle"
                    onClick={() => navigate('/check-in')}
                  >
                    <CameraIcon /> Check In (arriving attendee)
                  </button>

                  <button
                    type="button"
                    className="btn subtle"
                    onClick={() => navigate('/book')}
                  >
                    <CalendarIcon /> Book Room (for later)
                  </button>
                </div>
              </div>
            )}
          </div>

          <div className="kiosk-reference-footer">
            A SMARTER WAY TO MEET
            <div className="bars">
              <span />
              <span />
            </div>
          </div>
        </main>

        <aside className="kiosk-agenda">
          <div className="kiosk-agenda-head">
            <p className="kiosk-agenda-title">Today's Schedule</p>

            <span className="kiosk-agenda-date">
              {now.toLocaleDateString('en-US', {
                month: 'short',
                day: 'numeric',
                year: 'numeric',
              })}
            </span>
          </div>

          <div className="kiosk-agenda-list">
            {visibleAgenda.length === 0 && (
              <p className="kiosk-agenda-empty">No bookings today.</p>
            )}

            {visibleAgenda.map((b) => {
              const badge = statusBadge(b, nextBooking?.id);
              const nextRow = b.id === nextBooking?.id;

              return (
                <div
                  key={b.id}
                  className={[
                    'agenda-row',
                    `status-${b.status}`,
                    nextRow ? 'next-row' : '',
                  ].filter(Boolean).join(' ')}
                >
                  <div className="agenda-time">
                    {fmtTime(b.startAt)}
                    <br />
                    {fmtTime(b.endAt)}
                  </div>

                  <div className="agenda-body">
                    <p className="agenda-title">{b.title}</p>
                    <p className="agenda-meta">{b.organizerName}</p>
                  </div>

                  <span className={`agenda-status-badge ${badge.cls}`}>
                    {badge.label}
                  </span>
                </div>
              );
            })}
          </div>

          <button
            type="button"
            className="kiosk-agenda-room-cta"
            onClick={() => navigate('/schedule')}
            aria-label="Check other available meeting rooms"
          >
            <span className="icon-badge">
              <BuildingIcon />
            </span>

            <span className="kiosk-agenda-room-cta-copy">
              <span className="kiosk-agenda-room-cta-title">
                Need a room?
              </span>
              <span className="kiosk-agenda-room-cta-sub">
                Check other available meeting rooms
              </span>
            </span>

            <span className="kiosk-agenda-room-cta-arrow">›</span>
          </button>
        </aside>

      </div>
    </div>
  );
}
