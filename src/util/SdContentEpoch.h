#pragma once

#include <atomic>
#include <cstdint>

// Bumped whenever the SD card's files may have changed while the firmware kept
// running (USB plugged or unplugged, USB Drive or web file transfer finished),
// so caches built from directory scans know to rescan. A reboot (which is also
// when a new card is mounted) starts every such cache empty.
namespace SdContentEpoch {
inline std::atomic<uint32_t> value{0};
inline void markChanged() { value.fetch_add(1, std::memory_order_relaxed); }
inline uint32_t current() { return value.load(std::memory_order_relaxed); }
}  // namespace SdContentEpoch
