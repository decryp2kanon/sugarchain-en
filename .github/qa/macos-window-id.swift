import AppKit
import CoreGraphics

guard CommandLine.arguments.count == 2 else { exit(2) }
let name = CommandLine.arguments[1]
if let windows = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as? [[String: Any]] {
    let matches = windows.filter {
        let owner = $0[kCGWindowOwnerName as String] as? String ?? ""
        let pid = $0[kCGWindowOwnerPID as String] as? Int32 ?? 0
        let path = NSRunningApplication(processIdentifier: pid)?.bundleURL?.path ?? ""
        return (owner.contains(name) || path == "/Applications/\(name).app")
            && ($0[kCGWindowLayer as String] as? Int ?? -1) >= 0
    }
    for window in matches {
        let id = window[kCGWindowNumber as String] as? Int ?? 0
        let bounds = window[kCGWindowBounds as String] as? [String: Any] ?? [:]
        let height = bounds["Height"] as? Double ?? 0
        let width = bounds["Width"] as? Double ?? 0
        if id > 0 && width >= 220 && height >= 120 { print("\(id) \(Int(width)) \(Int(height))") }
    }
}
