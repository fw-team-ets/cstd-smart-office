// faceRecognition.js — bọc @vladmandic/face-api (chạy hoàn toàn trong
// browser qua TensorFlow.js, không cần backend). Model weights nằm ở
// public/models (copy từ node_modules/@vladmandic/face-api/model khi cài đặt
// — xem README nếu cần cài lại từ đầu).

import * as faceapi from '@vladmandic/face-api';

const MODEL_URL = '/models';

// Khoảng cách Euclidean giữa 2 descriptor 128-chiều — nhỏ hơn ngưỡng này thì
// coi là cùng 1 người. 0.5-0.6 là khoảng khuyến nghị phổ biến cho model này;
// hạ thấp hơn (khó nhận hơn) nếu thấy nhận sai người, tăng lên nếu quét mãi
// không nhận ra ai.
export const MATCH_THRESHOLD = 0.55;

const DETECT_OPTIONS = new faceapi.TinyFaceDetectorOptions({ inputSize: 224, scoreThreshold: 0.5 });

let loadPromise = null;
export function loadModels() {
  if (!loadPromise) {
    loadPromise = (async () => {
      // face-api.js không tự chọn backend nữa (tfjs-core bản mới bỏ auto-pick) —
      // phải tự set trước khi load model, không thì ném lỗi "backend chưa init".
      // webgl chạy được trên mọi browser/iPad Safari hiện đại; cpu là lưới an
      // toàn cho môi trường không có WebGL (ví dụ máy CI/headless).
      try {
        await faceapi.tf.setBackend('webgl');
      } catch {
        await faceapi.tf.setBackend('cpu');
      }
      await faceapi.tf.ready();
      await Promise.all([
        faceapi.nets.tinyFaceDetector.loadFromUri(MODEL_URL),
        faceapi.nets.faceLandmark68Net.loadFromUri(MODEL_URL),
        faceapi.nets.faceRecognitionNet.loadFromUri(MODEL_URL),
      ]);
    })();
  }
  return loadPromise;
}

// Chạy detector + landmark (để canh mặt) + recognition trên 1 khung hình
// video/canvas hiện tại. Trả về descriptor (number[128]) hoặc null nếu
// không thấy mặt nào.
export async function detectFaceDescriptor(videoEl) {
  const result = await faceapi
    .detectSingleFace(videoEl, DETECT_OPTIONS)
    .withFaceLandmarks()
    .withFaceDescriptor();
  return result ? Array.from(result.descriptor) : null;
}

function euclideanDistance(a, b) {
  let sum = 0;
  for (let i = 0; i < a.length; i++) sum += (a[i] - b[i]) ** 2;
  return Math.sqrt(sum);
}

// So descriptor vừa quét với descriptor đã enroll của từng người, trả về
// người khớp nhất nếu khoảng cách nằm trong ngưỡng — không thì null.
export function findBestMatch(descriptor, people) {
  let best = null;
  for (const person of people) {
    if (!person.faceDescriptor || person.faceDescriptor.length !== descriptor.length) continue;
    const distance = euclideanDistance(descriptor, person.faceDescriptor);
    if (!best || distance < best.distance) best = { person, distance };
  }
  return best && best.distance <= MATCH_THRESHOLD ? best : null;
}

// Enroll nhiều ảnh (2-3 lần chụp/upload) rồi lấy trung bình — giảm nhiễu so
// với chỉ 1 tấm duy nhất.
export function averageDescriptors(list) {
  const len = list[0].length;
  const sum = new Array(len).fill(0);
  for (const d of list) for (let i = 0; i < len; i++) sum[i] += d[i];
  return sum.map((v) => v / list.length);
}

// Đọc 1 file ảnh (input type=file) thành <img> đã load xong, để chạy
// detectFaceDescriptor() trên đó — dùng cho luồng "upload ảnh" enroll.
export function loadImageElement(file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Could not read image file.')); };
    img.src = url;
  });
}
