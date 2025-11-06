// pch.h
#ifndef PCH_H
#define PCH_H

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <stack>
#include <fstream>
#include <iomanip>
#include <thread>
#include <utility>
#include <array>
#include <cmath> // For std::isfinite
#include <print>
// Intel TBB Headers
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/combinable.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/task_group.h>

// XSIMD Header
#include <xsimd/xsimd.hpp>

#endif //PCH_H