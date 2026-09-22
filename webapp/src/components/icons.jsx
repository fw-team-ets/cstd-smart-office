// icons.jsx — icon SVG vẽ tay, không phụ thuộc icon font/CDN ngoài.
const base = { fill: 'none', stroke: 'currentColor', strokeWidth: 2, strokeLinecap: 'round', strokeLinejoin: 'round' };

export const BuildingIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><rect x="4" y="3" width="16" height="18" rx="2"/><path d="M9 8h.01M15 8h.01M9 12h.01M15 12h.01M9 16h.01M15 16h.01"/></svg>
);
export const DoorIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>
);
export const DoorOpenIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}>
    <rect x="7" y="3.5" width="10" height="17" rx="2.5"/>
    <circle cx="14" cy="12.5" r=".9" fill="currentColor" stroke="none"/>
    <path d="M4 20.5h16"/>
  </svg>
);
export const CalendarIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><rect x="3" y="5" width="18" height="16" rx="2"/><path d="M8 3v4M16 3v4M3 10h18"/></svg>
);
export const BoltIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M13 2 3 14h7l-1 8 10-12h-7l1-8z"/></svg>
);
export const CameraIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M23 7l-7 5 7 5V7z"/><rect x="1" y="5" width="15" height="14" rx="2"/></svg>
);
export const CheckIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M20 6 9 17l-5-5"/></svg>
);
export const XIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M18 6 6 18M6 6l12 12"/></svg>
);
export const ChevronRightIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M9 18l6-6-6-6"/></svg>
);
export const ChevronLeftIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M15 18l-6-6 6-6"/></svg>
);
export const UsersIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75"/></svg>
);
export const MenuIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M4 6h16M4 12h16M4 18h16"/></svg>
);
export const PlusIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M12 5v14M5 12h14"/></svg>
);
export const InfoIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/></svg>
);
export const MeetingRoomIllustration = (p) => (
  <svg viewBox="0 0 120 80" {...base} {...p}>
    <rect x="8" y="6" width="104" height="68" rx="8" opacity="0.3" />
    <rect x="32" y="27" width="56" height="26" rx="5" />
    <circle cx="22" cy="40" r="5" />
    <circle cx="98" cy="40" r="5" />
    <circle cx="46" cy="16" r="5" />
    <circle cx="74" cy="16" r="5" />
    <circle cx="46" cy="64" r="5" />
    <circle cx="74" cy="64" r="5" />
  </svg>
);
export const ClockIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 3"/></svg>
);
export const ScanFaceIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M4 8V6a2 2 0 0 1 2-2h2M4 16v2a2 2 0 0 0 2 2h2M20 8V6a2 2 0 0 0-2-2h-2M20 16v2a2 2 0 0 1-2 2h-2M9 10v1M15 10v1M9 15c.7.7 1.7 1 3 1s2.3-.3 3-1"/></svg>
);
export const SettingsIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.6 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.6a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
);
export const TrashIcon = (p) => (
  <svg viewBox="0 0 24 24" {...base} {...p}><path d="M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2m3 0-.9 14a2 2 0 0 1-2 1.9H8.9a2 2 0 0 1-2-1.9L6 6"/></svg>
);
