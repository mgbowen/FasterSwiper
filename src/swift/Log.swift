import FasterSwiper_LogC
import FasterSwiper_LogProto

/// Abseil-backed logging facade for Swift code.
///
/// Messages flow through the same Abseil log sinks and formatting as C++
/// `LOG(...)` and `VLOG(...)`, and honor `--v` / `--vmodule` flags (matching
/// Swift filenames without the `.swift` suffix, e.g. `--vmodule=DisplayLinkTimer=1`).
public enum Log {
    private typealias Severity = Fasterswiper_Swift_LogSeverity

    /// Returns whether verbose logging is enabled at `level` for `file`.
    ///
    /// Note: `FS_VLogIsOn` acquires an internal Abseil mutex on every call, so
    /// callers should avoid `Log.vlogIsOn` and `Log.vlog` in tight per-event
    /// loops.
    public static func vlogIsOn(
        _ level: Int32,
        file: StaticString = #filePath
    ) -> Bool {
        file.withUTF8Buffer { fileBuf in
            FS_VLogIsOn(level, fileBuf.baseAddress, fileBuf.count)
        }
    }

    /// Emits a verbose log message when `level` is enabled for `file`.
    ///
    /// Note: Checks verbosity via `FS_VLogIsOn`, which acquires an internal
    /// Abseil mutex per call; avoid calling `Log.vlog` in tight per-event loops.
    public static func vlog(
        _ level: Int32,
        _ message: @autoclosure () -> String,
        file: StaticString = #filePath,
        line: Int32 = #line
    ) {
        guard vlogIsOn(level, file: file) else { return }
        emit(
            severity: .info,
            verbosity: level,
            message: message(),
            file: file,
            line: line
        )
    }

    public static func info(
        _ message: @autoclosure () -> String,
        file: StaticString = #filePath,
        line: Int32 = #line
    ) {
        emit(
            severity: .info,
            verbosity: -1,
            message: message(),
            file: file,
            line: line
        )
    }

    public static func warning(
        _ message: @autoclosure () -> String,
        file: StaticString = #filePath,
        line: Int32 = #line
    ) {
        emit(
            severity: .warning,
            verbosity: -1,
            message: message(),
            file: file,
            line: line
        )
    }

    public static func error(
        _ message: @autoclosure () -> String,
        file: StaticString = #filePath,
        line: Int32 = #line
    ) {
        emit(
            severity: .error,
            verbosity: -1,
            message: message(),
            file: file,
            line: line
        )
    }

    public static func fatal(
        _ message: @autoclosure () -> String,
        file: StaticString = #filePath,
        line: Int32 = #line
    ) -> Never {
        let rendered = message()
        emit(
            severity: .fatal,
            verbosity: -1,
            message: rendered,
            file: file,
            line: line
        )
        // Parentheses silence Swift's warning about forwarding a `#filePath`
        // default argument to `fatalError`'s `#file` parameter.
        fatalError(rendered, file: (file), line: UInt(max(0, line)))
    }

    private static func emit(
        severity: Severity,
        verbosity: Int32,
        message: String,
        file: StaticString,
        line: Int32
    ) {
        var mutableMessage = message
        file.withUTF8Buffer { fileBuf in
            mutableMessage.withUTF8 { msgBuf in
                FS_LogMessage(
                    Int32(severity.rawValue),
                    verbosity,
                    fileBuf.baseAddress,
                    fileBuf.count,
                    line,
                    msgBuf.baseAddress,
                    msgBuf.count
                )
            }
        }
    }
}
