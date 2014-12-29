#include <string.h>
#include <maya/MIOStream.h>
#include <math.h>
#include <assert.h>

// Don't include CUDA headers in this file.  CUDA and Maya headers are incompatible due to
// namespace conflicts.
#define NO_CUDA

#include <maya/MPxDeformerNode.h> 
#include <maya/MPxCommand.h>
#include <maya/MPxData.h>
#include <maya/MItGeometry.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMatrixData.h>

#include <maya/MFnDagNode.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnTransform.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnGeometryFilter.h>
#include <maya/MFnPluginData.h>

#include <maya/MArgList.h>
#include <maya/MTypeId.h> 
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MFnMesh.h>

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>

#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MVector.h>
#include <maya/MVectorArray.h>
#include <maya/MMatrix.h>

#include <maya/MDagModifier.h>

#include "maya/maya_helpers.hpp"
#include "maya/maya_data.hpp"

#include "loader_mesh.hpp"
#include "loader_skel.hpp"

#include "Interface.hpp"

#include <algorithm>
#include <map>
using namespace std;

class ImplicitSkinDeformer: public MPxDeformerNode
{
public:
    static MTypeId id;

    PluginInterface pluginInterface;

    virtual ~ImplicitSkinDeformer() { }

    static void *creator() { return new ImplicitSkinDeformer(); }
    static MStatus initialize();

    MStatus deform(MDataBlock &block, MItGeometry &iter, const MMatrix &mat, unsigned int multiIndex);

    static MObject dataAttr;
    static MObject geomMatrixAttr;
    static MObject influenceBindMatrixAttr;
    static MObject influenceMatrixAttr;
};

// XXX: http://help.autodesk.com/view/MAYAUL/2015/ENU/?guid=__cpp_ref_class_m_type_id_html says that
// ADN assigns public blocks of IDs, but nothing says how to request a block without paying for
// a commercial ADN account.  Let's use a value in the devkit sample range, so it's unlikely to conflict,
// and it does, it won't conflict with somebody's internal-use IDs (0-0x7ffff).  At worst, we'll collide
// with a sample or somebody else doing the same thing.
MTypeId ImplicitSkinDeformer::id(0xEA115);
MObject ImplicitSkinDeformer::dataAttr;
MObject ImplicitSkinDeformer::geomMatrixAttr;
MObject ImplicitSkinDeformer::influenceBindMatrixAttr;
MObject ImplicitSkinDeformer::influenceMatrixAttr;

MStatus ImplicitSkinDeformer::initialize()
{
    MStatus status = MStatus::kSuccess;

    MFnMatrixAttribute mAttr;
    geomMatrixAttr = mAttr.create("geomMatrix", "gm");
    addAttribute(geomMatrixAttr);
    attributeAffects(ImplicitSkinDeformer::geomMatrixAttr, ImplicitSkinDeformer::outputGeom);

    influenceBindMatrixAttr = mAttr.create("influenceBindMatrix", "ibm");
    mAttr.setArray(true);
    addAttribute(influenceBindMatrixAttr);
    attributeAffects(ImplicitSkinDeformer::influenceBindMatrixAttr, ImplicitSkinDeformer::outputGeom);

    influenceMatrixAttr = mAttr.create("matrix", "ma");
    mAttr.setArray(true);
    mAttr.setConnectable(true);
    addAttribute(influenceMatrixAttr);
    attributeAffects(ImplicitSkinDeformer::influenceMatrixAttr, ImplicitSkinDeformer::outputGeom);

    return MStatus::kSuccess;
}

MStatus ImplicitSkinDeformer::deform(MDataBlock &dataBlock, MItGeometry &geomIter, const MMatrix &mat, unsigned int multiIndex)
{
// MStatus ImplicitSkinDeformer::compute(const MPlug &plug, MDataBlock &dataBlock)
    MStatus status = MStatus::kSuccess;
        
    // If implicit -setup hasn't been run yet, stop.  XXX: saving/loading
    if(!pluginInterface.is_setup())
        return MStatus::kSuccess;

    // We only support a single input, like skinCluster.
    if(multiIndex > 0)
        MStatus status = MStatus::kSuccess;

    // Get the geomMatrixAttr attribute.
    MMatrix objectToWorldSpaceMat = DagHelpers::readHandle<MMatrix>(dataBlock, geomMatrixAttr, &status);
    if(status != MS::kSuccess) return status;

    MMatrix worldToObjectSpaceMat = objectToWorldSpaceMat.inverse();

    // Get the world transforms the joints had at bind time.
    MArrayDataHandle influenceBindMatrixHandle = dataBlock.inputArrayValue(influenceBindMatrixAttr, &status);
    if(status != MS::kSuccess) return status;

    // Get the world transforms the joints have now.
    MArrayDataHandle influenceMatrixHandle = dataBlock.inputArrayValue(influenceMatrixAttr, &status);
    if(status != MS::kSuccess) return status;

    // Update the skeleton.
    vector<Loader::CpuTransfo> bone_transforms;

    // The root joint is a dummy, and doesn't correspond with a Maya transform.
    bone_transforms.push_back(Loader::CpuTransfo::identity());
    
    for(int i = 0; i < (int) influenceMatrixHandle.elementCount(); ++i)
    {
        // We need to get the change to the joint's transformation compared to when it was bound.
        // Maya gets this by multiplying the current worldMatrix against the joint's bindPreMatrix.
        // However, it's doing that in world space; we need it in object space.
        // XXX: There's probably a way to do this that doesn't require two matrix inversions.  It's
        // probably not worth caching, though.
        MMatrix bindPreMatrixWorldSpace = DagHelpers::readArrayHandleLogicalIndex<MMatrix>(influenceBindMatrixHandle, i, &status); // original inverted world space transform
        MMatrix bindMatrixWorldSpace = bindPreMatrixWorldSpace.inverse();                // original (non-inverted) world space transform
        MMatrix bindMatrixObjectSpace = bindMatrixWorldSpace * worldToObjectSpaceMat;    // original object space transform
        MMatrix bindMatrixObjectSpaceInv = bindMatrixObjectSpace.inverse();              // original inverted object space transform

        MMatrix jointTransformWorldSpace = DagHelpers::readArrayHandleLogicalIndex<MMatrix>(influenceMatrixHandle, i, &status); // current world space transform

        MMatrix currentTransformObjectSpace = jointTransformWorldSpace * worldToObjectSpaceMat; // current object space transform
        MMatrix changeToTransform = bindMatrixObjectSpaceInv * currentTransformObjectSpace; // joint transform relative to bind pose in object space
        
        bone_transforms.push_back(DagHelpers::MMatrixToCpuTransfo(changeToTransform));
    }

    pluginInterface.update_skeleton(bone_transforms);

    // Update the vertex data.  We read all geometry, not just the set (if any) that we're being
    // applied to, so the algorithm can see the whole mesh.
    {
        // Get input.
        MArrayDataHandle inputArray = dataBlock.inputArrayValue(input, &status);
        if(status != MS::kSuccess) return status;

        // Get input[multiIndex].
        MDataHandle inputGeomData = DagHelpers::readArrayHandleLogicalIndex<MDataHandle>(inputArray, multiIndex, &status);
        if(status != MS::kSuccess) return status;

        // Get input[multiIndex].inputGeometry.
        MDataHandle inputGeomDataHandle = inputGeomData.child(inputGeom);

        // Read all geometry.  This geometry has already had the skinCluster deformation applied to it.
        MItGeometry allGeomIter(inputGeomDataHandle, true);
        MPointArray points;
        allGeomIter.allPositions(points, MSpace::kObject);

        // If the geometry doesn't have the correct number of vertices, we can't use it.  This can be
        // caused by a deformer like deleteVertices being added between us and the skinCluster, and the
        // user should bake it (delete non-deformer history).
        // XXX: Is there a way we can tell the user about this?
        // XXX: Will the algorithm allow us to support this, if we give it a whole new mesh with similar
        // topology and call update_base_potential?
        if(points.length() != pluginInterface.expected_vertex_count())
            return MStatus::kSuccess;

        // Set the deformed vertex data.
        vector<Loader::Vertex> input_verts;
        input_verts.reserve(points.length());
        for(int i = 0; i < (int) points.length(); ++i)
            input_verts.push_back(Loader::Vertex((float) points[i].x, (float) points[i].y, (float) points[i].z));
        
        pluginInterface.update_vertices(input_verts);
    }

    // Run the algorithm.  XXX: If we're being applied to a set, can we reduce the work we're doing to
    // just those vertices?
    vector<Loader::Vec3> result_verts;
    pluginInterface.go(result_verts);

    // Copy out the vertices that we were actually asked to process.
    for ( ; !geomIter.isDone(); geomIter.next()) {
        int vertex_index = geomIter.index();

        Loader::Vec3 v = result_verts[vertex_index];
        MPoint pt = MPoint(v.x, v.y, v.z);
        geomIter.setPosition(pt, MSpace::kObject);
    }

    return MStatus::kSuccess;
}



class ImplicitCommand : public MPxCommand
{
public:
    virtual ~ImplicitCommand() { }
    MStatus doIt( const MArgList& );
//    MStatus redoIt();
//    MStatus undoIt();

    MStatus setup(MString nodeName);

    bool isUndoable() const { return false; }
    static void *creator() { return new ImplicitCommand(); }

private:
    MStatus getOnePlugByName(MString nodeName, MPlug &plug);
};

MStatus ImplicitCommand::getOnePlugByName(MString nodeName, MPlug &plug)
{
    MSelectionList slist;
    MStatus status = slist.add(nodeName);
    if(status != MS::kSuccess) return status;

    int matches = slist.length(&status);
    if(status != MS::kSuccess) return status;

    if(matches > 1)
    {
        displayError("Multiple nodes found: " + nodeName);
        return MS::kFailure;
    }

    MPlug implicitPlug;
    return slist.getPlug(0, plug);
}

MStatus ImplicitCommand::setup(MString nodeName)
{
    MStatus status;

    // Get the MPlug for the selected node.
    MPlug implicitPlug;
    status = getOnePlugByName(nodeName, implicitPlug);
    if(status != MS::kSuccess) return status;

    MFnDependencyNode implicitPlugDep(implicitPlug.node(), &status);
    if(status != MS::kSuccess) return status;

    // Verify that this is one of our nodes.
    {
        MTypeId type = implicitPlugDep.typeId(&status);
        if(status != MS::kSuccess) return status;

        if(type != ImplicitSkinDeformer::id)
        {
            displayError("Node not an implicitDeformer: " + nodeName);
            return MS::kFailure;
        }
    }

    ImplicitSkinDeformer *deformer = (ImplicitSkinDeformer *) implicitPlugDep.userNode(&status);
    if(status != MS::kSuccess) return status;

    // Create an MFnGeometryFilter on the ImplicitSkinDeformer.
    MFnGeometryFilter implicitGeometryFilter(implicitPlug.node(), &status);
    if(status != MS::kSuccess) return status;

    // Ask the MFnGeometryFilter for the MDagPath of the output, and pop to get to the transform
    // node.
    MDagPath implicitGeometryOutputDagPath;
    implicitGeometryFilter.getPathAtIndex(0, implicitGeometryOutputDagPath);
    implicitGeometryOutputDagPath.pop();

    // Get the inverse transformation, which is the transformation to go from world space to
    // object space.
    MFnTransform transformNode(implicitGeometryOutputDagPath.node());
    MMatrix objectToWorldSpaceMat = transformNode.transformationMatrix(&status);
    MMatrix worldToObjectSpaceMat = objectToWorldSpaceMat.inverse();

    // Store objectToWorldSpaceMat on geomMatrixAttr.
    DagHelpers::setMatrixPlug(implicitPlug, ImplicitSkinDeformer::geomMatrixAttr, objectToWorldSpaceMat);

    // XXX: We need to declare our dependency on the skinCluster, probably via skinCluster.baseDirty.
    // Find the skinCluster deformer node above the deformer.
    MPlug skinClusterPlug;
    status = DagHelpers::findAncestorDeformer(implicitPlug, MFn::kSkinClusterFilter, skinClusterPlug);
    if(status != MS::kSuccess)
    {
        printf("Couldn't find a skinCluster deformer.  Is the node skinned?\n");
        return status;
    }

    // Load the skeleton.  Set getBindPositions to true so we get the positions the bones
    // had at bind time.  Note that we're still assuming that the current position of the
    // object is the same as bind time.  XXX: Is there anything we can do about that?
    // XXX Use skinCluster.geomMatrix (gm), and mirror it
    Loader::Abs_skeleton skeleton;
    status = MayaData::loadSkeletonFromSkinCluster(skinClusterPlug, skeleton, worldToObjectSpaceMat, true);
    if(status != MS::kSuccess) return status;

    // Get the inputGeometry going into the skinCluster.  This is the mesh before skinning, which
    // we'll use to do initial calculations.  The bind-time positions of the joints we got above
    // should correspond with the pre-skinned geometry.
    MObject inputValue;
    status = DagHelpers::getInputGeometryForSkinClusterPlug(skinClusterPlug, inputValue);
    if(status != MS::kSuccess) return status;
    fprintf(stderr, "inPlug %i\n", inputValue.apiType());


    if(!inputValue.hasFn(MFn::kMesh)) {
        // XXX: only meshes are supported
        return MS::kFailure;
    }

    // For each influence going into the skinCluster's .matrix array, connect it to our .matrix array
    // as well.
    MPlug influenceInfluenceMatrix(implicitPlug.node(), ImplicitSkinDeformer::influenceMatrixAttr);

    {
        MFnDependencyNode skinClusterDep(skinClusterPlug.node());
        
        MPlug skinClusterInputMatrix(skinClusterPlug.node(), ImplicitSkinDeformer::influenceBindMatrixAttr);
        const MObject skinClusterMatrixObject = skinClusterDep.attribute("matrix", &status);
        if(status != MS::kSuccess) return status;

        MPlug skinClusterMatrixArray(skinClusterPlug.node(), skinClusterMatrixObject);
        skinClusterMatrixArray.evaluateNumElements(&status);
        if(status != MS::kSuccess) return status;

        MDGModifier dgModifer;

        for(int i = 0; i < (int) skinClusterMatrixArray.numElements(); ++i)
        {
            MPlug skinClusterMatrixElementPlug = skinClusterMatrixArray.elementByPhysicalIndex(i, &status);
            if(status != MS::kSuccess) return status;

            // XXX: test this if a skinCluster has deleted influences
            MPlugArray plugArray;
            skinClusterMatrixElementPlug.connectedTo(plugArray, true /* asDst */, false /* asSrc */, &status);
            if(status != MS::kSuccess) return status;

            if(plugArray.length() == 0)
                continue;

            // The joint's worldMatrix plug, which is connected to the skinCluster's matrix[n] plug.
            MPlug connectionPlug = plugArray[0];


            // Get the logical index on the skinCluster.matrix array, which we'll mirror.
            int elementLogicalIndex = skinClusterMatrixElementPlug.logicalIndex(&status);
            if(status != MS::kSuccess) return status;

            MPlug matrixElementPlug = influenceInfluenceMatrix.elementByLogicalIndex(elementLogicalIndex, &status);
            if(status != MS::kSuccess) return status;

            status = dgModifer.connect(connectionPlug, matrixElementPlug);
            if(status != MS::kSuccess) return status;
        }

        dgModifer.doIt();
    }

    // Copy bindPreMatrix from the skinCluster to influenceBindMatrix.  This stores the transform for
    // each influence at the time setup was done.
    MPlug influenceBindMatrix(implicitPlug.node(), ImplicitSkinDeformer::influenceBindMatrixAttr);

    {
        MFnDependencyNode skinClusterDep(skinClusterPlug.node());

        const MObject bindPreMatrixObject = skinClusterDep.attribute("bindPreMatrix", &status);
        if(status != MS::kSuccess) return status;


        MPlug bindPreMatrixArray(skinClusterPlug.node(), bindPreMatrixObject);
        bindPreMatrixArray.evaluateNumElements(&status);
        if(status != MS::kSuccess) return status;

        for(int i = 0; i < (int) bindPreMatrixArray.numElements(); ++i)
        {
            MPlug bindPreMatrix = bindPreMatrixArray.elementByPhysicalIndex(i, &status);
            if(status != MS::kSuccess) return status;

            MMatrix bindPreMatrixWorldSpace = DagHelpers::getMatrixFromPlug(bindPreMatrix, &status);
            if(status != MS::kSuccess) return status;

            // Create a MFnMatrixData holding the matrix.
            MFnMatrixData fnMat;
            MObject matObj = fnMat.create(&status);
            if(status != MS::kSuccess) return status;

            // Set the geomMatrixAttr attribute.
            fnMat.set(bindPreMatrixWorldSpace);

            int elementLogicalIndex = bindPreMatrix.logicalIndex(&status);
            if(status != MS::kSuccess) return status;

            MPlug item = influenceBindMatrix.elementByLogicalIndex(elementLogicalIndex, &status);
            if(status != MS::kSuccess) return status;

            status = item.setValue(matObj);
            if(status != MS::kSuccess) return status;
        }
    }

    // Load the input mesh.
    Loader::Abs_mesh mesh;
    status = MayaData::load_mesh(inputValue, mesh);
    if(status != MS::kSuccess) return status;

    deformer->pluginInterface.setup(mesh, skeleton);

    return MS::kSuccess;
}

MStatus ImplicitCommand::doIt(const MArgList &args)
{
    MStatus status;
    for(int i = 0; i < (int) args.length(); ++i)
    {
        if(args.asString(i, &status) == MString("-setup") && MS::kSuccess == status)
        {
            ++i;
            MString nodeName = args.asString(i, &status);
            if(status != MS::kSuccess) return status;

            status = setup(nodeName);

            if(status != MS::kSuccess) {
                displayError(status.errorString());
                return status;
            }
        }
    }
    return MS::kSuccess;
}



MStatus initializePlugin(MObject obj)
{
    MStatus status;

    PluginInterface::init();

    MFnPlugin plugin(obj, "", "1.0", "Any");

    status = plugin.registerNode("implicitSkin", ImplicitSkinDeformer::id, ImplicitSkinDeformer::creator, ImplicitSkinDeformer::initialize, MPxNode::kDeformerNode);
    if(status != MS::kSuccess) return status;

    status = plugin.registerCommand("implicitSkin", ImplicitCommand::creator);
    if(status != MS::kSuccess) return status;

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus status;

    PluginInterface::shutdown();

    MFnPlugin plugin(obj);

    status = plugin.deregisterNode(ImplicitSkinDeformer::id);
    if(status != MS::kSuccess) return status;

    status = plugin.deregisterCommand("implicitSkin");
    if(status != MS::kSuccess) return status;

    return MS::kSuccess;
}
