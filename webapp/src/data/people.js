// people.js — danh bạ nội bộ (Firestore collection "people").
// faceDescriptor: number[128] | null — vector khuôn mặt lấy từ face-api.js,
// ghi qua enrollFace() ở màn Admin. Chưa enroll thì null, không check-in được
// bằng khuôn mặt (checkIn() ở bookings.js sẽ không nhận diện được người này).

import { collection, addDoc, doc, updateDoc, deleteDoc, getDocs } from 'firebase/firestore';
import { db } from './firebase.js';

export async function getPeople() {
  const snap = await getDocs(collection(db, 'people'));
  return snap.docs.map((d) => ({ id: d.id, ...d.data() }));
}

function initialsOf(name) {
  return name.trim().split(/\s+/).map((w) => w[0]).slice(-2).join('').toUpperCase();
}

export async function addPerson(name) {
  const ref = await addDoc(collection(db, 'people'), {
    name, initials: initialsOf(name), faceDescriptor: null,
  });
  return { id: ref.id, name, initials: initialsOf(name), faceDescriptor: null };
}

export async function enrollFace(personId, descriptor) {
  await updateDoc(doc(db, 'people', personId), { faceDescriptor: descriptor });
}

export async function clearFace(personId) {
  await updateDoc(doc(db, 'people', personId), { faceDescriptor: null });
}

export async function deletePerson(personId) {
  await deleteDoc(doc(db, 'people', personId));
}
