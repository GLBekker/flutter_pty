# TMUX-Style Buffer Management Integration

This document describes the TMUX-style buffer management implementation added to flutter_pty.

## Phase 1-4: C Implementation (COMPLETED)

### Files Modified:
1. **src/flutter_pty.h** - Added buffer structures and function declarations
2. **src/flutter_pty_unix.c** - Unix/Linux implementation with:
   - Circular buffer operations
   - Platform-specific write detection using select()
   - Timer thread for periodic buffer flushing
   - Main buffered write function
3. **src/flutter_pty_win.c** - Windows implementation with:
   - Circular buffer operations
   - Platform-specific write detection using PeekNamedPipe
   - Timer queue for periodic buffer flushing
   - Main buffered write function

### Key Features Implemented:
- **Dynamic thresholds**: Based on terminal size (rows × cols)
  - Block start: `1 + (rows × cols × 8)`
  - Block stop: `1 + (rows × cols / 8)`
- **Circular buffer**: Efficient data management with automatic growth
- **Timer-based flushing**: 100ms intervals to retry blocked writes
- **Discard on overflow**: TMUX-style behavior when buffer grows too large
- **Platform-specific optimizations**: select() on Unix, overlapped I/O on Windows

## Phase 5: Dart API Updates (IN PROGRESS)

### Manual Integration Required:

1. **Add new methods to lib/flutter_pty.dart** (see lib/tmux_buffer_additions.dart):
   - `writeBuffered()` - Main TMUX-style buffered write method
   - `setDiscardNotificationCallback()` - Register for discard notifications
   - `getDiscardedBytes()` - Check how many bytes were discarded
   - `clearDiscardedBytes()` - Reset the discard counter
   - Add `ReceivePort? _discardPort;` field
   - Update `_onExitCode()` to close the discard port

2. **Regenerate FFI bindings**:
   ```bash
   # Run from the flutter_pty directory
   dart run ffigen
   ```

   This will regenerate `lib/src/flutter_pty_bindings_generated.dart` to include:
   - `pty_write_buffered`
   - `pty_set_discard_notification_port`
   - `pty_get_discarded_bytes`
   - `pty_clear_discarded_bytes`

## Phase 6: Echorb App Integration

After the Dart API is updated, integrate into Echorb:

1. **Update StandaloneTerminal** to use `writeBuffered()` instead of `writeAsync()`
2. **Add discard notification handling** to monitor when data is lost
3. **Test with rapid input** to verify buffering works correctly

## Usage Example

```dart
// Create PTY with buffer management
final pty = Pty.start(
  'bash',
  rows: 24,
  columns: 80,
);

// Set up discard notifications
pty.setDiscardNotificationCallback((bytesDiscarded) {
  print('Warning: $bytesDiscarded bytes were discarded due to buffer overflow');
});

// Use buffered write for all terminal input
void sendCommand(String command) {
  final data = utf8.encode(command);
  pty.writeBuffered(Uint8List.fromList(data));
}
```

## Testing

### Unix/Linux Testing:
```bash
# Build the native library
cd src
gcc -shared -fPIC flutter_pty_unix.c forkpty.c -o libflutter_pty.so -lpthread
```

### Windows Testing:
```bash
# Build using Visual Studio or MinGW
cl /LD flutter_pty_win.c /Fe:flutter_pty.dll
```

### Stress Test:
```dart
// Rapid write test to trigger buffering
for (int i = 0; i < 1000; i++) {
  pty.writeBuffered(utf8.encode('echo "Line $i"\n'));
}

// Check if any data was discarded
final discarded = pty.getDiscardedBytes();
if (discarded > 0) {
  print('Discarded $discarded bytes during stress test');
  pty.clearDiscardedBytes();
}
```

## Benefits

1. **No more thread overflow errors** - Proper backpressure handling
2. **Automatic flow control** - Buffers when PTY is busy
3. **Graceful degradation** - Discards data when overwhelmed (like TMUX)
4. **Performance optimized** - Uses circular buffers and platform-specific APIs
5. **Dynamic adaptation** - Thresholds adjust with terminal size

## Next Steps

1. ✅ Complete C implementation (Phases 1-4)
2. ⏳ Update Dart API (Phase 5) - Manual integration needed
3. ⏳ Regenerate FFI bindings
4. ⏳ Integrate into Echorb app (Phase 6)
5. ⏳ Test on both platforms
6. ⏳ Push to GitHub fork