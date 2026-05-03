import AppKit
import Synchronization

final class WindowTracker {
    static let shared = WindowTracker()

    private let windowCount = Mutex<Int>(0)

    func reportWindowOpened() {
        windowCount.withLock { currentWindowCount in
            currentWindowCount += 1
            updateActivationPolicy(currentWindowCount: currentWindowCount)
        }
    }

    func reportWindowClosed() {
        windowCount.withLock { currentWindowCount in
            if currentWindowCount > 0 {
                currentWindowCount -= 1
                updateActivationPolicy(currentWindowCount: currentWindowCount)
            }
        }
    }

    private func updateActivationPolicy(currentWindowCount: Int) {
        if currentWindowCount == 0 {
            NSApp.setActivationPolicy(.accessory)
            NSApp.deactivate()
        } else if currentWindowCount == 1 {
            NSApp.setActivationPolicy(.regular)
            NSApp.activate(ignoringOtherApps: true)
        }
    }
}
