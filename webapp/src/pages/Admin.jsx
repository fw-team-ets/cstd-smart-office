import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { CheckIcon, XIcon, TrashIcon, ScanFaceIcon, CameraIcon, BuildingIcon } from '../components/icons.jsx';
import { getPeople, addPerson, enrollFace, clearFace, deletePerson } from '../data/people.js';
import { useCamera } from '../hooks/useCamera.js';
import { loadModels, detectFaceDescriptor, averageDescriptors, loadImageElement } from '../data/faceRecognition.js';

const CAPTURES_NEEDED = 3;
const MAX_UPLOAD_PHOTOS = 3;

// Trang quản trị RIÊNG, tách khỏi giao diện kiosk trên iPad — trang bình
// thường, cuộn được, dùng trên laptop/điện thoại bất cứ lúc nào (không phải
// đứng cạnh kiosk), để thêm người + enroll khuôn mặt trước khi ra kiosk dùng.
// Enroll bằng 1 trong 2 cách: camera của thiết bị đang mở trang này, hoặc
// upload sẵn 1-3 ảnh có mặt rõ. Cả 2 cách đều chỉ lưu vector số (không lưu
// ảnh) — xem faceRecognition.js.
export default function Admin() {
  const [people, setPeople] = useState([]);
  const [loading, setLoading] = useState(true);
  const [newName, setNewName] = useState('');
  const [busy, setBusy] = useState(false);
  const [modelError, setModelError] = useState('');
  const [modelsReady, setModelsReady] = useState(false);

  const [enrollingId, setEnrollingId] = useState(null);
  const [method, setMethod] = useState('camera'); // camera | upload
  const [captures, setCaptures] = useState([]);
  const [captureMsg, setCaptureMsg] = useState('');
  const [uploadFiles, setUploadFiles] = useState([]);
  const [uploadPreviews, setUploadPreviews] = useState([]);
  const [uploadMsg, setUploadMsg] = useState('');
  const [uploadBusy, setUploadBusy] = useState(false);
  const { videoRef, videoEl, ready: cameraReady, error: cameraError } = useCamera(!!enrollingId && method === 'camera');
  const cancelRef = useRef(false);

  async function refresh() {
    const ppl = await getPeople();
    setPeople(ppl);
    setLoading(false);
  }

  useEffect(() => {
    refresh();
    loadModels().then(() => setModelsReady(true)).catch((err) => setModelError(err.message || 'Could not load recognition model.'));
  }, []);

  async function handleAddPerson(e) {
    e.preventDefault();
    const name = newName.trim();
    if (!name) return;
    setBusy(true);
    await addPerson(name);
    setNewName('');
    setBusy(false);
    refresh();
  }

  async function handleDelete(id) {
    setBusy(true);
    await deletePerson(id);
    setBusy(false);
    refresh();
  }

  async function handleClearFace(id) {
    setBusy(true);
    await clearFace(id);
    setBusy(false);
    refresh();
  }

  function startEnroll(id) {
    cancelRef.current = false;
    setMethod('camera');
    setCaptures([]);
    setCaptureMsg('');
    setUploadFiles([]);
    setUploadMsg('');
    setEnrollingId(id);
  }

  function revokeUploadPreviews() {
    uploadPreviews.forEach((p) => URL.revokeObjectURL(p.url));
  }

  function cancelEnroll() {
    cancelRef.current = true;
    setEnrollingId(null);
    setCaptures([]);
    revokeUploadPreviews();
    setUploadFiles([]);
    setUploadPreviews([]);
  }

  async function finishEnroll(descriptors) {
    const avg = averageDescriptors(descriptors);
    await enrollFace(enrollingId, avg);
    setEnrollingId(null);
    setCaptures([]);
    revokeUploadPreviews();
    setUploadFiles([]);
    setUploadPreviews([]);
    refresh();
  }

  async function handleCapture() {
    if (!videoEl) return;
    setCaptureMsg('Capturing…');
    const descriptor = await detectFaceDescriptor(videoEl);
    if (cancelRef.current) return;
    if (!descriptor) {
      setCaptureMsg('No face detected — face the camera and try again.');
      return;
    }
    const next = [...captures, descriptor];
    setCaptures(next);
    if (next.length >= CAPTURES_NEEDED) {
      await finishEnroll(next);
    } else {
      setCaptureMsg(`Captured ${next.length}/${CAPTURES_NEEDED} — capture again from a slightly different angle.`);
    }
  }

  function handleFilesSelected(e) {
    const files = Array.from(e.target.files || []).slice(0, MAX_UPLOAD_PHOTOS);
    revokeUploadPreviews();
    setUploadFiles(files);
    setUploadPreviews(files.map((f) => ({ url: URL.createObjectURL(f) })));
    setUploadMsg('');
  }

  async function handleSaveUpload() {
    if (uploadFiles.length === 0) return;
    setUploadBusy(true);
    setUploadMsg('Processing photos…');
    const descriptors = [];
    let failed = 0;
    for (const file of uploadFiles) {
      try {
        const img = await loadImageElement(file);
        const descriptor = await detectFaceDescriptor(img);
        if (descriptor) descriptors.push(descriptor); else failed++;
      } catch {
        failed++;
      }
    }
    setUploadBusy(false);
    if (descriptors.length === 0) {
      setUploadMsg('No face detected in any of the uploaded photos.');
      return;
    }
    if (failed > 0) {
      setUploadMsg(`${failed} photo(s) had no detectable face and will be skipped. Saving the rest…`);
      await new Promise((r) => setTimeout(r, 1200));
    }
    await finishEnroll(descriptors);
  }

  if (loading) {
    return <div className="admin-shell"><div className="admin-container"><p className="card-sub">Loading…</p></div></div>;
  }

  const enrollingPerson = people.find((p) => p.id === enrollingId);

  return (
    <div className="admin-shell">
      <div className="admin-header">
        <div className="admin-header-title">
          <span className="icon-badge"><BuildingIcon /></span>
          <h1>eteams Admin — People &amp; Face Enrollment</h1>
        </div>
        <Link className="btn subtle" to="/">Open kiosk view</Link>
      </div>

      <div className="admin-container">
        <div className="card">
          <p className="card-title" style={{ marginBottom: 10 }}>Add person</p>
          <form onSubmit={handleAddPerson} className="btn-row">
            <input className="input" placeholder="Full name" value={newName} onChange={(e) => setNewName(e.target.value)} />
            <button className="btn primary" type="submit" disabled={busy || !newName.trim()}>Add</button>
          </form>
        </div>

        {modelError && <div className="banner error"><XIcon /><span>{modelError}</span></div>}
        {!modelsReady && !modelError && <div className="banner neutral"><span>Loading recognition model…</span></div>}

        <div className="card">
          <p className="card-title" style={{ marginBottom: 10 }}>People ({people.length})</p>
          {people.map((p) => (
            <div key={p.id} className="admin-person-row" style={{ marginBottom: 8 }}>
              <span className="avatar">{p.initials}</span>
              <div className="admin-person-info">
                <p className="agenda-title">{p.name}</p>
                <p className="agenda-meta">{p.faceDescriptor ? 'Face enrolled' : 'Not enrolled'}</p>
              </div>
              <div className="admin-person-actions">
                <button className="btn ghost" disabled={busy} onClick={() => startEnroll(p.id)}>
                  <ScanFaceIcon /> {p.faceDescriptor ? 'Re-enroll' : 'Enroll Face'}
                </button>
                {p.faceDescriptor && (
                  <button className="btn subtle" disabled={busy} onClick={() => handleClearFace(p.id)}>Clear</button>
                )}
                <button className="icon-btn" disabled={busy} onClick={() => handleDelete(p.id)} aria-label="Delete"><TrashIcon /></button>
              </div>
            </div>
          ))}
          {people.length === 0 && <p className="card-sub">No people yet — add one above.</p>}
        </div>
      </div>

      {enrollingId && (
        <div className="modal-overlay">
          <div className="modal-card">
            <p className="card-title" style={{ marginBottom: 12 }}>Enroll face — {enrollingPerson?.name}</p>

            <div className="enroll-tabs">
              <div className={`enroll-tab ${method === 'camera' ? 'active' : ''}`} onClick={() => setMethod('camera')}>
                <CameraIcon width={15} height={15} /> Use Camera
              </div>
              <div className={`enroll-tab ${method === 'upload' ? 'active' : ''}`} onClick={() => setMethod('upload')}>
                Upload Photo
              </div>
            </div>

            {method === 'camera' ? (
              <>
                <div className="scan-frame">
                  <video ref={videoRef} className="scan-video" autoPlay playsInline muted />
                  <div className="scan-corner tl" /><div className="scan-corner tr" />
                  <div className="scan-corner bl" /><div className="scan-corner br" />
                  {!cameraReady && <div className="scan-icon"><ScanFaceIcon /></div>}
                  <div className="scan-status">{cameraError || captureMsg || (cameraReady ? 'Ready' : 'Starting camera…')}</div>
                </div>
                <p className="card-sub" style={{ margin: '10px 0 16px' }}>Captured {captures.length}/{CAPTURES_NEEDED}</p>
                <div className="btn-row">
                  <button className="btn subtle" onClick={cancelEnroll}>Cancel</button>
                  <button className="btn primary" onClick={handleCapture} disabled={!cameraReady || !modelsReady}>
                    <CheckIcon /> Capture
                  </button>
                </div>
              </>
            ) : (
              <>
                <label className="upload-drop">
                  <input type="file" accept="image/*" multiple hidden onChange={handleFilesSelected} />
                  {uploadFiles.length === 0
                    ? `Tap to choose 1-${MAX_UPLOAD_PHOTOS} photos with a clear face`
                    : `${uploadFiles.length} photo(s) selected`}
                </label>
                {uploadPreviews.length > 0 && (
                  <div className="upload-previews">
                    {uploadPreviews.map((p, i) => (
                      <img key={i} className="upload-preview" src={p.url} alt="" />
                    ))}
                  </div>
                )}
                {uploadMsg && <p className="card-sub" style={{ margin: '10px 0 0' }}>{uploadMsg}</p>}
                <div className="btn-row" style={{ marginTop: 16 }}>
                  <button className="btn subtle" onClick={cancelEnroll}>Cancel</button>
                  <button className="btn primary" onClick={handleSaveUpload} disabled={uploadFiles.length === 0 || uploadBusy || !modelsReady}>
                    <CheckIcon /> {uploadBusy ? 'Processing…' : 'Save'}
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
