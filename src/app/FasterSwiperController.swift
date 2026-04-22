import Foundation
import CoreGraphics
import Cocoa
import SwiftUI
import src_c_api

class FasterSwiperController: ObservableObject {
    enum Status {
        case started
        case permissionsRequired
        case failedToStart
    }

    enum EasingType: Int, CaseIterable, Identifiable {
        case linear = 0
        case easeOutQuadratic = 1
        case easeOutQuintic = 2

        var id: Int { self.rawValue }
        var name: String {
            switch self {
            case .linear: return "Linear"
            case .easeOutQuadratic: return "Ease Out Quadratic"
            case .easeOutQuintic: return "Ease Out Quintic"
            }
        }
        var cValue: EasingFunctionType {
            switch self {
            case .linear: return kEasingFunctionLinear
            case .easeOutQuadratic: return kEasingFunctionEaseOutQuadratic
            case .easeOutQuintic: return kEasingFunctionEaseOutQuintic
            }
        }
    }
    
    @Published var status: Status = .permissionsRequired
    
    // AppStorage for persistence
    @AppStorage("animationDurationMs") var animationDurationMs: Double = 200.0
    @AppStorage("ticksPerSecond") var ticksPerSecond: Int = 240
    @AppStorage("easingType") var easingType: EasingType = .easeOutQuadratic

    private var state: OpaquePointer?
    
    init() {
        start()
    }

    func start() {
        if !checkAccessibilityPermissions() {
            print("Accessibility permissions not granted")
            status = .permissionsRequired
            return
        }
        
        stop()
        
        var options = FasterSwiperOptions()
        options.animation_duration_per_space_ns = Int64(animationDurationMs * 1_000_000)
        options.easing_function_type = easingType.cValue
        options.ticks_per_second = Int64(ticksPerSecond)
        
        state = CreateFasterSwiper(&options)
        if state == nil {
            print("Failed to create FasterSwiper")
            status = .failedToStart
            return
        }
        
        if !StartFasterSwiper(state!) {
            print("Failed to start FasterSwiper")
            status = .failedToStart
            return
        }
        
        status = .started
    }

    func stop() {
        if let state = state {
            StopFasterSwiper(state)
            DestroyFasterSwiper(state)
            self.state = nil
        }
    }

    deinit {
        stop()
    }

    func checkAccessibilityPermissions() -> Bool {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }
}
