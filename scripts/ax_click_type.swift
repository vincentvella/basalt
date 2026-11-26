// Clicks an element for real and types into it, in one process.
//
//   swift scripts/ax_click_type.swift <pid> <label> <text>
//
// The element is found by its accessibility label, by pid (see ax_press.swift
// for why not System Events), and clicked with a CGEvent at its centre -- a
// real click, because what this checks is which native view AppKit delivers it
// to. The text then goes in as keyboard events. Prints the role of whatever has
// keyboard focus after the click; exits 2 without accessibility permission.
import AppKit
import ApplicationServices
import Foundation

let args = CommandLine.arguments
guard args.count == 4, let pid = pid_t(args[1]) else {
  FileHandle.standardError.write("usage: ax_click_type.swift <pid> <label> <text>\n".data(using: .utf8)!)
  exit(64)
}
guard AXIsProcessTrusted() else {
  print("no accessibility permission")
  exit(2)
}
let app = AXUIElementCreateApplication(pid)

func attribute(_ element: AXUIElement, _ name: String) -> AnyObject? {
  var value: AnyObject?
  AXUIElementCopyAttributeValue(element, name as CFString, &value)
  return value
}

func find(_ element: AXUIElement, _ label: String) -> AXUIElement? {
  if (attribute(element, kAXDescriptionAttribute) as? String) == label { return element }
  for child in (attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? [] {
    if let found = find(child, label) { return found }
  }
  return nil
}

guard let field = find(app, args[2]) else {
  print("missing \(args[2])")
  exit(1)
}
var origin = CGPoint.zero
var size = CGSize.zero
AXValueGetValue(attribute(field, kAXPositionAttribute) as! AXValue, .cgPoint, &origin)
AXValueGetValue(attribute(field, kAXSizeAttribute) as! AXValue, .cgSize, &size)
let centre = CGPoint(x: origin.x + size.width / 2, y: origin.y + size.height / 2)

// The click goes to whatever window is topmost at that point, so the host is
// brought forward first.
_ = NSRunningApplication(processIdentifier: pid)?.activate()
usleep(400_000)
for type in [CGEventType.leftMouseDown, .leftMouseUp] {
  CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: centre, mouseButton: .left)?.post(tap: .cghidEventTap)
  usleep(80_000)
}
usleep(300_000)

let focused = attribute(app, kAXFocusedUIElementAttribute).map { attribute($0 as! AXUIElement, kAXRoleAttribute) as? String ?? "?" } ?? "none"
print("focused \(focused)")

for scalar in args[3].utf16 {
  var unit = scalar
  for down in [true, false] {
    let event = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: down)
    event?.keyboardSetUnicodeString(stringLength: 1, unicodeString: &unit)
    event?.post(tap: .cghidEventTap)
    usleep(30_000)
  }
}
usleep(500_000)
