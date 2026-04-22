#pragma once

#include <stdint.h>

enum EasingFunctionType : int32_t {
  kEasingFunctionLinear = 0,
  kEasingFunctionEaseOutQuadratic = 1,
  kEasingFunctionEaseOutQuintic = 2,
};

typedef double (*EasingFunctionPtr)(double);

#ifdef __cplusplus
extern "C" {
#endif

EasingFunctionPtr GetEasingFunction(enum EasingFunctionType type);

double EasingFunctionLinear(double t);
double EasingFunctionEaseOutQuadratic(double t);
double EasingFunctionEaseOutQuintic(double t);

#ifdef __cplusplus
}
#endif
