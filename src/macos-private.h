// See ATTRIBUTION.md for information on third-party projects used to create
// this file.

#pragma once

#include <ApplicationServices/ApplicationServices.h>

using SLSSpaceId = size_t;

extern "C" {
int SLSMainConnectionID(void);
SLSSpaceId SLSGetActiveSpace(int cid);
CFArrayRef SLSCopyManagedDisplaySpaces(int cid);
bool SLSManagedDisplayIsAnimating(int cid, CFStringRef display);
int64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef uuid);

enum class CGSEventType {
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

CFStringRef SLSSpaceCopyName(int cid, SLSSpaceId sid);

enum class CGSSpaceMask {
  CGSSpaceIncludesCurrent = 1 << 0, // Dock, Notification Center, etc.
  CGSSpaceIncludesOthers = 1 << 1,  // Expose

  CGSSpaceIncludesUser = 1 << 2, // User controlled spaces
  CGSSpaceIncludesOS = 1 << 3,   // OS X controlled spaces

  CGSSpaceVisible = 1 << 16, // ?

  kCGSCurrentSpacesMask = CGSSpaceIncludesUser | CGSSpaceIncludesCurrent,
  kCGSOtherSpacesMask = CGSSpaceIncludesUser | CGSSpaceIncludesOthers,
  kCGSAllSpacesMask =
      CGSSpaceIncludesUser | CGSSpaceIncludesOthers | CGSSpaceIncludesCurrent,

  kCGSCurrentOSSpacesMask = CGSSpaceIncludesOS | CGSSpaceIncludesCurrent,
  kCGSOtherOSSpacesMask = CGSSpaceIncludesOS | CGSSpaceIncludesOthers,
  kCGSAllOSSpacesMask =
      CGSSpaceIncludesOS | CGSSpaceIncludesOthers | CGSSpaceIncludesCurrent,

  kCGSAllVisibleSpacesMask = CGSSpaceVisible | kCGSAllSpacesMask, // ?
};

CFArrayRef SLSCopySpaces(int cid, CGSSpaceMask type);
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
constexpr CGEventField kCGEventGestureSwipeMask =
    static_cast<CGEventField>(115);
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
constexpr int kCGGestureMotionVertical = 2;

constexpr int kGestureBegan = 1;
constexpr int kGestureChanged = 2;
constexpr int kGestureEnded = 4;
constexpr int kGestureCancelled = 8;

using FixedFP1616 = int32_t;

enum class IOHIDEventType : uint32_t {
  kIOHIDEventTypeVelocity = 9,
  kIOHIDEventTypeFluidTouchGesture = 23,
};

struct __attribute__((packed)) IOHIDSystemQueueElement {
  uint64_t timestamp;
  uint64_t sender_id;
  uint32_t options;
  uint32_t attribute_length;
  uint32_t event_count;
  uint8_t payload[0];
};

static_assert(sizeof(IOHIDSystemQueueElement) == 28,
              "Unexpected sizeof(IOHIDSystemQueueElement)");

struct __attribute__((packed)) IOHIDEventBase {
  uint32_t size;
  IOHIDEventType type;
  uint32_t options;
  uint8_t depth;
  uint8_t reserved[3];
};

static_assert(sizeof(IOHIDEventBase) == 16,
              "Unexpected sizeof(IOHIDEventBase)");

enum class IOHIDSwipeMask : uint32_t {
  kIOHIDSwipeUp = 1,
  kIOHIDSwipeDown = 2,
  kIOHIDSwipeLeft = 4,
  kIOHIDSwipeRight = 8,
};

enum class IOHIDGestureMotion : uint16_t {
  kIOHIDGestureMotionHorizontalX = 1,
  kIOHIDGestureMotionVerticalY = 2,
};

enum class IOHIDGestureFlavor : uint16_t {
  kIOHIDGestureFlavorDockPrimary = 3,
};

struct __attribute__((packed)) IOHIDFluidTouchGestureData {
  IOHIDEventBase base;
  FixedFP1616 position_x;
  FixedFP1616 position_y;
  FixedFP1616 position_z;
  IOHIDSwipeMask swipe_mask;
  IOHIDGestureMotion gesture_motion;
  IOHIDGestureFlavor gesture_flavor;
  FixedFP1616 swipe_progress;
};

static_assert(sizeof(IOHIDFluidTouchGestureData) == 40,
              "Unexpected sizeof(IOHIDFluidTouchGestureData)");

// IOHIDVelocityEventData. Size: 28 bytes.
struct __attribute__((packed)) IOHIDVelocityEventData {
  IOHIDEventBase base;
  FixedFP1616 velocity_x;
  FixedFP1616 velocity_y;
  FixedFP1616 velocity_z;
};

static_assert(sizeof(IOHIDVelocityEventData) == 28,
              "Unexpected sizeof(IOHIDVelocityEventData)");

} // namespace fasterswiper
