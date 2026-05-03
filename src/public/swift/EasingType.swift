public enum EasingType: Int, CaseIterable, Identifiable, Codable {
    case linear = 0
    case easeOutQuadratic = 1
    case easeOutQuintic = 2
    case bezierCurve = 3

    public var id: Int { self.rawValue }
    public var name: String {
        switch self {
        case .linear: return "Linear"
        case .easeOutQuadratic: return "Ease Out Quadratic"
        case .easeOutQuintic: return "Ease Out Quintic"
        case .bezierCurve: return "Cubic Bezier Curve"
        }
    }
}
