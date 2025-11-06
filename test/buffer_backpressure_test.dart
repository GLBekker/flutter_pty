import 'dart:io';
import 'dart:typed_data';
import 'package:flutter_pty/flutter_pty.dart';
import 'package:flutter_test/flutter_test.dart';

String get shell {
  if (Platform.isWindows) {
    return 'cmd.exe';
  }
  return 'bash';
}

void main() {
  group('Buffer Status', () {
    test('getBufferStatus returns valid data', () {
      final pty = Pty.start(shell);
      final status = pty.getBufferStatus();

      expect(status.capacity, greaterThan(0));
      expect(status.currentSize, greaterThanOrEqualTo(0));
      expect(status.canWrite, isA<bool>());

      pty.kill();
    });

    test('canWrite returns boolean', () {
      final pty = Pty.start(shell);
      expect(pty.canWrite(), isA<bool>());
      pty.kill();
    });
  });

  group('Async Write', () {
    test('writeAsync completes successfully with small command', () async {
      final pty = Pty.start(shell);
      final data = Uint8List.fromList('echo test\n'.codeUnits);

      await expectLater(pty.writeAsync(data), completes);

      pty.kill();
    });

    test('writeAsync handles large command without chunking', () async {
      final pty = Pty.start(shell);
      // Create 2KB command (would typically need chunking)
      final largeCmd = 'echo ' + ('x' * 2000) + '\n';
      final data = Uint8List.fromList(largeCmd.codeUnits);

      await expectLater(pty.writeAsync(data), completes);

      pty.kill();
    });
  });
}
