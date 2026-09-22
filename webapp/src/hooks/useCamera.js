import { useCallback, useEffect, useRef, useState } from 'react';

const READY_TIMEOUT_MS = 8000;
const MAX_ATTEMPTS = 3;
const RETRY_DELAY_MS = 800;

function attachStream(el, stream, onReady) {
  el.srcObject = stream;
  el.onloadedmetadata = onReady;
  el.onloadeddata = onReady;
  el.onplaying = onReady;
  el.play().catch(() => {});
}

// Mở/tắt camera trước (selfie) theo cờ `active` — dùng cờ này để chỉ xin
// quyền camera đúng lúc cần (màn Check In khi mở trang, màn Admin chỉ khi mở
// modal enroll), tránh giữ camera mở suốt khi không dùng tới.
//
// videoRef trả ra là CALLBACK REF, và `stream` xin được cũng lưu bằng REACT
// STATE (không chỉ ref thường) — vì <video> có thể mount SAU khi camera đã
// xin xong (trang có màn "Loading…" ban đầu chưa render <video>), hoặc camera
// xin xong SAU khi <video> đã mount. 1 effect riêng phụ thuộc [videoEl, stream]
// sẽ tự gắn stream vào video bất kể cái nào tới trước — nếu chỉ dùng ref
// thường cho stream, effect đó sẽ không có cách nào biết để chạy lại khi
// stream xin xong (ref không kích hoạt re-render/re-run effect).
export function useCamera(active) {
  const [videoEl, setVideoEl] = useState(null);
  const videoRef = useCallback((el) => setVideoEl(el), []);
  const [stream, setStream] = useState(null);
  const [ready, setReady] = useState(false);
  const [error, setError] = useState('');
  const ctrl = useRef({ alive: false, starting: false, stream: null, ready: false });

  useEffect(() => {
    if (videoEl && stream && videoEl.srcObject !== stream) {
      attachStream(videoEl, stream, () => {
        if (!ctrl.current.alive) return;
        ctrl.current.ready = true;
        setReady(true);
      });
    }
  }, [videoEl, stream]);

  useEffect(() => {
    if (!active) { setReady(false); setStream(null); return; }
    ctrl.current.alive = true;
    ctrl.current.ready = false;
    setError('');

    // Đọc ctrl.current.ready (ref, luôn là giá trị mới nhất) thay vì đóng
    // gói biến render lúc effect này chạy — vì effect này chỉ chạy 1 lần khi
    // `active` không đổi, nếu check trực tiếp state/props ở đây sẽ bị "đông"
    // lại giá trị cũ của lần render đầu (stale closure), báo lỗi sai dù
    // camera đã lên hình từ effect [videoEl, stream] ở trên.
    const timeoutId = setTimeout(() => {
      if (ctrl.current.alive && !ctrl.current.ready) {
        setError('Camera did not start in time — check that no other app is using it, or try a different camera.');
      }
    }, READY_TIMEOUT_MS);

    async function start() {
      if (ctrl.current.starting || ctrl.current.stream) return;
      ctrl.current.starting = true;

      for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        if (!ctrl.current.alive) { ctrl.current.starting = false; return; }
        try {
          // Không ép facingMode: đây chỉ là gợi ý cho camera trước/sau trên
          // điện thoại — với webcam/virtual camera (DroidCam, OBS...) trên máy
          // tính, ép constraint không có ý nghĩa và một số driver xử lý sai,
          // treo luôn getUserMedia() không resolve/reject.
          const s = await navigator.mediaDevices.getUserMedia({
            video: { width: { ideal: 480 }, height: { ideal: 360 } },
            audio: false,
          });
          ctrl.current.starting = false;
          if (!ctrl.current.alive) { s.getTracks().forEach((t) => t.stop()); return; }
          ctrl.current.stream = s;
          setStream(s);
          return;
        } catch (err) {
          // Thiết bị vừa được 1 trang khác nhả ra (đóng camera) có thể cần
          // 1-2 giây mới thật sự rảnh — nhất là driver ảo như DroidCam. Thử
          // lại vài lần trước khi báo lỗi hẳn, thay vì fail ngay lần đầu.
          if (attempt === MAX_ATTEMPTS) {
            ctrl.current.starting = false;
            if (ctrl.current.alive) setError(err.message || 'Camera unavailable.');
            return;
          }
          await new Promise((r) => setTimeout(r, RETRY_DELAY_MS));
        }
      }
    }
    start();

    return () => {
      clearTimeout(timeoutId);
      ctrl.current.alive = false;
      setReady(false);
      setStream(null);
      if (ctrl.current.stream) {
        ctrl.current.stream.getTracks().forEach((t) => t.stop());
        ctrl.current.stream = null;
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- videoEl cố ý không nằm trong deps: chỉ để attach ngay nếu đã sẵn, không phải để restart việc xin camera.
  }, [active]);

  return { videoRef, videoEl, ready, error };
}
