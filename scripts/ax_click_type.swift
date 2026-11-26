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

// Waited for rather than assumed: a cold development bundle can take a while
// to evaluate and lay out, and on CI every run is cold.
var found: AXUIElement?
for _ in 0..<80 {
  found = find(app, args[2])
  if found != nil { break }
  usleep(250_000)
}
guard let field = found else {
  print("missing \(args[2])")
  exit(1)
}
var origin = CGPoint.zero
var size = CGSize.zero
AXValueGetValue(attribute(field, kAXPositionAttribute) as! AXValue, .cgPoint, &origin)
AXValueGetValue(attribute(field, kAXSizeAttribute) as! AXValue, .cgSize, &size)
let centre = CGPoint(x: origin.x + size.width / 2, y: origin.y + size.height / 2)

func focusedRole() -> String {
  attribute(app, kAXFocusedUIElementAttribute).map { attribute($0 as! AXUIElement, kAXRoleAttribute) as? String ?? "?" } ?? "none"
}

// The click goes to whatever window is topmost at that point, so the host has
// to *be* frontmost first -- asked for and then waited on, because activation
// is asynchronous and a click sent before it lands goes to the previous app.
// Up to five attempts: a host whose hit testing is wrong fails every one,
// while one that was simply not in front yet passes on a later one.
var focused = "none"
for attempt in 1...5 {
  if attempt > 1 { usleep(500_000) }
  let host = NSRunningApplication(processIdentifier: pid)
  _ = host?.activate()
  for _ in 0..<40 where NSWorkspace.shared.frontmostApplication?.processIdentifier != pid {
    usleep(50_000)
  }
  usleep(200_000)
  for type in [CGEventType.leftMouseDown, .leftMouseUp] {
    CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: centre, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(80_000)
  }
  usleep(300_000)
  focused = focusedRole()
  if focused == "AXTextField" { break }
}
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
