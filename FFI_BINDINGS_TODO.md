# FFI Bindings Generation Required

The Dart API has been updated with the new TMUX-style buffer methods:
- `writeBuffered()`
- `setDiscardNotificationCallback()`
- `getDiscardedBytes()`
- `clearDiscardedBytes()`

However, the FFI bindings in `lib/src/flutter_pty_bindings_generated.dart` need to be regenerated.

## To regenerate FFI bindings:

1. Install LLVM (required by ffigen):
   - Windows: Download from https://github.com/llvm/llvm-project/releases
   - Or use: `choco install llvm`
   - Or use: `scoop install llvm`

2. Run ffigen:
   ```bash
   cd C:/workspace/VL/PubDev/flutter_pty
   dart run ffigen --config ffigen.yaml
   ```

3. Verify the generated bindings include:
   - `pty_write_buffered`
   - `pty_set_discard_notification_port`
   - `pty_get_discarded_bytes`
   - `pty_clear_discarded_bytes`

## Temporary Workaround

Until the bindings are regenerated, you can manually add the function signatures to
`lib/src/flutter_pty_bindings_generated.dart` if needed for testing.