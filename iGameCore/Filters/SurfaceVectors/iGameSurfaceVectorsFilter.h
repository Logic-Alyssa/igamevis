#ifndef iGameSurfaceVectorsFilter_h
#define iGameSurfaceVectorsFilter_h

#include "iGameFilter.h"
#include "iGameSurfaceMesh.h"
#include "iGameArrayObject.h"

#include <string>
#include <vector>
#include <array>

IGAME_NAMESPACE_BEGIN

class UnstructuredMesh;

class SurfaceVectorsFilter : public Filter {
public:
    I_OBJECT(SurfaceVectorsFilter);
    static Pointer New() { return new SurfaceVectorsFilter; }

    ~SurfaceVectorsFilter() override = default;

    bool Execute() override;

    void SetVectorAttributeByName(const std::string& name) { m_VectorAttrName = name; }
    void SetVectorAttributeByIndex(int index) { m_VectorAttrIndex = index; }
    void SetOutputAttributeName(const std::string& name) { m_OutputAttrName = name; }
    std::string GetOutputAttributeName() const { return m_OutputAttrName; }

    std::string GetMessage() const { return m_Message; }

protected:
    SurfaceVectorsFilter();

private:
    static std::array<float, 3> ComputeFaceNormal(SurfaceMesh::Pointer mesh, igIndex faceId);
    static bool ComputePointNormals(SurfaceMesh::Pointer mesh,
                                    std::vector<std::array<float, 3>>& outNormals);
    static void Normalize(std::array<float, 3>& v);

    std::string m_VectorAttrName;
    int         m_VectorAttrIndex{-1};
    std::string m_OutputAttrName{"TangentVectors"};
    std::string m_Message;
};

IGAME_NAMESPACE_END
#endif
