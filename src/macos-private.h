// clang-format off

//
// Certain portions of this file are pulled from Hammerspoon, located at
// https://github.com/Hammerspoon/hammerspoon. Its license is reproduced below:
//
// The MIT License (MIT)
//
// Copyright (c) 2014-2025 [Various Contributors](https://github.com/Hammerspoon/hammerspoon/graphs/contributors)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Certain portions of this file are pulled from InstantSpaceSwitcher, located at
// https://github.com/jurplel/InstantSpaceSwitcher. Its license is reproduced
// below:
//
// MIT License
//
// Copyright (c) 2026 jurplel
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// clang-format on

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
