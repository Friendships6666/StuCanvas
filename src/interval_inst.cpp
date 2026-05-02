
#define STUCANVAS_COMPILING_LIBRARY

#include "../stucanvas/utils/interval.hpp"

namespace StuCanvas {
    template struct Interval<double>;
    template struct IntervalSet<double>;

    template IntervalSet<double> operator+(const IntervalSet<double>&, const IntervalSet<double>&);
    template IntervalSet<double> operator-(const IntervalSet<double>&, const IntervalSet<double>&);
    template IntervalSet<double> operator*(const IntervalSet<double>&, const IntervalSet<double>&);
    template IntervalSet<double> operator/(const IntervalSet<double>&, const IntervalSet<double>&);
    


    template Interval<double> exp2(const Interval<double>&);
    template IntervalSet<double> sin(const IntervalSet<double>&);
    template IntervalSet<double> cos(const IntervalSet<double>&);


    namespace utils {
        template Interval<double> evaluate_circle_implicit(const Interval<double>&, const Interval<double>&, double, double, double);
        template Interval<double> evaluate_sphere_implicit(const Interval<double>&, const Interval<double>&, const Interval<double>&, double, double, double, double);
    }
}