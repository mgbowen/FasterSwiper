#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

#import <AppKit/AppKit.h>

typedef int CGSConnectionID;
typedef void *CGSNotificationData;
typedef void *CGSNotificationArg;

CG_EXTERN CGSConnectionID CGSMainConnectionID(void);

typedef enum {
  kCGSNotificationWorkspaceChanged = 1401,
  kCGSNotificationTransitionEnded = 1700,
} CGSNotificationType;

typedef void (*CGSNotifyProcPtr)(CGSNotificationType type, void *data,
                                 unsigned int dataLength, void *userData);

CG_EXTERN CGError CGSRegisterNotifyProc(CGSNotifyProcPtr proc,
                                        CGSNotificationType type,
                                        void *userData);

typedef enum {
  kCGSWorkspaceChangedEvent = 1401,
} CGSConnectionNotifyEvent;

typedef void (*CGConnectionNotifyProc)(CGSNotificationType type,
                                       CGSNotificationData notificationData,
                                       size_t dataLength,
                                       CGSNotificationArg userParameter,
                                       CGSConnectionID);

/// Registers a function to receive notifications for connection-level events.
CG_EXTERN CGError CGSRegisterConnectionNotifyProc(
    CGSConnectionID cid, CGConnectionNotifyProc function,
    CGSConnectionNotifyEvent event, void *userData);

static void PrintTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::cout << std::put_time(std::localtime(&time_t_now), "%H:%M:%S") << "."
            << std::setfill('0') << std::setw(3) << ms.count();
}

// Observer class that handles NSWorkspaceActiveSpaceDidChangeNotification.
@interface SpaceChangeObserver : NSObject
@end

@implementation SpaceChangeObserver

- (void)spaceDidChange:(NSNotification *)notification {
  PrintTimestamp();
  std::cout << " [NSWorkspace] Active space changed" << std::endl;
}

@end

static void CGSNotifyCallback(CGSNotificationType type, void *data,
                              unsigned int dataLength, void *userData) {
  PrintTimestamp();
  std::cout << " [CGSNotifyProc] Workspace changed (type=" << type << ")"
            << std::endl;
}

static void CGSConnectionNotifyCallback(CGSNotificationType type,
                                        CGSNotificationData notificationData,
                                        size_t dataLength,
                                        CGSNotificationArg userParameter,
                                        CGSConnectionID cid) {
  PrintTimestamp();
  std::cout << " [CGSConnectionNotifyProc] Workspace changed (type=" << type
            << ")" << std::endl;
}

int main() {
  @autoreleasepool {
    // NSApplication must be initialized for the process to have a window server
    // connection. Without this, NSWorkspace and CGS notifications are never
    // delivered.
    NSApplication *app = [NSApplication sharedApplication];

    // 1. NSWorkspace notification (documented API).
    SpaceChangeObserver *observer = [[SpaceChangeObserver alloc] init];
    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserver:observer
           selector:@selector(spaceDidChange:)
               name:NSWorkspaceActiveSpaceDidChangeNotification
             object:nil];

    // 2. CGS global notification (private API).
    CGSRegisterNotifyProc(CGSNotifyCallback, kCGSNotificationWorkspaceChanged,
                          nullptr);

    // 3. CGS connection-level notification (private API).
    CGSConnectionID cid = CGSMainConnectionID();
    CGSRegisterConnectionNotifyProc(cid, CGSConnectionNotifyCallback,
                                    kCGSWorkspaceChangedEvent, nullptr);

    std::cout << "Listening for space changes (Ctrl+C to quit)..." << std::endl;
    std::cout << "  Registered: NSWorkspaceActiveSpaceDidChangeNotification"
              << std::endl;
    std::cout << "  Registered: CGSRegisterNotifyProc (type 1401)" << std::endl;
    std::cout << "  Registered: CGSRegisterConnectionNotifyProc (type 1401, "
                 "cid="
              << cid << ")" << std::endl;

    // Run the NSApplication event loop. This processes both AppKit
    // notifications and CGS notifications.
    [app run];
  }

  return 0;
}
