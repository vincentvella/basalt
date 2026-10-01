// The macOS half of core/MenuModel.h: the application menu, in the system
// menu bar.
//
// This is the platform where a menu bar is not decoration. `NSApplication`
// hands every key event to the main menu's `performKeyEquivalent:` before the
// responder chain sees it, so Cmd-C only reaches a text field's editor if there
// is a Copy item to route it. Until this file existed the host installed a menu
// with one Quit item, and the consequence was quiet and complete: copy, cut,
// paste, undo and select-all did nothing in a <TextInput> on macOS, on every
// app, with nothing in any log.
//
// Which is why the default below is not a courtesy. An app that never renders a
// <Menu> still gets an Edit menu, because an app that never renders a <Menu>
// still has text fields.
//
// ## Roles, and why they have no target
//
// Every role maps to a selector and a `nil` target. That is the whole trick:
// a menu item with no target sends its action down the responder chain, so
// `copy:` arrives at whatever has focus -- the field editor of whichever
// NSTextField the person is typing in. Targeting anything, including this file,
// would deliver it to the wrong object and do nothing.

#include "MenuModel.h"

#import <Cocoa/Cocoa.h>

#include <string>
#include <utility>
#include <vector>

// The target for an app's own items. Roles do not use it; see the header.
@interface RnAppKitMenuTargetBar : NSObject
- (instancetype)initWithHandler:(std::function<void(int)>)handler;
- (void)chose:(NSMenuItem *)sender;
@end

@implementation RnAppKitMenuTargetBar {
  std::function<void(int)> _handler;
}

- (instancetype)initWithHandler:(std::function<void(int)>)handler {
  self = [super init];
  if (self != nil) {
    _handler = std::move(handler);
  }
  return self;
}

- (void)chose:(NSMenuItem *)sender {
  if (_handler) {
    _handler(static_cast<int>(sender.tag));
  }
}

@end

namespace basalt {

namespace {

// Held for as long as the menu is installed: NSMenuItem's target is weak, so
// nothing else would keep it alive and every custom item would stop working
// the moment the menu was built.
__strong RnAppKitMenuTargetBar *&menuTarget() {
  static __strong RnAppKitMenuTargetBar *target = nil;
  return target;
}

// A role's selector, or nullptr when the name is not one this platform
// implements. `delete:` is spelled `delete:` and is a real NSResponder action
// despite the keyword.
SEL selectorForRole(const std::string &role) {
  if (role == "about") {
    return @selector(orderFrontStandardAboutPanel:);
  }
  if (role == "quit") {
    return @selector(terminate:);
  }
  if (role == "undo") {
    return @selector(undo:);
  }
  if (role == "redo") {
    return @selector(redo:);
  }
  if (role == "cut") {
    return @selector(cut:);
  }
  if (role == "copy") {
    return @selector(copy:);
  }
  if (role == "paste") {
    return @selector(paste:);
  }
  if (role == "delete") {
    return @selector(delete:);
  }
  if (role == "selectAll") {
    return @selector(selectAll:);
  }
  if (role == "minimize") {
    return @selector(performMiniaturize:);
  }
  if (role == "zoom") {
    return @selector(performZoom:);
  }
  if (role == "close") {
    return @selector(performClose:);
  }
  if (role == "togglefullscreen") {
    return @selector(toggleFullScreen:);
  }
  return nullptr;
}

// The shortcut a role brings with it. macOS has a standard one for each, and an
// app that spelled its own would be fighting the platform.
NSString *keyEquivalentForRole(const std::string &role, NSEventModifierFlags *mask) {
  *mask = NSEventModifierFlagCommand;
  if (role == "quit") {
    return @"q";
  }
  if (role == "undo") {
    return @"z";
  }
  if (role == "redo") {
    *mask |= NSEventModifierFlagShift;
    return @"z";
  }
  if (role == "cut") {
    return @"x";
  }
  if (role == "copy") {
    return @"c";
  }
  if (role == "paste") {
    return @"v";
  }
  if (role == "selectAll") {
    return @"a";
  }
  if (role == "minimize") {
    return @"m";
  }
  if (role == "close") {
    return @"w";
  }
  if (role == "togglefullscreen") {
    *mask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    return @"f";
  }
  *mask = 0;
  return @"";
}

// The platform's own word for a role. An app that gave a label keeps it; one
// that did not gets what every other Mac application says, localised by nobody
// -- which is a gap and is in docs/BACKLOG.md.
NSString *defaultLabelForRole(const std::string &role) {
  if (role == "about") {
    NSString *name = NSRunningApplication.currentApplication.localizedName;
    return [@"About " stringByAppendingString:name != nil ? name : @"this app"];
  }
  if (role == "quit") {
    NSString *name = NSRunningApplication.currentApplication.localizedName;
    return [@"Quit " stringByAppendingString:name != nil ? name : @"this app"];
  }
  if (role == "undo") {
    return @"Undo";
  }
  if (role == "redo") {
    return @"Redo";
  }
  if (role == "cut") {
    return @"Cut";
  }
  if (role == "copy") {
    return @"Copy";
  }
  if (role == "paste") {
    return @"Paste";
  }
  if (role == "delete") {
    return @"Delete";
  }
  if (role == "selectAll") {
    return @"Select All";
  }
  if (role == "minimize") {
    return @"Minimize";
  }
  if (role == "zoom") {
    return @"Zoom";
  }
  if (role == "close") {
    return @"Close";
  }
  if (role == "togglefullscreen") {
    return @"Toggle Full Screen";
  }
  return @"";
}

// "CmdOrCtrl+Shift+O" into AppKit's pair. Only used for an app's own items: a
// role brings the platform's shortcut.
NSString *keyEquivalentFrom(const std::string &accelerator, NSEventModifierFlags *mask) {
  *mask = 0;
  if (accelerator.empty()) {
    return @"";
  }
  size_t start = 0;
  while (true) {
    const size_t plus = accelerator.find('+', start);
    const std::string part =
        accelerator.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    if (plus == std::string::npos) {
      NSString *key = [NSString stringWithUTF8String:part.c_str()];
      return key != nil ? key.lowercaseString : @"";
    }
    if (part == "Cmd" || part == "Command" || part == "CmdOrCtrl" || part == "CommandOrControl") {
      *mask |= NSEventModifierFlagCommand;
    } else if (part == "Ctrl" || part == "Control") {
      *mask |= NSEventModifierFlagControl;
    } else if (part == "Shift") {
      *mask |= NSEventModifierFlagShift;
    } else if (part == "Alt" || part == "Option") {
      *mask |= NSEventModifierFlagOption;
    } else {
      *mask = 0;
      return @"";
    }
    start = plus + 1;
  }
}

void addItems(NSMenu *menu, const std::vector<MenuItemModel> &items);

void addItem(NSMenu *menu, const MenuItemModel &item) {
  if (item.separator) {
    [menu addItem:[NSMenuItem separatorItem]];
    return;
  }

  if (!item.submenu.empty()) {
    NSString *label = [NSString stringWithUTF8String:item.label.c_str()];
    NSMenuItem *parent = [menu addItemWithTitle:label != nil ? label : @""
                                         action:nullptr
                                  keyEquivalent:@""];
    NSMenu *submenu = [[NSMenu alloc] initWithTitle:label != nil ? label : @""];
    addItems(submenu, item.submenu);
    parent.submenu = submenu;
    return;
  }

  if (!item.role.empty()) {
    SEL selector = selectorForRole(item.role);
    if (selector == nullptr) {
      // An unknown role is dropped. A menu item that does nothing is worse
      // than one that is not there.
      return;
    }
    NSEventModifierFlags mask = 0;
    NSString *key = keyEquivalentForRole(item.role, &mask);
    NSString *label = item.label.empty() ? defaultLabelForRole(item.role)
                                         : [NSString stringWithUTF8String:item.label.c_str()];
    NSMenuItem *added = [menu addItemWithTitle:label != nil ? label : @""
                                        action:selector
                                 keyEquivalent:key];
    added.keyEquivalentModifierMask = mask;
    // No target, deliberately: see the header. This is what sends `copy:` down
    // the responder chain to whichever field has focus.
    added.target = nil;
    added.enabled = item.enabled;
    return;
  }

  NSEventModifierFlags mask = 0;
  NSString *key = keyEquivalentFrom(item.accelerator, &mask);
  NSString *label = [NSString stringWithUTF8String:item.label.c_str()];
  NSMenuItem *added = [menu addItemWithTitle:label != nil ? label : @""
                                      action:@selector(chose:)
                               keyEquivalent:key];
  added.keyEquivalentModifierMask = mask;
  added.target = menuTarget();
  added.tag = item.id;
  added.enabled = item.enabled;
}

void addItems(NSMenu *menu, const std::vector<MenuItemModel> &items) {
  // Items enable themselves from this project's model rather than from
  // AppKit's automatic validation, which would grey out every custom item --
  // nothing in the responder chain implements `chose:`.
  menu.autoenablesItems = NO;
  for (const MenuItemModel &item : items) {
    addItem(menu, item);
  }
}

// What an app gets without asking. The Edit menu is the part that matters: see
// the header for what its absence did.
MenuModel defaultMenu() {
  MenuItemModel app;
  app.label = "App";
  app.submenu = {
      MenuItemModel{.role = "about"},
      MenuItemModel{.separator = true},
      MenuItemModel{.role = "minimize"},
      MenuItemModel{.role = "close"},
      MenuItemModel{.separator = true},
      MenuItemModel{.role = "quit"},
  };

  MenuItemModel edit;
  edit.label = "Edit";
  edit.submenu = {
      MenuItemModel{.role = "undo"},
      MenuItemModel{.role = "redo"},
      MenuItemModel{.separator = true},
      MenuItemModel{.role = "cut"},
      MenuItemModel{.role = "copy"},
      MenuItemModel{.role = "paste"},
      MenuItemModel{.role = "delete"},
      MenuItemModel{.role = "selectAll"},
  };

  return {app, edit};
}

} // namespace

bool applicationMenuSupported() {
  return true;
}

namespace {

// How a key equivalent reads in the dump: "cmd+shift+z". Lower case and in a
// fixed order, so the text is the same however AppKit stored the mask.
std::string describeKey(NSMenuItem *item) {
  if (item.keyEquivalent.length == 0) {
    return {};
  }
  std::string out = " [";
  const NSEventModifierFlags mask = item.keyEquivalentModifierMask;
  if ((mask & NSEventModifierFlagControl) != 0) {
    out += "ctrl+";
  }
  if ((mask & NSEventModifierFlagOption) != 0) {
    out += "alt+";
  }
  if ((mask & NSEventModifierFlagShift) != 0) {
    out += "shift+";
  }
  if ((mask & NSEventModifierFlagCommand) != 0) {
    out += "cmd+";
  }
  out += item.keyEquivalent.lowercaseString.UTF8String;
  out += "]";
  return out;
}

void describeInto(std::string &out, NSMenu *menu, int depth) {
  for (NSMenuItem *item in menu.itemArray) {
    out.append(static_cast<size_t>(depth) * 2, ' ');
    if (item.isSeparatorItem) {
      out += "-\n";
      continue;
    }
    out += item.title.UTF8String != nullptr ? item.title.UTF8String : "";
    out += describeKey(item);
    if (!item.isEnabled) {
      out += " (disabled)";
    }
    out += "\n";
    if (item.submenu != nil) {
      describeInto(out, item.submenu, depth + 1);
    }
  }
}

} // namespace

// Not only what was asked for. macOS adds items of its own to menus it
// recognises by title -- "Close All" to a File menu, dictation and Emoji &
// Symbols to an Edit one -- and they are in the dump because they are in the
// menu. A test reads it by looking for what it expects rather than by comparing
// the whole thing.
std::string describeApplicationMenu() {
  @autoreleasepool {
    std::string out;
    if (NSApp.mainMenu != nil) {
      describeInto(out, NSApp.mainMenu, 0);
    }
    return out;
  }
}

void setApplicationMenu(const MenuModel &menu, std::function<void(int)> onChosen) {
  @autoreleasepool {
    menuTarget() = [[RnAppKitMenuTargetBar alloc] initWithHandler:std::move(onChosen)];

    const MenuModel &model = menu.empty() ? defaultMenu() : menu;
    NSMenu *bar = [[NSMenu alloc] init];
    for (const MenuItemModel &top : model) {
      // A bare command in a menu bar is not a thing any desktop has.
      if (top.submenu.empty()) {
        continue;
      }
      NSString *label = [NSString stringWithUTF8String:top.label.c_str()];
      NSMenuItem *item = [[NSMenuItem alloc] init];
      // The bar draws the submenu's title rather than the item's, but the item
      // carries it too: that is what the dump reads, and what a test asserting
      // "there is a File menu" can see.
      item.title = label != nil ? label : @"";
      NSMenu *submenu = [[NSMenu alloc] initWithTitle:label != nil ? label : @""];
      addItems(submenu, top.submenu);
      item.submenu = submenu;
      [bar addItem:item];
    }
    NSApp.mainMenu = bar;
  }
}

} // namespace basalt
