import { useEffect, useRef, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { ChevronLeftIcon, ScanFaceIcon, CheckIcon, XIcon, DoorIcon, ClockIcon } from '../components/icons.jsx';
import { getPeople } from '../data/people.js';
import { getCurrentMeeting, checkOut, extend } from '../data/bookings.js';
import { useCamera } from '../hooks/useCamera.js';
import { loadModels, detectFaceDescriptor, findBestMatch } from '../data/faceRecognition.js';

const SCAN_ATTEMPTS = 15;
const SCAN_INTERVAL_MS = 300;

function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}

// Checkout/Extend cũng phải quét khuôn mặt như Check In — chỉ khác là sau khi
// nhận diện xong còn phải kiểm tra đúng là organizer của cuộc họp đang diễn ra
// thì mới cho Checkout/Extend (đúng yêu cầu "chỉ người đăng ký mới được kết
// thúc cuộc họp"). Không còn chọn danh tính qua PersonPicker nữa.
export default function Checkout() {
  const navigate = useNavigate();
  const [people, setPeople] = useState([]);
  const [meeting, setMeeting] = useState(null);
  const [loading, setLoading] = useState(true);
  const [modelsReady, setModelsReady] = useState(false);
  const [modelError, setModelError] = useState('');
  const [phase, setPhase] = useState('idle'); // idle | scanning | verified | denied
  const [deniedMsg, setDeniedMsg] = useState('');
  const [matchedPerson, setMatchedPerson] = useState(null);
  const [actionBusy, setActionBusy] = useState(false);
  const cancelRef = useRef(false);

  const { videoRef, videoEl, ready: cameraReady, error: cameraError } = useCamera(phase === 'idle' || phase === 'scanning');

  useEffect(() => {
    let active = true;
    Promise.all([getPeople(), getCurrentMeeting()]).then(([ppl, m]) => {
      if (!active) return;
      setPeople(ppl);
      setMeeting(m);
      setLoading(false);
    });
    loadModels().then(() => { if (active) setModelsReady(true); }).catch((err) => { if (active) setModelError(err.message || 'Could not load recognition model.'); });
    return () => { active = false; };
  }, []);

  useEffect(() => () => { cancelRef.current = true; }, []);

  // Tự quét ngay khi camera + model đã sẵn sàng — không cần bấm nút nào cả
  // (kể cả lúc bấm "Try Again" reset về 'idle' cũng tự quét lại luôn).
  useEffect(() => {
    if (phase === 'idle' && cameraReady && modelsReady && !cameraError && !modelError) {
      handleScan();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [phase, cameraReady, modelsReady, cameraError, modelError]);

  async function handleScan() {
    setPhase('scanning');
    cancelRef.current = false;

    for (let attempt = 0; attempt < SCAN_ATTEMPTS; attempt++) {
      if (cancelRef.current) return;
      const descriptor = await detectFaceDescriptor(videoEl);
      if (descriptor) {
        const match = findBestMatch(descriptor, people);
        if (cancelRef.current) return;
        if (!match) { deny('Face not recognized.'); return; }
        if (match.person.id !== meeting.organizerId) { deny(`Only the organizer (${meeting.organizerName}) can manage this meeting.`); return; }
        setMatchedPerson(match.person);
        setPhase('verified');
        return;
      }
      await new Promise((r) => setTimeout(r, SCAN_INTERVAL_MS));
    }
    deny('No face detected. Please try again.');
  }

  function deny(msg) {
    if (cancelRef.current) return;
    setDeniedMsg(msg);
    setPhase('denied');
  }

  async function handleCheckOut() {
    setActionBusy(true);
    const res = await checkOut({ personId: matchedPerson.id });
    setActionBusy(false);
    navigate('/', { state: { flash: res.ok ? { type: 'success', text: 'Meeting ended. Room is available.' } : { type: 'error', text: res.error } } });
  }

  async function handleExtend(minutes) {
    setActionBusy(true);
    const res = await extend({ personId: matchedPerson.id, addMinutes: minutes });
    setActionBusy(false);
    navigate('/', { state: { flash: res.ok ? { type: 'success', text: `Extended to ${fmtTime(res.endAt)}.` } : { type: 'error', text: res.error } } });
  }

  if (loading) {
    return <div className="kiosk"><div className="kiosk-center"><p className="card-sub">Loading…</p></div></div>;
  }

  if (!meeting) {
    return (
      <div className="kiosk">
        <div className="kiosk-topbar">
          <div className="kiosk-topbar-left">
            <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
            <h1 className="kiosk-room-name">Checkout</h1>
          </div>
        </div>
        <div className="kiosk-page-body"><p className="card-sub">No meeting is currently in progress.</p></div>
      </div>
    );
  }

  const cameraStatus = cameraError ? cameraError : modelError ? modelError : !cameraReady ? 'Starting camera…' : !modelsReady ? 'Loading recognition model…' : '';

  return (
    <div className="kiosk">
      <div className="kiosk-topbar">
        <div className="kiosk-topbar-left">
          <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
          <h1 className="kiosk-room-name">Checkout / Extend</h1>
        </div>
      </div>

      <div className="kiosk-page-body">
        <div className="kiosk-page-inner" style={{ maxWidth: 740, textAlign: 'center' }}>
          {phase !== 'verified' && (
            <>
              <p className="card-sub" style={{ marginBottom: 14 }}>Only the organizer can end or extend this meeting — look at the camera to verify.</p>

              <div className={`scan-frame ${phase === 'scanning' ? 'scanning' : phase === 'denied' ? 'recognized' : ''}`}>
                <video ref={videoRef} className="scan-video" autoPlay playsInline muted />
                <div className="scan-corner tl" /><div className="scan-corner tr" />
                <div className="scan-corner bl" /><div className="scan-corner br" />
                {!cameraReady && <div className="scan-icon"><ScanFaceIcon /></div>}
                <div className="scan-status">
                  {phase === 'idle' && (cameraStatus || 'Preparing…')}
                  {phase === 'scanning' && 'Scanning…'}
                  {phase === 'denied' && 'Not verified'}
                </div>
              </div>

              {phase === 'denied' && (
                <>
                  <div className="banner error" style={{ marginTop: 16, justifyContent: 'center' }}>
                    <XIcon /><span>{deniedMsg}</span>
                  </div>
                  <button className="btn subtle block" onClick={() => setPhase('idle')}>Try Again</button>
                </>
              )}
            </>
          )}

          {phase === 'verified' && (
            <div className="kiosk-meeting-card">
              <div className="banner success" style={{ justifyContent: 'center' }}>
                <CheckIcon /><span>Verified — {matchedPerson.name}</span>
              </div>
              <div className="card" style={{ textAlign: 'left', marginTop: 12 }}>
                <p className="card-title">{meeting.title}</p>
                <p className="card-sub">{fmtTime(meeting.startAt)} – {fmtTime(meeting.endAt)}{meeting.autoExtended ? ' (extended)' : ''}</p>
                <div className="kiosk-meeting-actions" style={{ marginTop: 14 }}>
                  <button className="btn danger" style={{ marginRight: 30 }} disabled={actionBusy} onClick={handleCheckOut}>
                    <DoorIcon /> Checkout
                  </button>
                  <button className="btn ghost" style={{ marginRight: 10 }} disabled={actionBusy} onClick={() => handleExtend(15)}><ClockIcon /> +15m</button>
                  <button className="btn ghost" disabled={actionBusy} onClick={() => handleExtend(30)}><ClockIcon /> +30m</button>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
