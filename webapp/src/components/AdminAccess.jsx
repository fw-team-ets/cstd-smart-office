// AdminAccess.jsx — nút nhỏ ở góc màn kiosk, bấm vào hỏi tài khoản/mật khẩu
// rồi mới chuyển sang /admin. Thay cho việc mở /admin qua "Add to Home
// Screen" riêng (iOS cách ly storage giữa các icon khác nhau, xem trao đổi
// trước) — giờ /admin chỉ mở TỪ TRONG chính app kiosk, không rời app, không
// có vấn đề storage.
//
// LƯU Ý: đây là hàng rào TIỆN LỢI, không phải bảo mật thật — tài khoản/mật
// khẩu ('admin'/'admin') nằm ngay trong mã JS gửi cho trình duyệt, ai mở
// DevTools đọc source cũng thấy được. Chặn thật (đổi mật khẩu, giới hạn theo
// người dùng, v.v.) cần một lớp khác — việc đó đang được hoãn lại theo yêu
// cầu trước đó.

import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { SettingsIcon, XIcon } from './icons.jsx';

const ADMIN_USERNAME = 'admin';
const ADMIN_PASSWORD = 'admin';

export default function AdminAccess() {
  const navigate = useNavigate();
  const [open, setOpen] = useState(false);
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');

  function handleOpen() {
    setUsername('');
    setPassword('');
    setError('');
    setOpen(true);
  }

  function handleSubmit(e) {
    e.preventDefault();
    if (username === ADMIN_USERNAME && password === ADMIN_PASSWORD) {
      setOpen(false);
      navigate('/admin');
    } else {
      setError('Wrong username or password.');
    }
  }

  return (
    <>
      <button
        type="button"
        className="admin-access-btn"
        aria-label="Admin sign in"
        onClick={handleOpen}
      >
        <SettingsIcon />
      </button>

      {open && (
        <div className="modal-overlay">
          <div className="modal-card">
            <p className="card-title" style={{ marginBottom: 12 }}>Admin sign in</p>
            <form onSubmit={handleSubmit}>
              <input
                className="input" style={{ width: '100%', marginBottom: 10 }}
                placeholder="Username" autoFocus autoComplete="username"
                value={username} onChange={(e) => setUsername(e.target.value)}
              />
              <input
                className="input" style={{ width: '100%', marginBottom: 12 }}
                placeholder="Password" type="password" autoComplete="current-password"
                value={password} onChange={(e) => setPassword(e.target.value)}
              />
              {error && (
                <div className="banner error" style={{ marginBottom: 12 }}>
                  <XIcon /><span>{error}</span>
                </div>
              )}
              <div className="btn-row">
                <button type="button" className="btn subtle" onClick={() => setOpen(false)}>Cancel</button>
                <button type="submit" className="btn primary">Sign in</button>
              </div>
            </form>
          </div>
        </div>
      )}
    </>
  );
}
