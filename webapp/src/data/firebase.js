// firebase.js — khởi tạo kết nối Firebase, đọc config từ biến môi trường
// (xem .env.example). Chưa điền .env.local thì import file này sẽ lỗi —
// đó là lỗi mong đợi, không phải bug, cho tới khi có Firebase project thật.

import { initializeApp } from 'firebase/app';
import { getFirestore } from 'firebase/firestore';

const firebaseConfig = {
  apiKey: import.meta.env.VITE_FIREBASE_API_KEY,
  authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,
  projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,
  storageBucket: import.meta.env.VITE_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: import.meta.env.VITE_FIREBASE_MESSAGING_SENDER_ID,
  appId: import.meta.env.VITE_FIREBASE_APP_ID,
};

if (!firebaseConfig.projectId) {
  throw new Error(
    'Thiếu Firebase config — tạo file .env.local từ .env.example và điền giá trị lấy từ Firebase Console.'
  );
}

const app = initializeApp(firebaseConfig);
export const db = getFirestore(app);
