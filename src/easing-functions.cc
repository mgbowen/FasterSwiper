#include "src/easing-functions.h"

EasingFunctionPtr GetEasingFunction(EasingFunctionType type) {
  switch (type) {
  case kEasingFunctionLinear:
    return EasingFunctionLinear;
  case kEasingFunctionEaseOutQuadratic:
    return EasingFunctionEaseOutQuadratic;
  case kEasingFunctionEaseOutQuintic:
    return EasingFunctionEaseOutQuintic;
  }

  return nullptr;
}

double EasingFunctionLinear(double t) { return t; }

double EasingFunctionEaseOutQuadratic(double t) {
  double inv = 1.0 - t;
  return 1.0 - inv * inv;
}

double EasingFunctionEaseOutQuintic(double t) {
  double inv = 1.0 - t;
  return 1.0 - inv * inv * inv * inv * inv;
}
