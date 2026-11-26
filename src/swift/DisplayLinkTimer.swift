import AppKit
import FasterSwiper_Log
import FasterSwiper_RpcStatusProto
import Foundation
import QuartzCore

public typealias DisplayLinkTickCallback =
    @convention(c) (UnsafeMutableRawPointer?, Int64) -> Bool
public typealias DisplayLinkStoppedCallback =
    @convention(c) (UnsafeMutableRawPointer?, Bool) -> Void

private let kWatchdogGraceSeconds: Double = 0.500
private let kWatchdogCompletionTailNs: Int64 = 250_000_000
private let kDefaultFallbackMaxFps: Float = 120.0

private func saturatingAdd(_ lhs: Int64, _ rhs: Int64) -> Int64 {
    let (sum, overflow) = lhs.addingReportingOverflow(rhs)
    if overflow {
        return rhs >= 0 ? Int64.max : Int64.min
    }
    return sum
}

/// Persistent high-QoS background RunLoop dedicated to driving CADisplayLink
/// ticks and animation commits off the main CGEventTap thread.
private final class DisplayLinkRunner: @unchecked Sendable {
    static let shared = DisplayLinkRunner()

    private let ready = DispatchSemaphore(value: 0)
    private var runLoop: CFRunLoop?

    private init() {
        let thread = Thread { [self] in
            var ctx = CFRunLoopSourceContext()
            let keepAlive = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx)
            let loop = CFRunLoopGetCurrent()
            if let loop, let keepAlive {
                CFRunLoopAddSource(loop, keepAlive, .commonModes)
            }
            self.runLoop = loop
            self.ready.signal()
            CFRunLoopRun()
        }
        thread.name = "com.fasterswiper.DisplayLinkRunner"
        thread.qualityOfService = .userInteractive
        thread.start()
        ready.wait()
    }

    var isOnRunnerThread: Bool {
        guard let runLoop else { return false }
        return CFRunLoopGetCurrent() === runLoop
    }

    func post(_ block: @escaping @Sendable () -> Void) {
        guard let runLoop else { return }
        CFRunLoopPerformBlock(runLoop, CFRunLoopMode.commonModes.rawValue, block)
        CFRunLoopWakeUp(runLoop)
    }
}

@available(macOS 14.0, *)
private final class DisplayLinkTarget: NSObject {
    weak var session: DisplayLinkSession?

    init(session: DisplayLinkSession) {
        self.session = session
    }

    @objc func handleTick(_ link: CADisplayLink) {
        session?.handleTickOnWorkerThread(link)
    }
}

@available(macOS 14.0, *)
private final class DisplayLinkSession: @unchecked Sendable {
    private let durationNs: Int64
    private let fireImmediately: Bool
    private let onTick: DisplayLinkTickCallback
    private let onStopped: DisplayLinkStoppedCallback
    private let callbackCtx: UnsafeMutableRawPointer?
    private let maxFps: Float
    private let screenName: String

    // Created on the main thread; scheduled and invalidated exclusively on
    // DisplayLinkRunner.
    private var displayLink: CADisplayLink?
    private var target: DisplayLinkTarget?

    // Worker-thread-only state.
    private var finished = false
    private var watchdog: DispatchSourceTimer?
    private var activity: NSObjectProtocol?
    private var startMediaTime: CFTimeInterval = 0

    // Debug frame-pacing telemetry (worker-thread-only).
    private var tickCount = 0
    private var hasFirstVsync = false
    private var firstVsyncWallMs: Double = 0
    private var firstTsDeltaMs: Double = 0
    private var firstTargetLeadMs: Double = 0
    private var lastTickWall: Double = 0
    private var minDtMs = Double.infinity
    private var maxDtMs: Double = 0
    private var sumDtMs: Double = 0
    private var dtCount = 0
    private var watchdogFired = false

    // Cross-thread synchronization.
    private let stopLock = NSLock()
    private var stopRequested = false
    private var stopFinished = false
    private let stoppedSem = DispatchSemaphore(value: 0)

    @MainActor
    init?(
        displayID: CGDirectDisplayID,
        durationNs: Int64,
        fireImmediately: Bool,
        onTick: DisplayLinkTickCallback,
        onStopped: DisplayLinkStoppedCallback,
        callbackCtx: UnsafeMutableRawPointer?
    ) {
        guard let screen = Self.resolveScreen(displayID: displayID) else {
            return nil
        }

        self.durationNs = durationNs
        self.fireImmediately = fireImmediately
        self.onTick = onTick
        self.onStopped = onStopped
        self.callbackCtx = callbackCtx
        self.maxFps =
            screen.maximumFramesPerSecond > 0
            ? Float(screen.maximumFramesPerSecond)
            : kDefaultFallbackMaxFps
        self.screenName = screen.localizedName

        let target = DisplayLinkTarget(session: self)
        let link = screen.displayLink(
            target: target,
            selector: #selector(DisplayLinkTarget.handleTick(_:))
        )
        self.target = target
        self.displayLink = link

        DisplayLinkRunner.shared.post { [self] in
            self.startOnWorkerThread()
        }
    }

    @MainActor
    private static func displayID(for screen: NSScreen) -> CGDirectDisplayID? {
        (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?
            .uint32Value
    }

    private static func isDisplayAwake(_ displayID: CGDirectDisplayID) -> Bool {
        displayID != kCGNullDirectDisplay
            && CGDisplayIsActive(displayID) != 0
            && CGDisplayIsAsleep(displayID) == 0
    }

    @MainActor
    private static func resolveScreen(displayID: CGDirectDisplayID) -> NSScreen? {
        let screens = NSScreen.screens.filter { screen in
            guard let id = Self.displayID(for: screen) else { return false }
            return Self.isDisplayAwake(id)
        }
        guard !screens.isEmpty else { return nil }

        var matchedScreen: NSScreen?
        if displayID != kCGNullDirectDisplay {
            matchedScreen = screens.first { Self.displayID(for: $0) == displayID }
        }

        if matchedScreen == nil {
            let mouseLoc = NSEvent.mouseLocation
            matchedScreen = screens.first { NSMouseInRect(mouseLoc, $0.frame, false) }
        }

        if !NSScreen.screensHaveSeparateSpaces {
            // When all displays switch spaces together, pace against the highest
            // refresh rate display (preferring `matchedScreen` on ties).
            var best = matchedScreen ?? screens[0]
            for screen in screens where screen.maximumFramesPerSecond > best.maximumFramesPerSecond {
                best = screen
            }
            return best
        }

        if let matchedScreen {
            return matchedScreen
        }
        if let main = NSScreen.main,
           let mainID = Self.displayID(for: main),
           Self.isDisplayAwake(mainID)
        {
            return main
        }
        return screens[0]
    }

    private func claimStop(finished: Bool) -> Bool {
        stopLock.lock()
        defer { stopLock.unlock() }
        guard !stopRequested else { return false }
        stopRequested = true
        stopFinished = finished
        return true
    }

    private var isStopRequested: Bool {
        stopLock.lock()
        defer { stopLock.unlock() }
        return stopRequested
    }

    private func startOnWorkerThread() {
        guard !finished else { return }
        if isStopRequested {
            finishOnWorkerThread()
            return
        }

        activity = ProcessInfo.processInfo.beginActivity(
            options: [.userInitiatedAllowingIdleSystemSleep, .latencyCritical],
            reason: "FasterSwiper active swipe animation"
        )

        if maxFps > 0 {
            displayLink?.preferredFrameRateRange = CAFrameRateRange(
                minimum: maxFps,
                maximum: maxFps,
                preferred: maxFps
            )
        }

        startMediaTime = CACurrentMediaTime()

        if fireImmediately || durationNs <= 0 {
            tickCount += 1
            lastTickWall = startMediaTime
            if !onTick(callbackCtx, 0) {
                _ = claimStop(finished: true)
                finishOnWorkerThread()
                return
            }
        }

        displayLink?.add(to: .current, forMode: .common)

        let watchdogDelay = max(0, Double(durationNs) * 1e-9) + kWatchdogGraceSeconds
        let timer = DispatchSource.makeTimerSource(queue: .global(qos: .userInteractive))
        timer.schedule(deadline: .now() + watchdogDelay, leeway: .milliseconds(50))
        timer.setEventHandler { [weak self] in
            guard let session = self else { return }
            DisplayLinkRunner.shared.post { [weak session] in
                session?.handleWatchdogOnWorkerThread()
            }
        }
        timer.resume()
        watchdog = timer
    }

    func handleTickOnWorkerThread(_ link: CADisplayLink) {
        guard !finished else { return }
        if isStopRequested {
            finishOnWorkerThread()
            return
        }

        let now = CACurrentMediaTime()
        if !hasFirstVsync {
            hasFirstVsync = true
            firstVsyncWallMs = (now - startMediaTime) * 1000.0
            firstTsDeltaMs = (link.timestamp - startMediaTime) * 1000.0
            firstTargetLeadMs = (link.targetTimestamp - link.timestamp) * 1000.0
        } else {
            let dtMs = (now - lastTickWall) * 1000.0
            minDtMs = min(minDtMs, dtMs)
            maxDtMs = max(maxDtMs, dtMs)
            sumDtMs += dtMs
            dtCount += 1
        }
        lastTickWall = now
        tickCount += 1

        let elapsedNs = Int64((max(0, link.timestamp - startMediaTime) * 1e9).rounded())
        if !onTick(callbackCtx, elapsedNs) {
            _ = claimStop(finished: true)
            finishOnWorkerThread()
        }
    }

    private func handleWatchdogOnWorkerThread() {
        guard !finished else { return }
        if isStopRequested {
            finishOnWorkerThread()
            return
        }

        let elapsedNs = Int64((max(0, CACurrentMediaTime() - startMediaTime) * 1e9).rounded())
        let completionNs = max(
            elapsedNs,
            saturatingAdd(max(0, durationNs), kWatchdogCompletionTailNs)
        )

        watchdogFired = true
        Log.warning(
            "CADisplayLink watchdog fired for screen \"\(self.screenName)\" after \(self.tickCount) ticks; forcing completion."
        )
        tickCount += 1
        _ = onTick(callbackCtx, completionNs)
        _ = claimStop(finished: true)
        finishOnWorkerThread()
    }

    private func finishOnWorkerThread() {
        guard !finished else { return }
        finished = true

        if let watchdog {
            watchdog.cancel()
            self.watchdog = nil
        }

        displayLink?.invalidate()
        displayLink = nil
        target = nil

        stopLock.lock()
        let didFinish = stopFinished
        stopLock.unlock()

        let totalMs = startMediaTime > 0 ? (CACurrentMediaTime() - startMediaTime) * 1000.0 : 0.0
        onStopped(callbackCtx, didFinish)

        if let activity {
            ProcessInfo.processInfo.endActivity(activity)
            self.activity = nil
        }

        if Log.vlogIsOn(1) {
            let avgDtMs = dtCount > 0 ? (sumDtMs / Double(dtCount)) : 0.0
            let effHz = avgDtMs > 0 ? (1000.0 / avgDtMs) : 0.0
            let loggedMinDt = dtCount > 0 ? minDtMs : 0.0
            let reason = didFinish ? "kFinished" : "kCancelled"
            let watchdogSuffix = watchdogFired ? ", WATCHDOG" : ""
            Log.vlog(
                1,
                String(
                    format:
                        "CADisplayLink[%@, max=%.0fHz]: %d ticks in %.1fms (first_wall=%.2fms, first_ts=%.2fms, lead=%.2fms, dt avg=%.2fms min=%.2fms max=%.2fms ~%.1fHz, reason=%@%@)",
                    self.screenName,
                    self.maxFps,
                    self.tickCount,
                    totalMs,
                    self.firstVsyncWallMs,
                    self.firstTsDeltaMs,
                    self.firstTargetLeadMs,
                    avgDtMs,
                    loggedMinDt,
                    self.maxDtMs,
                    effHz,
                    reason,
                    watchdogSuffix
                )
            )
        }

        stoppedSem.signal()
    }

    func cancelAndWait() {
        precondition(
            !DisplayLinkRunner.shared.isOnRunnerThread,
            "DisplayLinkTimer::Cancel() must not be called from runner thread"
        )

        if claimStop(finished: false) {
            DisplayLinkRunner.shared.post { [self] in
                self.finishOnWorkerThread()
            }
        }

        stoppedSem.wait()
    }
}

private func writeErrorStatus(
    _ code: Google_Rpc_Code,
    _ message: String,
    to outErrorStatus: UnsafeMutablePointer<UnsafeRawPointer?>?
) {
    guard let outErrorStatus else { return }
    var status = Google_Rpc_Status()
    status.code = Int32(code.rawValue)
    status.message = message
    guard let data = try? status.serializedData() else { return }
    outErrorStatus.pointee = UnsafeRawPointer(
        Unmanaged.passRetained(data as CFData).toOpaque()
    )
}

@_cdecl("fs_display_link_start")
public func fsDisplayLinkStart(
    _ displayID: UInt32,
    _ durationNs: Int64,
    _ fireImmediately: Bool,
    _ onTick: DisplayLinkTickCallback?,
    _ onStopped: DisplayLinkStoppedCallback?,
    _ callbackCtx: UnsafeMutableRawPointer?,
    _ outErrorStatus: UnsafeMutablePointer<UnsafeRawPointer?>?
) -> UnsafeMutableRawPointer? {
    outErrorStatus?.pointee = nil

    guard #available(macOS 14.0, *) else {
        writeErrorStatus(
            .unimplemented,
            "CADisplayLink requires macOS 14.0 or later",
            to: outErrorStatus
        )
        return nil
    }
    guard Thread.isMainThread else {
        writeErrorStatus(
            .failedPrecondition,
            "CreateDisplayLinkTimer must be called on the main thread",
            to: outErrorStatus
        )
        return nil
    }
    guard let onTick, let onStopped else {
        writeErrorStatus(
            .invalidArgument,
            "fs_display_link_start called with null callback",
            to: outErrorStatus
        )
        return nil
    }

    nonisolated(unsafe) let ctx = callbackCtx
    guard let session = MainActor.assumeIsolated({
        DisplayLinkSession(
            displayID: displayID,
            durationNs: durationNs,
            fireImmediately: fireImmediately,
            onTick: onTick,
            onStopped: onStopped,
            callbackCtx: ctx
        )
    }) else {
        writeErrorStatus(
            .unavailable,
            "No active awake NSScreen available for CADisplayLink",
            to: outErrorStatus
        )
        return nil
    }
    return Unmanaged.passRetained(session).toOpaque()
}

@_cdecl("fs_display_link_cancel_and_destroy")
public func fsDisplayLinkCancelAndDestroy(
    _ rawHandle: UnsafeMutableRawPointer?
) {
    guard #available(macOS 14.0, *), let rawHandle else { return }
    let session = Unmanaged<DisplayLinkSession>.fromOpaque(rawHandle).takeRetainedValue()
    session.cancelAndWait()
}
