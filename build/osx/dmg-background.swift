// Installer artwork rendered with system AppKit; no third-party fonts/assets.
import AppKit

guard CommandLine.arguments.count == 3 else { fatalError("Expected NEW_PNG VERSION") }
let path = CommandLine.arguments[1]
guard !FileManager.default.fileExists(atPath: path) else { fatalError("Refusing overwrite") }
let width: CGFloat = 680, height: CGFloat = 440
let image = NSImage(size: NSSize(width: width, height: height))
image.lockFocus()
NSColor(calibratedRed: 0.965, green: 0.975, blue: 0.985, alpha: 1).setFill()
NSRect(x: 0, y: 0, width: width, height: height).fill()
func text(_ value: String, _ top: CGFloat, _ size: CGFloat, _ bold: Bool = false) {
    let style = NSMutableParagraphStyle()
    style.alignment = .center
    let attributes: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: size, weight: bold ? .semibold : .regular),
        .foregroundColor: NSColor(calibratedRed: 0.20, green: 0.27, blue: 0.34, alpha: 1),
        .paragraphStyle: style
    ]
    (value as NSString).draw(in: NSRect(x: 20, y: height - top - 34, width: width - 40, height: 34), withAttributes: attributes)
}
text("WizNotePlus", 24, 28, true)
text("\(CommandLine.arguments[2])  ·  Apple Silicon", 65, 14)
let arrow = NSBezierPath()
arrow.lineWidth = 2.5
arrow.lineCapStyle = .round
arrow.lineJoinStyle = .round
let center = height - 169
arrow.move(to: NSPoint(x: 285, y: center))
arrow.line(to: NSPoint(x: 395, y: center))
arrow.move(to: NSPoint(x: 385, y: center + 9))
arrow.line(to: NSPoint(x: 395, y: center))
arrow.line(to: NSPoint(x: 385, y: center - 9))
NSColor(calibratedRed: 0.45, green: 0.57, blue: 0.66, alpha: 1).setStroke()
arrow.stroke()
text("将左侧应用拖入右侧 Applications 文件夹", 246, 15)
text("macOS 14.0 及以上  ·  本地签名，未公证", 397, 11)
image.unlockFocus()
guard let tiff = image.tiffRepresentation,
      let bitmap = NSBitmapImageRep(data: tiff),
      let png = bitmap.representation(using: .png, properties: [:]) else { fatalError("Cannot render PNG") }
try png.write(to: URL(fileURLWithPath: path), options: .withoutOverwriting)
