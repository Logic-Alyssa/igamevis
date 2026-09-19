#ifndef iGameAppendReduceFilter_h
#define iGameAppendReduceFilter_h

#include "iGameFilter.h"
#include "iGameSurfaceMesh.h"
#include "iGamePoints.h"
#include "iGameCellArray.h"
#include "iGameArrayObject.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

IGAME_NAMESPACE_BEGIN

struct MeshPointMap {
    std::vector<igIndex> pointMap;
    igIndex faceOffset{0};
};

class AppendReduceFilter : public Filter {
public:
    I_OBJECT(AppendReduceFilter);
    static Pointer New() { return new AppendReduceFilter; }

    ~AppendReduceFilter() override;

    bool Execute() override;

    void SetMergePoints(bool merge) { m_MergePoints = merge; }
    bool GetMergePoints() const { return m_MergePoints; }

    void SetTolerance(float tol) { m_Tolerance = tol; }
    float GetTolerance() const { return m_Tolerance; }

    void AddInput(DataObject::Pointer data);

    std::string GetMessage() const { return m_Message; }

protected:
    AppendReduceFilter();

private:
    void AppendMeshSimple(SurfaceMesh::Pointer src,
                          Points::Pointer outPoints,
                          CellArray::Pointer outFaces,
                          igIndex& pointOffset,
                          std::vector<igIndex>& pointMap);

    void AppendMeshWithMerge(SurfaceMesh::Pointer src,
                             Points::Pointer outPoints,
                             CellArray::Pointer outFaces,
                             std::unordered_map<int64_t, std::vector<igIndex>>& pointHash,
                             std::vector<igIndex>& pointMap);

    void MergeAttributes(const std::vector<SurfaceMesh::Pointer>& meshes,
                         const std::vector<MeshPointMap>& meshMaps,
                         SurfaceMesh::Pointer outMesh);

    ArrayObject::Pointer CreateArrayByType(IGenum arrayType);

    int CountDegenerateFaces(CellArray::Pointer faces);

    bool m_MergePoints;
    float m_Tolerance;
    std::string m_Message;
};

IGAME_NAMESPACE_END
#endif
