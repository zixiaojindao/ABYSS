#include "localmath.h"
#include <cmath>

inline double roundf(double x)
{
	return (x - floor(x)) > 0.5? ceil(x):floor(x);
}