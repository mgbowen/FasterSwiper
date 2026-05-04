import Foundation
import FasterSwiper_Proto

public struct BezierFormatStyle: ParseableFormatStyle {
    public var parseStrategy: BezierParseStrategy {
        BezierParseStrategy()
    }

    public init() {}

    public func format(_ value: Fasterswiper_Proto_CubicBezierCurve) -> String {
        let formatter = NumberFormatter()
        formatter.minimumFractionDigits = 2
        formatter.maximumFractionDigits = 2
        formatter.numberStyle = .decimal

        func fmt(_ n: Double) -> String {
            return formatter.string(from: NSNumber(value: n)) ?? "\(n)"
        }

        return "\(fmt(value.p1X)), \(fmt(value.p1Y)), \(fmt(value.p2X)), \(fmt(value.p2Y))"
    }
}

public struct BezierParseStrategy: ParseStrategy {
    public init() {}

    public func parse(_ value: String) throws -> Fasterswiper_Proto_CubicBezierCurve {
        var cleaned = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleaned.lowercased().hasPrefix("cubic-bezier(") && cleaned.hasSuffix(")") {
            cleaned = String(cleaned.dropFirst("cubic-bezier(".count).dropLast())
        }

        let components = cleaned.split(separator: ",").map {
            $0.trimmingCharacters(in: .whitespaces)
        }
        guard components.count == 4 else {
            throw BezierParseError.invalidCount
        }

        guard let p1x = Double(components[0]),
            let p1y = Double(components[1]),
            let p2x = Double(components[2]),
            let p2y = Double(components[3])
        else {
            throw BezierParseError.invalidFormat
        }

        var curve = Fasterswiper_Proto_CubicBezierCurve()
        curve.p1X = p1x
        curve.p1Y = p1y
        curve.p2X = p2x
        curve.p2Y = p2y
        return curve
    }
}

public enum BezierParseError: Error {
    case invalidCount
    case invalidFormat
}

extension FormatStyle where Self == BezierFormatStyle {
    public static var bezier: BezierFormatStyle { .init() }
}

public typealias EasingFunction = Fasterswiper_Proto_EasingFunction

extension Fasterswiper_Proto_EasingFunction: @retroactive CustomStringConvertible, @retroactive Identifiable {
    public var description: String {
        switch self {
        case .linear: return "Linear"
        case .quadraticEaseOut: return "Quadratic Ease Out"
        case .quinticEaseOut: return "Quintic Ease Out"
        case .cubicBezierCurve: return "Cubic Bezier Curve"
        }
    }

    public var id: Int { 
        return self.rawValue 
    }
}