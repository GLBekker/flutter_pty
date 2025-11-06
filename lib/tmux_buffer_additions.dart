// New methods to add to the Pty class in flutter_pty.dart
// Add these methods before the _onExitCode method:

  /// Write using TMUX-style buffered approach (Echorb enhancement)
  /// This method handles backpressure automatically by buffering data
  /// when the PTY is not ready, and discarding data if the buffer grows too large
  void writeBuffered(Uint8List data) {
    final buf = malloc<Int8>(data.length);
    try {
      buf.asTypedList(data.length).setAll(0, data);
      _bindings.pty_write_buffered(_handle, buf.cast(), data.length);
    } finally {
      malloc.free(buf);
    }
  }

  /// Set up port for receiving discard notifications (Echorb enhancement)
  void setDiscardNotificationCallback(void Function(int bytesDiscarded) callback) {
    _discardPort = ReceivePort();
    _discardPort!.listen((dynamic message) {
      if (message is int) {
        callback(message);
      }
    });
    _bindings.pty_set_discard_notification_port(
      _handle,
      _discardPort!.sendPort.nativePort,
    );
  }

  /// Get number of bytes discarded since last clear (Echorb enhancement)
  int getDiscardedBytes() {
    return _bindings.pty_get_discarded_bytes(_handle);
  }

  /// Clear the discarded bytes counter (Echorb enhancement)
  void clearDiscardedBytes() {
    _bindings.pty_clear_discarded_bytes(_handle);
  }

  // Add this field after the other fields (like _handle):
  ReceivePort? _discardPort;

// IMPORTANT: Also update the _onExitCode method to close the _discardPort:
// Replace the existing _onExitCode method with:

  void _onExitCode(dynamic exitCode) {
    _stdoutPort.close();
    _exitPort.close();
    _discardPort?.close();
    _exitCodeCompleter.complete(exitCode);
  }