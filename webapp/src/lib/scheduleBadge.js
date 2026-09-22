// scheduleBadge.js — nhãn trạng thái dùng chung cho các danh sách lịch xem
// theo NGÀY BẤT KỲ (Book Room, Room Schedule). Không phân biệt "Next" như
// RoomHome vì "sắp diễn ra gần nhất" chỉ có ý nghĩa khi xem đúng hôm nay —
// RoomHome tự tính badge riêng cho nhu cầu đó.
export function scheduleStatusBadge(status) {
  if (status === 'checked_in') return { cls: 'now', label: 'In Progress' };
  if (status === 'completed') return { cls: 'completed', label: 'Completed' };
  if (status === 'no_show') return { cls: 'cancelled', label: 'No-show' };
  if (status === 'cancelled') return { cls: 'cancelled', label: 'Cancelled' };
  return { cls: 'upcoming', label: 'Scheduled' };
}
