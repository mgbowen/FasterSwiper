import AppKit

extension NSImage {
    static func stoplightIcon(color: NSColor) -> NSImage {
        let config = NSImage.SymbolConfiguration(paletteColors: [color])
        let image = NSImage(systemSymbolName: "circle.fill", accessibilityDescription: nil)?
            .withSymbolConfiguration(config)
        image?.isTemplate = false
        return image ?? NSImage()
    }
}
