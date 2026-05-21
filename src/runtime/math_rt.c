#include "duxrt.h"
#include <math.h>
#include <stdlib.h>

double  duxrt_math_sqrt(double x)           { return sqrt(x); }
double  duxrt_math_pow(double x, double y)  { return pow(x, y); }
double  duxrt_math_floor(double x)          { return floor(x); }
double  duxrt_math_ceil(double x)           { return ceil(x); }
double  duxrt_math_abs_d(double x)          { return fabs(x); }
int64_t duxrt_math_abs_i(int64_t x)        { return x < 0 ? -x : x; }
double  duxrt_math_min_d(double a, double b){ return a < b ? a : b; }
double  duxrt_math_max_d(double a, double b){ return a > b ? a : b; }
int64_t duxrt_math_min_i(int64_t a, int64_t b){ return a < b ? a : b; }
int64_t duxrt_math_max_i(int64_t a, int64_t b){ return a > b ? a : b; }
double  duxrt_math_log(double x)            { return log(x); }
double  duxrt_math_log2(double x)           { return log2(x); }
double  duxrt_math_sin(double x)            { return sin(x); }
double  duxrt_math_cos(double x)            { return cos(x); }
