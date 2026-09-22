import { useState } from 'react';

// Combobox tìm theo tên — dùng chung cho: chọn organizer, thêm attendee, và
// mô phỏng "người được nhận diện" lúc check-in (đứng tạm cho camera thật).
export default function PersonPicker({ people, value, onSelect, placeholder = 'Search name…', excludeIds = [] }) {
  const [query, setQuery] = useState('');
  const [open, setOpen] = useState(false);

  const filtered = people.filter(
    (p) => !excludeIds.includes(p.id) && p.name.toLowerCase().includes(query.toLowerCase())
  );

  return (
    <div className="person-picker">
      <input
        className="input"
        placeholder={value ? value.name : placeholder}
        value={query}
        onFocus={() => setOpen(true)}
        onChange={(e) => { setQuery(e.target.value); setOpen(true); }}
        onBlur={() => setTimeout(() => setOpen(false), 120)}
      />
      {open && (
        <div className="person-picker-list">
          {filtered.length === 0 && <div className="person-picker-empty">No match</div>}
          {filtered.map((p) => (
            <div
              key={p.id}
              className="person-picker-item"
              onMouseDown={() => { onSelect(p); setQuery(''); setOpen(false); }}
            >
              <span className="avatar sm">{p.initials}</span>
              {p.name}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
