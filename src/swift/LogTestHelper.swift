import FasterSwiper_Log

@_cdecl("fs_swift_log_test_emit_severities")
public func fsSwiftLogTestEmitSeverities() {
    Log.info("swift info message")
    Log.warning("swift warning message")
    Log.error("swift error message")
}

@_cdecl("fs_swift_log_test_vlog")
public func fsSwiftLogTestVlog(_ level: Int32, _ evalCount: UnsafeMutablePointer<Int32>) {
    Log.vlog(level, {
        evalCount.pointee += 1
        return "swift vlog level \(level)"
    }())
}

@_cdecl("fs_swift_log_test_vlog_is_on")
public func fsSwiftLogTestVlogIsOn(_ level: Int32) -> Bool {
    return Log.vlogIsOn(level)
}

@_cdecl("fs_swift_log_test_fatal")
public func fsSwiftLogTestFatal() {
    Log.fatal("swift fatal message")
}
