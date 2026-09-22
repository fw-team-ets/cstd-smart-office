import { useEffect, useRef, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { ChevronLeftIcon, ScanFaceIcon, CheckIcon, XIcon } from '../components/icons.jsx';
import { getPeople } from '../data/people.js';
import { checkIn } from '../data/bookings.js';
import { useCamera } from '../hooks/useCamera.js';
import { loadModels, detectFaceDescriptor, findBestMatch } from '../data/faceRecognition.js';

const SCAN_ATTEMPTS = 15;
const SCAN_INTERVAL_MS = 300;

// Quét camera thật, tính descriptor bằng face-api.js rồi so với faceDescriptor
// đã enroll của từng người (xem trang Admin) — không còn mô phỏng qua danh
// sách chọn tên nữa.
export default function CheckIn() {
  const navigate = useNavigate();
  const [people, setPeople] = useState([]);
  const [modelsReady, setModelsReady] = useState(false);
  const [modelError, setModelError] = useState('');
  const [phase, setPhase] = useState('idle'); // idle | scanning | done
  const [result, setResult] = useState(null);
  const [matchedName, setMatchedName] = useState('');
  const [loading, setLoading] = useState(true);
  const cancelRef = useRef(false);

  const { videoRef, videoEl, ready: cameraReady, error: cameraError } = useCamera(true);

  useEffect(() => {
    let active = true;
    getPeople().then((ppl) => { if (active) { setPeople(ppl); setLoading(false); } });
    loadModels().then(() => { if (active) setModelsReady(true); }).catch((err) => { if (active) setModelError(err.message || 'Could not load recognition model.'); });
    return () => { active = false; };
  }, []);

  useEffect(() => () => { cancelRef.current = true; }, []);

  // Tự quét ngay khi camera + model đã sẵn sàng — không cần bấm nút nào cả.
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
        if (!match) {
          finishScan({ ok: false, error: 'Face not recognized.' }, '');
          return;
        }
        const res = await checkIn({ personId: match.person.id, personName: match.person.name });
        finishScan(res, match.person.name);
        return;
      }
      await new Promise((r) => setTimeout(r, SCAN_INTERVAL_MS));
    }
    finishScan({ ok: false, error: 'No face detected. Please try again.' }, '');
  }

  function finishScan(res, name) {
    if (cancelRef.current) return;
    setResult(res);
    setMatchedName(name);
    setPhase('done');
    setTimeout(() => {
      navigate('/', { state: { flash: res.ok ? { type: 'success', text: `Welcome, ${name}.` } : { type: 'error', text: res.error } } });
    }, 1400);
  }

  if (loading) {
    return <div className="kiosk"><div className="kiosk-center"><p className="card-sub">Loading…</p></div></div>;
  }

  const cameraStatus = cameraError ? cameraError : modelError ? modelError : !cameraReady ? 'Starting camera…' : !modelsReady ? 'Loading recognition model…' : '';

  return (
    <div className="kiosk">
      <div className="kiosk-topbar">
        <div className="kiosk-topbar-left">
          <button className="icon-btn" onClick={() => navigate('/')} aria-label="Back"><ChevronLeftIcon /></button>
          <h1 className="kiosk-room-name">Check In</h1>
        </div>
      </div>

      <div className="kiosk-page-body">
        <div className="kiosk-page-inner" style={{ maxWidth: 740, textAlign: 'center' }}>
          <p className="card-sub" style={{ marginBottom: 14 }}>Look at the camera to check in</p>

          <div className={`scan-frame ${phase === 'scanning' ? 'scanning' : phase === 'done' ? 'recognized' : ''}`}>
            <video ref={videoRef} className="scan-video" autoPlay playsInline muted />
            <div className="scan-corner tl" /><div className="scan-corner tr" />
            <div className="scan-corner bl" /><div className="scan-corner br" />
            {!cameraReady && <div className="scan-icon"><ScanFaceIcon /></div>}
            <div className="scan-status">
              {phase === 'idle' && (cameraStatus || 'Preparing…')}
              {phase === 'scanning' && 'Scanning…'}
              {phase === 'done' && (result?.ok ? 'Face matched' : 'Not matched')}
            </div>
          </div>

          {phase === 'done' && (
            <div className={`banner ${result.ok ? 'success' : 'error'}`} style={{ marginTop: 16, justifyContent: 'center' }}>
              {result.ok ? <CheckIcon /> : <XIcon />}
              <span>{result.ok ? `Recognized — ${matchedName}` : result.error}</span>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
