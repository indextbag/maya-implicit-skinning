#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#include <vector>

#include "bone.hpp"
#include "skeleton.hpp"


struct MeshGeomVertex {
    Point_cu pos;
    Vec3_cu normal;
    Point_cu col;
};

class MeshGeom
{
public:
//    MFloatPointArray positions;
//    MColorArray colors;
    std::vector<MeshGeomVertex> vertices;
};

namespace MarchingCubes
{
    // Compute the geometry to preview the given skeleton, and append it to meshGeom.
    void compute_surface(MeshGeom &geom, const Skeleton *skel, float isoLevel = 0.5f);
}

#endif

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
