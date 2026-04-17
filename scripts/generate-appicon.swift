#!/usr/bin/env swift
// Generate app/apple-catalyst/Assets.xcassets/AppIcon.appiconset/icon_1024.png
//
// A Smalltalk-themed hot-air balloon drawn with CoreGraphics. 1024×1024
// RGBA PNG, ready for Xcode's consolidated "universal" app icon slot.
//
// Re-run to regenerate if the design changes:
//     ./scripts/generate-appicon.swift
//
// Copyright (c) 2026 Aaron Wohl. MIT License.

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers
import CoreText

let side = 1024
let bytesPerRow = side * 4
let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
guard let ctx = CGContext(
    data: nil, width: side, height: side,
    bitsPerComponent: 8, bytesPerRow: bytesPerRow,
    space: colorSpace,
    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue |
                CGBitmapInfo.byteOrder32Big.rawValue)
else {
    print("Failed to create CGContext")
    exit(1)
}

// ─── Sky gradient background ─────────────────────────────────────
let skyColors = [
    CGColor(red: 0.70, green: 0.87, blue: 0.98, alpha: 1.0),  // pale
    CGColor(red: 0.20, green: 0.55, blue: 0.85, alpha: 1.0)   // deep
] as CFArray
let sky = CGGradient(colorsSpace: colorSpace, colors: skyColors, locations: [0, 1])!
ctx.drawLinearGradient(sky,
                       start: CGPoint(x: 0, y: side),
                       end:   CGPoint(x: 0, y: 0),
                       options: [])

// ─── Three soft clouds ───────────────────────────────────────────
func drawCloud(at p: CGPoint, scale: CGFloat) {
    ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.75))
    for (dx, dy, r) in [(-60.0, 0.0, 55.0),
                        (0.0,   20.0, 75.0),
                        (60.0,  5.0,  55.0),
                        (20.0, -25.0, 45.0)] {
        let cx = p.x + CGFloat(dx) * scale
        let cy = p.y + CGFloat(dy) * scale
        let rr = CGFloat(r) * scale
        ctx.fillEllipse(in: CGRect(x: cx - rr, y: cy - rr, width: rr * 2, height: rr * 2))
    }
}
drawCloud(at: CGPoint(x: 180, y: 820), scale: 0.9)
drawCloud(at: CGPoint(x: 860, y: 770), scale: 0.7)
drawCloud(at: CGPoint(x: 500, y: 170), scale: 0.6)

// ─── Balloon envelope: teardrop (ellipse + slight bottom pinch) ──
let bx: CGFloat = CGFloat(side) / 2
let by: CGFloat = CGFloat(side) * 0.58  // upper half
let rx: CGFloat = 310
let ry: CGFloat = 360

// Save/clip to the envelope shape so stripes stay inside.
ctx.saveGState()
let envelope = CGPath(ellipseIn: CGRect(x: bx - rx, y: by - ry, width: rx * 2, height: ry * 2),
                      transform: nil)
ctx.addPath(envelope)
ctx.clip()

// Base colour (warm red)
ctx.setFillColor(CGColor(red: 0.83, green: 0.18, blue: 0.18, alpha: 1.0))
ctx.fill(CGRect(x: bx - rx - 2, y: by - ry - 2, width: rx * 2 + 4, height: ry * 2 + 4))

// Cream stripes — six vertical gores
let gores = 6
let goreW = (rx * 2) / CGFloat(gores)
for i in stride(from: 0, to: gores, by: 2) {
    let x = bx - rx + CGFloat(i) * goreW
    ctx.setFillColor(CGColor(red: 0.97, green: 0.92, blue: 0.78, alpha: 1.0))
    ctx.fill(CGRect(x: x, y: by - ry - 2, width: goreW, height: ry * 2 + 4))
}

// Specular highlight — a pale ellipse on the upper left
ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.18))
ctx.fillEllipse(in: CGRect(x: bx - rx * 0.85, y: by + ry * 0.1,
                            width: rx * 1.1, height: ry * 1.0))
ctx.restoreGState()

// Envelope outline
ctx.setLineWidth(8)
ctx.setStrokeColor(CGColor(red: 0.25, green: 0.08, blue: 0.08, alpha: 1.0))
ctx.addPath(envelope)
ctx.strokePath()

// ─── "St" monogram, centred on the envelope ──────────────────────
let font = CTFontCreateWithName("Georgia-Bold" as CFString, 260, nil)
let textColor = CGColor(red: 1, green: 1, blue: 1, alpha: 0.95)
let attrs: [CFString: Any] = [
    kCTFontAttributeName:             font,
    kCTForegroundColorAttributeName:  textColor
]
let mono = CFAttributedStringCreate(kCFAllocatorDefault,
                                    "St" as CFString,
                                    attrs as CFDictionary)!
let line = CTLineCreateWithAttributedString(mono)
let lineBounds = CTLineGetBoundsWithOptions(line, .useOpticalBounds)
ctx.textPosition = CGPoint(
    x: bx - lineBounds.width / 2 - lineBounds.origin.x,
    y: by - lineBounds.height / 2 - lineBounds.origin.y)
CTLineDraw(line, ctx)

// ─── Suspension lines + basket ───────────────────────────────────
let basketTopY: CGFloat = by - ry - 90
let basketHeight: CGFloat = 95
let basketWidth: CGFloat = 190
let basketX: CGFloat = bx - basketWidth / 2
let ropeTopY: CGFloat = by - ry * 0.95  // just inside the envelope bottom
let envBottomAttach = [-rx * 0.55, -rx * 0.2, rx * 0.2, rx * 0.55]
let basketAttach   = [-basketWidth / 2 + 8, -basketWidth / 2 + 62,
                       basketWidth / 2 - 62,  basketWidth / 2 - 8]

ctx.setStrokeColor(CGColor(red: 0.12, green: 0.08, blue: 0.05, alpha: 1.0))
ctx.setLineWidth(6)
for i in 0..<4 {
    let top = CGPoint(x: bx + envBottomAttach[i], y: ropeTopY)
    let btm = CGPoint(x: bx + basketAttach[i],  y: basketTopY)
    ctx.move(to: top)
    ctx.addLine(to: btm)
}
ctx.strokePath()

// Basket — wicker brown with a darker top rim
let wicker = CGColor(red: 0.52, green: 0.33, blue: 0.15, alpha: 1.0)
let wickerDark = CGColor(red: 0.32, green: 0.20, blue: 0.08, alpha: 1.0)
ctx.setFillColor(wicker)
ctx.fill(CGRect(x: basketX, y: basketTopY - basketHeight,
                 width: basketWidth, height: basketHeight))
// Top rim
ctx.setFillColor(wickerDark)
ctx.fill(CGRect(x: basketX, y: basketTopY - 14,
                 width: basketWidth, height: 14))
// Weave hatching — thin diagonal lines
ctx.setStrokeColor(CGColor(red: 0.30, green: 0.18, blue: 0.06, alpha: 0.8))
ctx.setLineWidth(2)
for dy in stride(from: -basketHeight + 10, to: -10, by: 14) {
    ctx.move(to: CGPoint(x: basketX + 4, y: basketTopY + dy))
    ctx.addLine(to: CGPoint(x: basketX + basketWidth - 4, y: basketTopY + dy + 10))
}
ctx.strokePath()

// Basket outline
ctx.setStrokeColor(CGColor(red: 0.15, green: 0.08, blue: 0.02, alpha: 1))
ctx.setLineWidth(5)
ctx.stroke(CGRect(x: basketX, y: basketTopY - basketHeight,
                   width: basketWidth, height: basketHeight))

// ─── Emit PNG ────────────────────────────────────────────────────
let arg = CommandLine.arguments.dropFirst().first
    ?? "app/apple-catalyst/Assets.xcassets/AppIcon.appiconset/icon_1024.png"
let outURL = URL(fileURLWithPath: arg)
try? FileManager.default.createDirectory(
    at: outURL.deletingLastPathComponent(),
    withIntermediateDirectories: true)

guard let image = ctx.makeImage(),
      let dest = CGImageDestinationCreateWithURL(
        outURL as CFURL, UTType.png.identifier as CFString, 1, nil)
else {
    print("Failed to create PNG destination")
    exit(1)
}
CGImageDestinationAddImage(dest, image, nil)
if !CGImageDestinationFinalize(dest) {
    print("Failed to write PNG")
    exit(1)
}
print("Wrote \(arg)")
