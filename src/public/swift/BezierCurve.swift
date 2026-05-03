import Foundation

public struct BezierCurve: Codable, Hashable {
    public var p1x: Double
    public var p1y: Double
    public var p2x: Double
    public var p2y: Double

    public init(p1x: Double, p1y: Double, p2x: Double, p2y: Double) {
        self.p1x = p1x
        self.p1y = p1y
        self.p2x = p2x
        self.p2y = p2y
    }
}

extension BezierCurve: RawRepresentable {
    public init?(rawValue: String) {
        if let curve = try? BezierParseStrategy().parse(rawValue) {
            self = curve
        } else {
            return nil
        }
    }

    public var rawValue: String {
        "\(p1x), \(p1y), \(p2x), \(p2y)"
    }
}

public struct BezierFormatStyle: ParseableFormatStyle {
    public var parseStrategy: BezierParseStrategy {
        BezierParseStrategy()
    }

    public init() {}

    public func format(_ value: BezierCurve) -> String {
        let formatter = NumberFormatter()
        formatter.minimumFractionDigits = 2
        formatter.maximumFractionDigits = 2
        formatter.numberStyle = .decimal

        func fmt(_ n: Double) -> String {
            return formatter.string(from: NSNumber(value: n)) ?? "\(n)"
        }

        return "\(fmt(value.p1x)), \(fmt(value.p1y)), \(fmt(value.p2x)), \(fmt(value.p2y))"
    }
}

public struct BezierParseStrategy: ParseStrategy {
    public init() {}

    public func parse(_ value: String) throws -> BezierCurve {
        // Handle "cubic-bezier(x, x, x, x)" format as well, just in case
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

        return BezierCurve(p1x: p1x, p1y: p1y, p2x: p2x, p2y: p2y)
    }
}

public enum BezierParseError: Error {
    case invalidCount
    case invalidFormat
}

extension FormatStyle where Self == BezierFormatStyle {
    public static var bezier: BezierFormatStyle { .init() }
}
