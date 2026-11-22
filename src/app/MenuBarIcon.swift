import AppKit

@MainActor
enum MenuBarIcon {
    private static let basePointSize: CGFloat = 15
    private static let badgePointSize: CGFloat = 9
    private static let haloExtraPointSize: CGFloat = 3
    private static let badgeOverflowRight: CGFloat = 3
    private static let canvasHeight: CGFloat = 18

    static let normal: NSImage = makeNormalIcon()
    static let updateAvailable: NSImage = makeUpdateAvailableIcon()

    private static func makeNormalIcon() -> NSImage {
        let config = NSImage.SymbolConfiguration(
            pointSize: basePointSize,
            weight: .regular
        )
        let image =
            NSImage(
                systemSymbolName: "appwindow.swipe.rectangle",
                accessibilityDescription: "FasterSwiper"
            )?
            .withSymbolConfiguration(config)
            ?? NSImage()
        image.isTemplate = true
        return image
    }

    private static func makeUpdateAvailableIcon() -> NSImage {
        let baseMeasureConfig = NSImage.SymbolConfiguration(
            pointSize: basePointSize,
            weight: .regular
        )
        let baseSize =
            NSImage(
                systemSymbolName: "appwindow.swipe.rectangle",
                accessibilityDescription: nil
            )?
            .withSymbolConfiguration(baseMeasureConfig)?
            .size ?? NSSize(width: 22, height: 15)

        let canvasSize = NSSize(
            width: ceil(baseSize.width + badgeOverflowRight),
            height: canvasHeight
        )

        let image = NSImage(size: canvasSize, flipped: false) { rect in
            guard let ctx = NSGraphicsContext.current else { return false }
            let cgContext = ctx.cgContext

            // Isolate the cutout to this icon so .destinationOut never punches
            // through the macOS menu bar's own backdrop.
            cgContext.beginTransparencyLayer(auxiliaryInfo: nil)
            defer { cgContext.endTransparencyLayer() }

            let baseConfig = NSImage.SymbolConfiguration(
                pointSize: basePointSize,
                weight: .regular
            ).applying(NSImage.SymbolConfiguration(hierarchicalColor: .labelColor))
            if let baseSymbol = NSImage(
                systemSymbolName: "appwindow.swipe.rectangle",
                accessibilityDescription: nil
            )?.withSymbolConfiguration(baseConfig) {
                let baseOrigin = NSPoint(
                    x: 0,
                    y: (rect.height - baseSymbol.size.height) / 2.0
                )
                baseSymbol.draw(
                    in: NSRect(origin: baseOrigin, size: baseSymbol.size),
                    from: .zero,
                    operation: .sourceOver,
                    fraction: 1.0
                )
            }

            let badgeConfig = NSImage.SymbolConfiguration(
                pointSize: badgePointSize,
                weight: .semibold
            ).applying(
                NSImage.SymbolConfiguration(
                    paletteColors: [.white, .controlAccentColor]
                )
            )
            if let badgeSymbol = NSImage(
                systemSymbolName: "arrow.down.circle.fill",
                accessibilityDescription: nil
            )?.withSymbolConfiguration(badgeConfig) {
                let badgeRect = NSRect(
                    x: rect.width - badgeSymbol.size.width - 0.5,
                    y: 0.5,
                    width: badgeSymbol.size.width,
                    height: badgeSymbol.size.height
                )

                // Draw sibling `circle.fill` slightly larger with .destinationOut
                // so the transparent halo is optically concentric with the badge.
                let haloConfig = NSImage.SymbolConfiguration(
                    pointSize: badgePointSize + haloExtraPointSize,
                    weight: .semibold
                )
                if let haloSymbol = NSImage(
                    systemSymbolName: "circle.fill",
                    accessibilityDescription: nil
                )?.withSymbolConfiguration(haloConfig) {
                    let haloRect = NSRect(
                        x: badgeRect.midX - haloSymbol.size.width / 2.0,
                        y: badgeRect.midY - haloSymbol.size.height / 2.0,
                        width: haloSymbol.size.width,
                        height: haloSymbol.size.height
                    )
                    haloSymbol.draw(
                        in: haloRect,
                        from: .zero,
                        operation: .destinationOut,
                        fraction: 1.0
                    )
                }

                badgeSymbol.draw(
                    in: badgeRect,
                    from: .zero,
                    operation: .sourceOver,
                    fraction: 1.0
                )
            }

            return true
        }

        image.isTemplate = false
        image.accessibilityDescription = "FasterSwiper — Update Available"
        return image
    }
}
