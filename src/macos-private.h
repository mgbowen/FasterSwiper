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

constexpr int kCGSEventGesture = 29;
constexpr int kCGSEventDockControl = 30;

constexpr int kIOHIDEventTypeDockSwipe = 23;
constexpr int kCGGestureMotionHorizontal = 1;

constexpr int kGestureBegan = 1;
constexpr int kGestureChanged = 2;
constexpr int kGestureEnded = 4;
constexpr int kGestureCancelled = 8;

} // namespace fasterswiper
