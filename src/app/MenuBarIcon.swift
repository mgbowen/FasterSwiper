import AppKit
import FasterSwiper_Views

public enum MenuBarIconState: Sendable, Equatable {
    case normal
    case updateAvailable
    case error
    case stopped
}

@MainActor
public enum MenuBarIcon {
    private static let sfSymbolName = "appwindow.swipe.rectangle"
    private static let sfSymbolPointSize: CGFloat = 15
    private static let fallbackBaseSize = NSSize(width: 22, height: 18)
    private static let canvasHeight: CGFloat = 18

    private static let statusDotDiameter: CGFloat = 7.5
    private static let statusDotRightInset: CGFloat = 0.5
    private static let statusDotBottomInset: CGFloat = 1.0
    private static let badgeHaloThickness: CGFloat = 1.4

    private struct StyleImageSet {
        let normal: NSImage
        let updateAvailable: NSImage
        let error: NSImage
        let stopped: NSImage
    }

    private static let appIconImages = makeImageSet(for: .appIcon)
    private static let sfSymbolImages = makeImageSet(for: .sfSymbol)

    public static var normal: NSImage { appIconImages.normal }
    public static var updateAvailable: NSImage { appIconImages.updateAvailable }
    public static var error: NSImage { appIconImages.error }
    public static var stopped: NSImage { appIconImages.stopped }

    public static func state(
        daemonStatus: DaemonStatus,
        hasPendingUpdate: Bool,
        showStatusGlyph: Bool,
        override: MenuBarStatusIndicatorOverride = .automatic
    ) -> MenuBarIconState {
        switch override {
        case .automatic:
            break
        case .normal:
            return .normal
        case .updateAvailable:
            return .updateAvailable
        case .error:
            return .error
        case .stopped:
            return .stopped
        }

        if showStatusGlyph
            && (daemonStatus == .accessibilityPermissionDenied
                || daemonStatus == .genericError)
        {
            return .error
        }

        if hasPendingUpdate {
            return .updateAvailable
        }

        if showStatusGlyph && daemonStatus == .stopped {
            return .stopped
        }

        return .normal
    }

    public static func image(
        for state: MenuBarIconState,
        style: MenuBarIconStyle = .appIcon
    ) -> NSImage {
        let imageSet =
            switch style {
            case .appIcon: appIconImages
            case .sfSymbol: sfSymbolImages
            }

        return switch state {
        case .normal: imageSet.normal
        case .updateAvailable: imageSet.updateAvailable
        case .error: imageSet.error
        case .stopped: imageSet.stopped
        }
    }

    public static func accessibilityLabel(
        for state: MenuBarIconState,
        daemonStatus: DaemonStatus
    ) -> String {
        switch state {
        case .normal:
            "FasterSwiper"
        case .updateAvailable:
            "FasterSwiper — Update Available"
        case .error:
            "FasterSwiper — \(daemonStatus.text)"
        case .stopped:
            "FasterSwiper — Stopped"
        }
    }

    private static func makeImageSet(for style: MenuBarIconStyle) -> StyleImageSet {
        let baseIcon = loadBaseIcon(style: style)
        return StyleImageSet(
            normal: makeNormalIcon(baseIcon: baseIcon),
            updateAvailable: makeBadgedIcon(
                baseIcon: baseIcon,
                badgeColor: .systemBlue,
                accessibilityDescription: "FasterSwiper — Update Available"
            ),
            error: makeBadgedIcon(
                baseIcon: baseIcon,
                badgeColor: .systemRed,
                accessibilityDescription: "FasterSwiper — Error"
            ),
            stopped: makeBadgedIcon(
                baseIcon: baseIcon,
                badgeColor: .systemGray,
                accessibilityDescription: "FasterSwiper — Stopped"
            )
        )
    }

    private static func loadBaseIcon(style: MenuBarIconStyle) -> NSImage? {
        let image: NSImage? =
            switch style {
            case .appIcon:
                Bundle.main.url(
                    forResource: "MenuBarIcon",
                    withExtension: "svg"
                ).flatMap(NSImage.init(contentsOf:))
            case .sfSymbol:
                NSImage(
                    systemSymbolName: sfSymbolName,
                    accessibilityDescription: nil
                )?
                .withSymbolConfiguration(
                    NSImage.SymbolConfiguration(
                        pointSize: sfSymbolPointSize,
                        weight: .regular
                    )
                )
            }
        image?.isTemplate = true
        return image
    }

    private static func canvasSize(for baseIcon: NSImage?) -> NSSize {
        let baseSize = baseIcon?.size ?? fallbackBaseSize
        return NSSize(width: ceil(baseSize.width), height: canvasHeight)
    }

    private static func makeNormalIcon(baseIcon: NSImage?) -> NSImage {
        let size = canvasSize(for: baseIcon)
        let image = NSImage(size: size, flipped: false) { rect in
            guard let baseIcon else { return false }
            let baseRect = NSRect(
                x: (rect.width - baseIcon.size.width) / 2.0,
                y: (rect.height - baseIcon.size.height) / 2.0,
                width: baseIcon.size.width,
                height: baseIcon.size.height
            )
            baseIcon.draw(
                in: baseRect,
                from: .zero,
                operation: .sourceOver,
                fraction: 1.0
            )
            return true
        }
        image.isTemplate = true
        image.accessibilityDescription = "FasterSwiper"
        return image
    }

    private static func makeBadgedIcon(
        baseIcon: NSImage?,
        badgeColor: NSColor,
        accessibilityDescription: String
    ) -> NSImage {
        let size = canvasSize(for: baseIcon)

        let image = NSImage(size: size, flipped: false) { rect in
            guard let ctx = NSGraphicsContext.current else { return false }
            let cgContext = ctx.cgContext

            // Isolate cutouts to this icon so .clear never punches through the
            // macOS menu bar's own backdrop.
            cgContext.beginTransparencyLayer(auxiliaryInfo: nil)
            defer { cgContext.endTransparencyLayer() }

            if let baseIcon {
                let baseRect = NSRect(
                    x: (rect.width - baseIcon.size.width) / 2.0,
                    y: (rect.height - baseIcon.size.height) / 2.0,
                    width: baseIcon.size.width,
                    height: baseIcon.size.height
                )

                // Tint the monochrome template dynamically to .labelColor so
                // badged icons match both Light and Dark macOS menu bars.
                cgContext.beginTransparencyLayer(auxiliaryInfo: nil)
                baseIcon.draw(
                    in: baseRect,
                    from: .zero,
                    operation: .sourceOver,
                    fraction: 1.0
                )
                NSColor.labelColor.setFill()
                baseRect.fill(using: .sourceIn)
                cgContext.endTransparencyLayer()
            }

            let dotRect = CGRect(
                x: rect.width - statusDotDiameter - statusDotRightInset,
                y: statusDotBottomInset,
                width: statusDotDiameter,
                height: statusDotDiameter
            )
            let haloRect = dotRect.insetBy(
                dx: -badgeHaloThickness,
                dy: -badgeHaloThickness
            )
            cgContext.setBlendMode(.clear)
            cgContext.addEllipse(in: haloRect)
            cgContext.fillPath()

            cgContext.setBlendMode(.normal)
            cgContext.setFillColor(badgeColor.cgColor)
            cgContext.addEllipse(in: dotRect)
            cgContext.fillPath()

            return true
        }

        image.isTemplate = false
        image.accessibilityDescription = accessibilityDescription
        return image
    }
}
