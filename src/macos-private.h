// See ATTRIBUTION.md for information on third-party projects used to create
// this file.

#pragma once

#include <ApplicationServices/ApplicationServices.h>

extern "C" {
int SLSMainConnectionID(void);
uint64_t SLSGetActiveSpace(int cid);
CFArrayRef SLSCopyManagedDisplaySpaces(int cid);
bool SLSManagedDisplayIsAnimating(int cid, CFStringRef display);
uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef uuid);

enum CGSEventType {
  kCGSWorkspaceWillChange = 1400,
  kCGSWorkspaceDidChange = 1401,
  kCGSWorkspaceWindowIsViewable = 1402,
  kCGSWorkspaceWindowIsNotViewable = 1403,
  kCGSWorkspaceWindowDidMove = 1404,
  kCGSWorkspacePrefsDidChange = 1405,
  kCGSWorkspacesWindowDragDidStart = 1411,
  kCGSWorkspacesWindowDragDidEnd = 1412,
  kCGSWorkspacesWindowDragWillEnd = 1413,
  kCGSWorkspacesShowSpaceForProcess = 1414,
  kCGSWorkspacesWindowDidOrderInOnNonCurrentManagedSpacesOnly = 1415,
  kCGSWorkspacesWindowDidOrderOutOnNonCurrentManagedSpaces = 1416,
};

using CGSNotifyProcPtr = void (*)(CGSEventType type, void *data,
                                  unsigned int dataLength, void *userData);

CGError CGSRegisterNotifyProc(CGSNotifyProcPtr proc, CGSEventType type,
                              void *userData);
CGError CGSRemoveNotifyProc(CGSNotifyProcPtr proc, CGSEventType type,
                            void *userData);
}

namespace fasterswiper {

constexpr CGEventField kCGSEventTypeField = static_cast<CGEventField>(55);
constexpr CGEventField kCGEventGestureHIDType = static_cast<CGEventField>(110);
constexpr CGEventField kCGEventGestureScrollY = static_cast<CGEventField>(119);
constexpr CGEventField kCGEventGestureSwipeMotion =
    static_cast<CGEventField>(123);
constexpr CGEventField kCGEventGestureSwipeProgress =
    static_cast<CGEventField>(124);
constexpr CGEventField kCGEventGestureSwipePositionX =
    static_cast<CGEventField>(125);
constexpr CGEventField kCGEventGestureSwipePositionY =
    static_cast<CGEventField>(126);
constexpr CGEventField kCGEventGestureSwipeVelocityX =
    static_cast<CGEventField>(129);
constexpr CGEventField kCGEventGestureSwipeVelocityY =
    static_cast<CGEventField>(130);
constexpr CGEventField kCGEventGesturePhase = static_cast<CGEventField>(132);
constexpr CGEventField kCGEventScrollGestureFlagBits =
    static_cast<CGEventField>(135);
constexpr CGEventField kCGEventGestureZoomDeltaX =
    static_cast<CGEventField>(139);
constexpr CGEventField kCGEventGestureZoomDeltaY =
    static_cast<CGEventField>(140);

constexpr auto kCGSEventDockControl = static_cast<CGEventType>(30);

constexpr int kIOHIDEventTypeDockSwipe = 23;

constexpr int kCGGestureMotionHorizontal = 1;

constexpr int kGestureBegan = 1;
constexpr int kGestureChanged = 2;
constexpr int kGestureEnded = 4;
constexpr int kGestureCancelled = 8;

} // namespace fasterswiper
