
#ifndef DYNAMICGEOENGINE_GRIDS_H
#define DYNAMICGEOENGINE_GRIDS_H

#include <../include/graph/GeoGraph.h>
double CalculateGridStep(double wpp);


void GenerateCartesianLines(
        std::vector<GridLineData>& buffer,
        std::vector<AxisIntersectionData>* intersection_buffer,
        const ViewState& v,
        uint64_t global_mask,
        double min_w, double max_w, double minor_step, double major_step,
        double ndc_scale, double offset,
        bool horizontal
    );
#endif //DYNAMICGEOENGINE_GRIDS_H