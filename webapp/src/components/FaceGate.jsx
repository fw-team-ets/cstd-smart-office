// FaceGate.jsx — modal quét khuôn mặt, chỉ gọi onVerified(person) nếu khớp
// với một người có isAdmin=true trong danh sách truyền vào. Dùng để chặn các
// hành động nhạy cảm (pair/unpair cửa).
//
// CHỈ chặn ở phía webapp — firmware không có khái niệm admin, ai cầm được
// bearer token vẫn gọi /api/unpair trực tiếp được (xem data/esp32Client.js).

import { useEffect, useRef, useState } from 'react';
import { ScanFaceIcon, XIcon } from './icons.jsx';
import { useCamera } from '../hooks/useCamera.js';
import { detectFaceDescriptor, findBestMatch } from '../data/faceRecognition.js';

const SCAN_ATTEMPTS = 15;
const SCAN_INTERVAL_MS = 300;

export default function FaceGate({ title, subtitle, admins, modelsReady, onVerified, onCancel }) {
  const [phase, setPhase] = useState('idle'); // idle | scanning | denied
  const [deniedMsg, setDeniedMsg] = useState('');
  const cancelRef = useRef(false);
  const { videoRef, videoEl, ready: cameraReady, error: cameraError } = useCamera(true);

  useEffect(() => () => { cancelRef.current = true; }, []);

  useEffect(() => {
    if (phase === 'idle' && cameraReady && modelsReady && !cameraError) {
      handleScan();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [phase, cameraReady, modelsReady, cameraError]);

  async function handleScan() {
    setPhase('scanning');
    cancelRef.current = false;

    for (let attempt = 0; attempt < SCAN_ATTEMPTS; attempt++) {
      if (cancelRef.current) return;
      const descriptor = await detectFaceDescriptor(videoEl);
      if (descriptor) {
        const match = findBestMatch(descriptor, admins);
        if (cancelRef.current) return;
        if (!match) { deny('Face not recognized as an admin.'); return; }
        onVerified(match.person);
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

  if (admins.length === 0) {
    return (
      <div className="modal-overlay">
        <div className="modal-card">
          <p className="card-title" style={{ marginBottom: 12 }}>{title}</p>
          <div className="banner error"><XIcon /><span>No admin has an enrolled face yet — enroll one first.</span></div>
          <div className="btn-row" style={{ marginTop: 16 }}>
            <button className="btn subtle" onClick={onCancel}>Close</button>
          </div>
        </div>
      </div>
    );
  }

  const cameraStatus = cameraError || (!cameraReady ? 'Starting camera…' : !modelsReady ? 'Loading recognition model…' : '');

  return (
    <div className="modal-overlay">
      <div className="modal-card">
        <p className="card-title" style={{ marginBottom: 8 }}>{title}</p>
        <p className="card-sub" style={{ marginBottom: 14 }}>{subtitle || "Only an admin's face can do this — look at the camera."}</p>

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
          <div className="banner error" style={{ marginTop: 16, justifyContent: 'center' }}>
            <XIcon /><span>{deniedMsg}</span>
          </div>
        )}

        <div className="btn-row" style={{ marginTop: 16 }}>
          <button className="btn subtle" onClick={onCancel}>Cancel</button>
          {phase === 'denied' && (
            <button className="btn primary" onClick={() => setPhase('idle')}>Try Again</button>
          )}
        </div>
      </div>
    </div>
  );
}
