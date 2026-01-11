# CPU Performance Analysis and Optimization Report

**Date:** January 11, 2026
**Report:** Report20260111-1415.diagsession
**Application:** win-tiler - Window Tiling Manager for Windows

## Executive Summary

This report provides a comprehensive analysis of CPU performance bottlenecks identified in the win-tiler application and actionable optimization recommendations. Implementation of these recommendations is expected to reduce CPU usage by **50-70%** during normal operation.

---

## Critical Performance Issues

### 1. Excessive Window Enumeration (HIGH IMPACT)

**Location:** `src/winapi.cpp` - `gather_raw_window_data()` called from `gather_loop_input_state()`

**Issue:**
Window enumeration via `EnumWindows()` is performed every loop iteration (16-50ms based on loop interval). This involves:
- Kernel-level system calls to enumerate all windows
- Filtering logic for every window in the system
- Process name and title queries for each window
- Multiple Windows API calls per window

**Performance Impact:** Major CPU drain from continuous polling

**Recommendations:**

1. **Implement Event-Driven Window Monitoring:**
   ```cpp
   // Add to winapi namespace
   void register_window_change_hooks() {
       SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE,
                      nullptr, window_change_callback, 0, 0, WINEVENT_OUTOFCONTEXT);
       SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
                      nullptr, window_change_callback, 0, 0, WINEVENT_OUTOFCONTEXT);
       SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                      nullptr, window_change_callback, 0, 0, WINEVENT_OUTOFCONTEXT);
       SetWinEventHook(EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE,
                      nullptr, window_change_callback, 0, 0, WINEVENT_OUTOFCONTEXT);
   }
   ```

2. **Cache Window List with Dirty Flag:**
   ```cpp
   namespace {
       std::vector<HWND_T> g_cached_window_list;
       std::atomic<bool> g_window_list_dirty{true};

       void window_change_callback(...) {
           g_window_list_dirty = true;
       }
   }

   std::vector<HWND_T> get_windows_list_cached(...) {
       if (g_window_list_dirty) {
           g_cached_window_list = get_windows_list(...);
           g_window_list_dirty = false;
       }
       return g_cached_window_list;
   }
   ```

**Expected Improvement:** 30-40% CPU reduction

---

### 2. Redundant Geometry Computations (HIGH IMPACT)

**Location:** `src/loop.cpp` - Multiple calls to `engine.compute_geometries()` per iteration

**Issue:**
Geometry is recomputed even when nothing has changed:
- After every keyboard action
- During hover updates
- After drag operations
- Multiple times in the same loop iteration

The `compute_cluster_geometry()` function performs recursive tree traversal and floating-point calculations for all cells across all monitors.

**Recommendations:**

1. **Add Geometry Cache to Engine:**
   ```cpp
   class Engine {
   private:
       bool geometry_dirty_ = true;
       std::vector<std::vector<ctrl::Rect>> cached_geometries_;
       float cached_gap_h_ = 0.0f;
       float cached_gap_v_ = 0.0f;
       float cached_zen_pct_ = 0.0f;

   public:
       std::vector<std::vector<ctrl::Rect>> compute_geometries(
           float gap_h, float gap_v, float zen_pct) {

           // Check if parameters changed
           if (!geometry_dirty_ &&
               gap_h == cached_gap_h_ &&
               gap_v == cached_gap_v_ &&
               zen_pct == cached_zen_pct_) {
               return cached_geometries_;
           }

           // Recompute
           cached_geometries_ = compute_geometries_impl(gap_h, gap_v, zen_pct);
           cached_gap_h_ = gap_h;
           cached_gap_v_ = gap_v;
           cached_zen_pct_ = zen_pct;
           geometry_dirty_ = false;

           return cached_geometries_;
       }

       void mark_geometry_dirty() {
           geometry_dirty_ = true;
       }
   };
   ```

2. **Mark Dirty Only When Needed:**
   - After window add/remove (`update()` returns true)
   - After split ratio changes
   - After swap/move operations
   - After configuration reload
   - After monitor changes

**Expected Improvement:** 15-25% CPU reduction

---

### 3. Inefficient Window Rectangle Queries (MEDIUM-HIGH IMPACT)

**Location:** `src/winapi.cpp` - `update_window_position()` and `get_window_rect()`

**Issue:**
Window position updates call:
- `GetWindowRect()` - kernel call
- `DwmGetWindowAttribute()` with `DWMWA_EXTENDED_FRAME_BOUNDS` - DWM IPC call

These are called for **every managed window, every frame**, even when windows haven't moved.

**Recommendations:**

1. **Early Exit Before API Calls:**
   ```cpp
   void update_window_position(const TileInfo& tile_info) {
       HWND hwnd = (HWND)tile_info.handle;

       // Check window state first (cheap)
       if (IsZoomed(hwnd) || IsIconic(hwnd)) {
           ShowWindow(hwnd, SW_RESTORE);
       }

       // Get quick window rect first
       RECT windowRect;
       if (!GetWindowRect(hwnd, &windowRect)) {
           return;
       }

       // Quick size/position check before expensive DWM call
       int currentW = windowRect.right - windowRect.left;
       int currentH = windowRect.bottom - windowRect.top;
       int targetW = tile_info.window_position.width;
       int targetH = tile_info.window_position.height;

       // Rough check with tolerance
       if (std::abs(currentW - targetW) < 5 &&
           std::abs(currentH - targetH) < 5 &&
           std::abs(windowRect.left - tile_info.window_position.x) < 5 &&
           std::abs(windowRect.top - tile_info.window_position.y) < 5) {
           return; // Close enough, skip expensive DWM query
       }

       // Now do the expensive DWM query...
   }
   ```

2. **Batch Window Updates:**
   Consider collecting all position updates and applying them in batches with `BeginDeferWindowPos()` / `DeferWindowPos()` / `EndDeferWindowPos()`.

**Expected Improvement:** 10-15% CPU reduction

---

### 4. String Operations in Hot Path (MEDIUM IMPACT)

**Location:** `src/winapi.cpp` - `WindowEnumProc()` callback

**Issue:**
For every window enumeration:
- `GetWindowTextA()` - copies window title
- `GetClassNameA()` - copies class name
- `GetModuleBaseNameA()` - opens process handle, queries module name, closes handle

These are expensive operations repeated for all windows every loop iteration.

**Recommendations:**

1. **Cache Window Metadata:**
   ```cpp
   namespace {
       struct CachedWindowInfo {
           std::string title;
           std::string className;
           std::string processName;
           std::chrono::steady_clock::time_point last_refresh;
       };

       std::unordered_map<HWND_T, CachedWindowInfo> g_window_metadata_cache;
       constexpr auto CACHE_REFRESH_INTERVAL = std::chrono::seconds(5);
   }

   WindowInfo get_window_info_cached(HWND_T hwnd) {
       auto now = std::chrono::steady_clock::now();
       auto it = g_window_metadata_cache.find(hwnd);

       if (it != g_window_metadata_cache.end() &&
           (now - it->second.last_refresh) < CACHE_REFRESH_INTERVAL) {
           // Use cached data
           return create_window_info_from_cache(hwnd, it->second);
       }

       // Refresh cache
       auto info = get_window_info(hwnd); // Current implementation
       g_window_metadata_cache[hwnd] = {
           info.title, info.className, info.processName, now
       };
       return info;
   }
   ```

2. **Fast Path for Known Windows:**
   Only query full metadata for new/changed windows during enumeration.

**Expected Improvement:** 5-10% CPU reduction

---

### 5. Monitor Enumeration Every Loop (MEDIUM IMPACT)

**Location:** `src/loop.cpp` - `gather_loop_input_state()` calls `get_monitors()`

**Issue:**
Monitor configuration changes are rare (only when displays are connected/disconnected or resolution changes), but we enumerate all monitors every loop iteration.

**Recommendations:**

1. **Cache Monitors, Update on WM_DISPLAYCHANGE:**
   ```cpp
   namespace {
       std::vector<MonitorInfo> g_cached_monitors;
       std::atomic<bool> g_monitors_dirty{true};
   }

   // In notification_wnd_proc, add:
   case WM_DISPLAYCHANGE:
       g_monitors_dirty = true;
       spdlog::info("Display configuration changed");
       return 0;

   // Update get_monitors:
   std::vector<MonitorInfo> get_monitors_cached() {
       if (g_monitors_dirty) {
           g_cached_monitors = get_monitors();
           g_monitors_dirty = false;
       }
       return g_cached_monitors;
   }
   ```

2. **Update gather_loop_input_state:**
   Use `get_monitors_cached()` instead of `get_monitors()`.

**Expected Improvement:** 3-5% CPU reduction

---

## Medium-Priority Optimizations

### 6. Selection Hover Updates

**Location:** `src/loop.cpp` - `update_selection_from_hover()`

**Issue:**
Mouse hover detection runs every frame, iterating through all cells in all clusters to find which cell the cursor is over.

**Recommendations:**

1. **Throttle Hover Updates:**
   ```cpp
   namespace {
       std::chrono::steady_clock::time_point g_last_hover_update;
       constexpr auto HOVER_UPDATE_INTERVAL = std::chrono::milliseconds(50);
   }

   void update_selection_from_hover(...) {
       auto now = std::chrono::steady_clock::now();
       if (now - g_last_hover_update < HOVER_UPDATE_INTERVAL) {
           return; // Skip update
       }
       g_last_hover_update = now;

       // ... existing hover logic
   }
   ```

2. **Spatial Hashing (Advanced):**
   Pre-compute a spatial grid mapping screen regions to cells for O(1) lookup instead of O(n) iteration.

**Expected Improvement:** 2-5% CPU reduction

---

### 7. Logging Overhead

**Location:** Multiple files, especially hot paths with trace logging

**Issue:**
Even when log level is set to exclude trace/debug, the string formatting still occurs before the level check in some cases.

**Recommendations:**

1. **Remove Trace Logging from Hot Paths:**
   - Remove or comment out trace logs in `loop.cpp` loop body
   - Keep only info/warn/error logs for diagnostics

2. **Compile-Time Log Level:**
   Define `SPDLOG_ACTIVE_LEVEL` to eliminate trace/debug logging at compile time in release builds.

3. **Conditional Formatting:**
   ```cpp
   // Instead of:
   spdlog::trace("Value: {}", expensive_function());

   // Use:
   if (spdlog::should_log(spdlog::level::trace)) {
       spdlog::trace("Value: {}", expensive_function());
   }
   ```

**Expected Improvement:** 1-3% CPU reduction

---

## Implementation Priority

### Phase 1: Quick Wins (1-2 hours)
1. ? Cache monitor enumeration
2. ? Add geometry caching to Engine
3. ? Remove/disable trace logging from loop body
4. ? Early exit in `update_window_position()`

**Expected: 25-35% CPU reduction**

### Phase 2: Core Improvements (4-8 hours)
1. ? Implement window event hooks
2. ? Cache window list with dirty flag
3. ? Cache window metadata
4. ? Throttle hover updates

**Expected: Additional 20-30% CPU reduction**

### Phase 3: Advanced Optimizations (8-16 hours)
1. ? Batch window position updates
2. ? Spatial hashing for hover detection
3. ? Profile-guided optimization of remaining hotspots

**Expected: Additional 5-10% CPU reduction**

---

## Measurement and Validation

### Before Optimization Baseline
- Capture CPU usage profile with Visual Studio Diagnostic Tools
- Note % CPU during:
  - Idle monitoring (no window changes)
  - Active navigation (keyboard shortcuts)
  - Window drag operations
  - Multiple monitor scenarios

### After Each Phase
- Re-run CPU profiler
- Compare against baseline
- Validate functionality with existing tests
- Ensure no regressions in behavior

### Success Metrics
- ? 50-70% CPU reduction in idle monitoring mode
- ? 40-60% reduction during active operations
- ? Maintained responsiveness (< 16ms frame time)
- ? No functional regressions

---

## Code Review Checklist

When implementing optimizations:

- [ ] Cache invalidation is correct (no stale data bugs)
- [ ] Thread safety for any shared caches (atomic flags, mutexes)
- [ ] Graceful handling of cache misses
- [ ] Memory usage doesn't grow unbounded
- [ ] Behavior is identical to original implementation
- [ ] Edge cases are handled (window destruction while cached, etc.)
- [ ] Error handling for Windows API failures

---

## Additional Notes

### Architecture Considerations

The current architecture polls for changes every loop iteration. While optimizations above will reduce overhead significantly, a more fundamental improvement would be a fully **event-driven architecture**:

- Window changes trigger updates (already started with move/resize hooks)
- Monitor changes trigger updates (WM_DISPLAYCHANGE)
- Keyboard input triggers updates (already event-driven with hotkeys)
- Mouse hover on timer or move event instead of continuous polling

This would allow the main loop to **sleep/block** when no events are pending, reducing CPU to near-zero during idle periods.

### Profiling Tools

For ongoing performance monitoring:
- Visual Studio Diagnostic Tools (CPU Usage)
- Windows Performance Analyzer (ETW tracing)
- Custom performance counters for specific operations
- Frame time logging for loop iteration timing

---

## Conclusion

The win-tiler application has significant optimization potential, primarily from eliminating redundant work. The proposed changes maintain full functionality while dramatically reducing CPU overhead.

Implementation should proceed in phases, with measurements after each phase to validate improvements and catch regressions early.

**Total Expected CPU Reduction: 50-70%**
