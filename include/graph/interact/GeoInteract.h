#ifndef GEO_INTERACT_H
#define GEO_INTERACT_H

#include "../GeoGraph.h"
#include "../../../include/graph/GeoFactory.h"
#include "../../../include/plot/plotCall.h"
#include "../../../include/graph/GeoGraph.h"
#include "../../../include/grids/grids.h"
#include <vector>
#include <set>
#include <string>
#include <cmath>



uint32_t CreatePoint_Interact(GeometryGraph& graph);



uint32_t TrySelect_Interact(GeometryGraph& graph, bool is_multi_select);
Vec2 SnapToGrid_Interact(const GeometryGraph& graph, Vec2 world_coord);


void CancelPreview_Intectact(GeometryGraph& graph);
void UpdateMousePos_Interact(GeometryGraph& graph,double x,double y);
#endif // GEO_INTERACT_H
