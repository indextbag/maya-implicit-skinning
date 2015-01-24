#include "marching_cubes.hpp"
#include "tables.h"

namespace MarchingCubes
{
    struct GridCell {
        Point_cu p[8];
        Point_cu c[8];
        float val[8];
    };

    MeshGeomVertex VertexLerp(const float isoLevel, const GridCell& cell, const int i1, const int i2) {
        // Linearly interpolate the position where an isosurface cuts
        // an edge between two vertices, each with their own scalar value
        MeshGeomVertex res;
        if(fabsf(isoLevel - cell.val[i1]) < 0.00001f) {
            res.pos = cell.p[i1];
            res.col = cell.c[i1];
            return res;
        }
        if(fabsf(isoLevel - cell.val[i2]) < 0.00001f) {
            res.pos = cell.p[i2];
            res.col = cell.c[i2];
            return res;
        }
    
        if(fabsf(cell.val[i1] - cell.val[i2]) < 0.00001f) {
            res.pos = cell.p[i1];
            res.col = cell.c[i1];
            return res;
        }

        float mu = (isoLevel - cell.val[i1]) / (cell.val[i2] - cell.val[i1]);

        res.pos = cell.p[i1] + (cell.p[i2] - cell.p[i1])*mu;
        res.col = cell.c[i1] + (cell.c[i2] - cell.c[i1])*mu;

        return res;
    }

    void polygonize(const GridCell &grid, MeshGeom &geom)
    {
        const float isoLevel = 0.5f;

        // Determine the index into the edge table which
        // tells us which vertices are inside of the surface
        int cubeIndex = 0;
        for(int i = 0; i < 8; ++i)
            if(grid.val[i] < isoLevel) cubeIndex |= 1<<i;

        // Cube is entirely in/out of the surface
        if(edgeTable[cubeIndex] == 0)
            return;

        // Find the vertices where the surface intersects the cube
        MeshGeomVertex vertList[12];
        if(edgeTable[cubeIndex] & (1<<0))   vertList[0] = VertexLerp(isoLevel, grid, 0, 1);
        if(edgeTable[cubeIndex] & (1<<1))   vertList[1] = VertexLerp(isoLevel, grid, 1, 2);
        if(edgeTable[cubeIndex] & (1<<2))   vertList[2] = VertexLerp(isoLevel, grid, 2, 3);
        if(edgeTable[cubeIndex] & (1<<3))   vertList[3] = VertexLerp(isoLevel, grid, 3, 0);
        if(edgeTable[cubeIndex] & (1<<4))   vertList[4] = VertexLerp(isoLevel, grid, 4, 5);
        if(edgeTable[cubeIndex] & (1<<5))   vertList[5] = VertexLerp(isoLevel, grid, 5, 6);
        if(edgeTable[cubeIndex] & (1<<6))   vertList[6] = VertexLerp(isoLevel, grid, 6, 7);
        if(edgeTable[cubeIndex] & (1<<7))   vertList[7] = VertexLerp(isoLevel, grid, 7, 4);
        if(edgeTable[cubeIndex] & (1<<8))   vertList[8] = VertexLerp(isoLevel, grid, 0, 4);
        if(edgeTable[cubeIndex] & (1<<9))   vertList[9] = VertexLerp(isoLevel, grid, 1, 5);
        if(edgeTable[cubeIndex] & (1<<10)) vertList[10] = VertexLerp(isoLevel, grid, 2, 6);
        if(edgeTable[cubeIndex] & (1<<11)) vertList[11] = VertexLerp(isoLevel, grid, 3, 7);

        // Create the triangles.
        for(int i = 0; triTable[cubeIndex][i] != -1; i += 3) {
            geom.vertices.push_back(vertList[triTable[cubeIndex][i+0]]);
            geom.vertices.push_back(vertList[triTable[cubeIndex][i+1]]);
            geom.vertices.push_back(vertList[triTable[cubeIndex][i+2]]);
        }
    }

}

void MarchingCubes::compute_surface(MeshGeom &geom, const Bone *bone, const Skeleton *skel)
{
    OBBox_cu obbox = bone->get_obbox(true);

    // Get the set of all of the bounding boxes in the skeleton.  These may overlap.
    std::vector<OBBox_cu> bbox_list;
    for(Bone::Id bone_id: skel->get_bone_ids())
        bbox_list.push_back(bone->get_obbox());

    // set the size of the grid cells, and the amount of cells per side
    int gridRes = 16;

    const HermiteRBF &hrbf = bone->get_hrbf();

    Point_cu deltas[8];
    deltas[0] = Point_cu(0, 0, 0);
    deltas[1] = Point_cu(1, 0, 0);
    deltas[2] = Point_cu(1, 1, 0);
    deltas[3] = Point_cu(0, 1, 0);
    deltas[4] = Point_cu(0, 0, 1);
    deltas[5] = Point_cu(1, 0, 1);
    deltas[6] = Point_cu(1, 1, 1);
    deltas[7] = Point_cu(0, 1, 1);

    for(OBBox_cu obbox: bbox_list)
    {
        BBox_cu bbox = obbox._bb;

        float deltaX = (bbox.pmax.x - bbox.pmin.x) / gridRes;
        float deltaY = (bbox.pmax.y - bbox.pmin.y) / gridRes;
        float deltaZ = (bbox.pmax.z - bbox.pmin.z) / gridRes;

        Point_cu delta(deltaX, deltaY, deltaZ);
        delta = obbox._tr * delta;

        for(float px = bbox.pmin.x; px < bbox.pmax.x; px += deltaX) {
            for(float py = bbox.pmin.y; py < bbox.pmax.y; py += deltaY) {
                for(float pz = bbox.pmin.z; pz < bbox.pmax.z; pz += deltaZ) {
                    Point_cu p(px, py, pz);
                    p = obbox._tr * p;

                    GridCell cell;
                    for(int i = 0; i < 8; i++)
                    {
                        Point_cu &c = cell.p[i];
                        c = p + delta*deltas[i];

                        Vec3_cu gf;
                        // XXX: bring back hrbf.f()? we don't need the gradient
                        cell.val[i] = hrbf.fngf(gf, Point_cu((float) c.x, (float) c.y, (float) c.z));
                    }

                    // generate triangles from this cell
                    polygonize(cell, geom);
                }
            }
        }
    }
}

/* 
    ================================================================================
    Copyright (c) 2010, Jose Esteve. http://www.joesfer.com
    All rights reserved. 

    Redistribution and use in source and binary forms, with or without modification, 
    are permitted provided that the following conditions are met: 

    * Redistributions of source code must retain the above copyright notice, this 
      list of conditions and the following disclaimer. 
    
    * Redistributions in binary form must reproduce the above copyright notice, 
      this list of conditions and the following disclaimer in the documentation 
      and/or other materials provided with the distribution. 
    
    * Neither the name of the organization nor the names of its contributors may 
      be used to endorse or promote products derived from this software without 
      specific prior written permission. 

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR 
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
    ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
    ================================================================================
*/