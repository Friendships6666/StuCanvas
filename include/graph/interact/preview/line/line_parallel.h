//
// Created by hp on 2026/2/1.
//

#ifndef DYNAMICGEOENGINE_LINE_PARALLEL_H
#define DYNAMICGEOENGINE_LINE_PARALLEL_H
#include "../include/graph/interact/GeoInteract.h"
#include "../include/plot/plotLine.h"
#include "../include/graph/GeoFactory.h"
uint32_t InitParallelLine_Interact(GeometryGraph& graph);
void PreviewParallelLine_Intertact(GeometryGraph& graph);
uint32_t InitParallelLine_PathB_Step2_Interact(GeometryGraph& graph);
uint32_t EndParallelLine_Interact(GeometryGraph& graph);

#endif //DYNAMICGEOENGINE_LINE_PARALLEL_H