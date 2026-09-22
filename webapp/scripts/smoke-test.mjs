// smoke-test.mjs — chạy tay để kiểm tra bookings.js nói chuyện được với
// Firestore thật (bắt lỗi composite index thiếu sớm, trước khi test trên UI).
import { initializeApp } from 'firebase/app';
import { getFirestore } from 'firebase/firestore';
import fs from 'fs';

const envText = fs.readFileSync('.env.local', 'utf-8');
const env = {};
for (const line of envText.split('\n')) {
  const m = line.match(/^([A-Z_]+)=(.*)$/);
  if (m) env[m[1]] = m[2].trim();
}

const app = initializeApp({
  apiKey: env.VITE_FIREBASE_API_KEY,
  authDomain: env.VITE_FIREBASE_AUTH_DOMAIN,
  projectId: env.VITE_FIREBASE_PROJECT_ID,
  storageBucket: env.VITE_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: env.VITE_FIREBASE_MESSAGING_SENDER_ID,
  appId: env.VITE_FIREBASE_APP_ID,
});
const db = getFirestore(app);

const { collection, getDocs, query, where } = await import('firebase/firestore');

async function main() {
  console.log('--- people ---');
  const people = await getDocs(collection(db, 'people'));
  people.forEach((d) => console.log(d.id, d.data()));

  console.log('--- bookings active query (roomId== + status in) ---');
  const q = query(collection(db, 'bookings'), where('roomId', '==', 'room-a'), where('status', 'in', ['scheduled', 'checked_in']));
  const snap = await getDocs(q);
  console.log('active bookings count:', snap.size);

  console.log('--- bookings by date query (roomId== + date==) ---');
  const q2 = query(collection(db, 'bookings'), where('roomId', '==', 'room-a'), where('date', '==', '2026-01-01'));
  const snap2 = await getDocs(q2);
  console.log('date-filtered bookings count:', snap2.size);

  console.log('OK — no index errors.');
  process.exit(0);
}

main().catch((err) => {
  console.error('SMOKE TEST FAILED:', err);
  process.exit(1);
});
