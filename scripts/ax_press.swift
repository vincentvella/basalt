// Presses accessibility elements by label in one process, the way VoiceOver's
// VO-Space does: an AXPress from outside it.
//
//   swift scripts/ax_press.swift <pid> <label>...
//
// By pid through AXUIElementCreateApplication, not System Events: System Events
// resolves a process by *name*, every Basalt host is named basalt_appkit, and
// with two running it searched the other one's window and reported the element
// missing. Prints "pressed <label>" or "missing <label>" per label, in order,
// and exits 2 if the process has no accessibility permission.
import ApplicationServices
import Foundation

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[1]) else {
  FileHandle.standardError.write("usage: ax_press.swift <pid> <label>...\n".data(using: .utf8)!)
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

// Depth first; the first element whose description is the label and which is
// not the text inside it.
func find(_ element: AXUIElement, _ label: String) -> AXUIElement? {
  if (attribute(element, kAXDescriptionAttribute) as? String) == label,
     (attribute(element, kAXRoleAttribute) as? String) != "AXStaticText" {
    return element
  }
  for child in (attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? [] {
    if let found = find(child, label) { return found }
  }
  return nil
}

for label in args.dropFirst(2) {
  if let element = find(app, label), AXUIElementPerformAction(element, kAXPressAction as CFString) == .success {
    print("pressed \(label)")
  } else {
    print("missing \(label)")
  }
  fflush(stdout)
  usleep(400_000) // let React commit before the next press reads the tree
}
