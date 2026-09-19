#include "iGameAppendReduceFilter.h"
#include <cmath>
#include <cstdint>
#include <set>
#include <algorithm>

IGAME_NAMESPACE_BEGIN

AppendReduceFilter::AppendReduceFilter()
    : m_MergePoints(true)
    , m_Tolerance(1e-6f)
{
    this->SetNumberOfInputs(1);
    this->SetNumberOfOutputs(1);
}

AppendReduceFilter::~AppendReduceFilter() = default;

void AppendReduceFilter::AddInput(DataObject::Pointer data) {
    int n = this->GetNumberOfInputs();
    if (n == 1 && this->GetInput(0) == nullptr) {
        this->SetInput(0, data);
    } else {
        this->SetNumberOfInputs(n + 1);
        this->SetInput(n, data);
    }
}

ArrayObject::Pointer AppendReduceFilter::CreateArrayByType(IGenum arrayType) {
    switch (arrayType) {
    case IG_FloatArray:           return FloatArray::New();
    case IG_DoubleArray:          return DoubleArray::New();
    case IG_IntArray:             return IntArray::New();
    case IG_UnsignedIntArray:     return UnsignedIntArray::New();
    case IG_CharArray:            return CharArray::New();
    case IG_UnsignedCharArray:    return UnsignedCharArray::New();
    case IG_ShortArray:           return ShortArray::New();
    case IG_UnsignedShortArray:   return UnsignedShortArray::New();
    case IG_LongLongArray:        return LongLongArray::New();
    case IG_UnsignedLongLongArray: return UnsignedLongLongArray::New();
    default:                      return FloatArray::New();
    }
}

void AppendReduceFilter::AppendMeshSimple(SurfaceMesh::Pointer src,
                                           Points::Pointer outPoints,
                                           CellArray::Pointer outFaces,
                                           igIndex& pointOffset,
                                           std::vector<igIndex>& pointMap) {
    if (!src) return;

    auto srcPoints = src->GetPoints();
    auto srcFaces = src->GetFaces();
    if (!srcPoints) return;

    IGsize numPoints = srcPoints->GetNumberOfPoints();
    pointMap.resize(numPoints);

    for (IGsize i = 0; i < numPoints; i++) {
        const Point& p = srcPoints->GetPoint(i);
        outPoints->AddPoint(p);
        pointMap[i] = pointOffset + (igIndex)i;
    }
    if (!srcFaces) {
        pointOffset += (igIndex)numPoints;
        return;
    }
    IGsize numFaces = srcFaces->GetNumberOfCells();
    for (IGsize f = 0; f < numFaces; f++) {
        const igIndex* ids = nullptr;
        int numIds = srcFaces->GetCellIds(f, ids);

        std::vector<igIndex> adjustedIds(numIds);
        for (int i = 0; i < numIds; i++) {
            adjustedIds[i] = ids[i] + pointOffset;
        }
        outFaces->AddCellIds(adjustedIds.data(), numIds);
    }
    pointOffset += (igIndex)numPoints;
}

void AppendReduceFilter::AppendMeshWithMerge(
    SurfaceMesh::Pointer src,
    Points::Pointer outPoints,
    CellArray::Pointer outFaces,
    std::unordered_map<int64_t, std::vector<igIndex>>& pointHash,
    std::vector<igIndex>& pointMap)
{
    if (!src) return;

    auto srcPoints = src->GetPoints();
    auto srcFaces = src->GetFaces();
    if (!srcPoints) return;

    IGsize numPoints = srcPoints->GetNumberOfPoints();
    pointMap.resize(numPoints);

    float invTol = 1.0f / m_Tolerance;
    float tol2 = m_Tolerance * m_Tolerance;

    for (IGsize i = 0; i < numPoints; i++) {
        const Point& p = srcPoints->GetPoint(i);

        int64_t ix = (int64_t)std::floor(p[0] * invTol);
        int64_t iy = (int64_t)std::floor(p[1] * invTol);
        int64_t iz = (int64_t)std::floor(p[2] * invTol);

        bool found = false;

        // Check 27 neighboring buckets to fix cross-bucket miss
        for (int64_t dx = -1; dx <= 1 && !found; dx++) {
            for (int64_t dy = -1; dy <= 1 && !found; dy++) {
                for (int64_t dz = -1; dz <= 1 && !found; dz++) {
                    int64_t nx = ix + dx;
                    int64_t ny = iy + dy;
                    int64_t nz = iz + dz;
                    int64_t key = (nx * 73856093LL) ^ (ny * 19349663LL) ^ (nz * 83492791LL);

                    auto it = pointHash.find(key);
                    if (it == pointHash.end()) continue;

                    for (igIndex existingIdx : it->second) {
                        const Point& existing = outPoints->GetPoint(existingIdx);
                        float ddx = p[0] - existing[0];
                        float ddy = p[1] - existing[1];
                        float ddz = p[2] - existing[2];
                        if (ddx * ddx + ddy * ddy + ddz * ddz < tol2) {
                            pointMap[i] = existingIdx;
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        if (!found) {
            igIndex newIdx = (igIndex)outPoints->AddPoint(p);
            int64_t key = (ix * 73856093LL) ^ (iy * 19349663LL) ^ (iz * 83492791LL);
            pointHash[key].push_back(newIdx);
            pointMap[i] = newIdx;
        }
    }

    if (!srcFaces) return;

    IGsize numFaces = srcFaces->GetNumberOfCells();
    for (IGsize f = 0; f < numFaces; f++) {
        const igIndex* ids = nullptr;
        int numIds = srcFaces->GetCellIds(f, ids);

        std::vector<igIndex> mappedIds(numIds);
        for (int i = 0; i < numIds; i++) {
            mappedIds[i] = pointMap[ids[i]];
        }
        outFaces->AddCellIds(mappedIds.data(), numIds);
    }
}

void AppendReduceFilter::MergeAttributes(
    const std::vector<SurfaceMesh::Pointer>& meshes,
    const std::vector<MeshPointMap>& meshMaps,
    SurfaceMesh::Pointer outMesh)
{
    if (meshes.empty() || meshMaps.empty()) return;

    auto outAttrSet = outMesh->GetAttributeSet();
    if (!outAttrSet) return;

    struct AttrKey {
        std::string name;
        IGenum type;
        IGenum attachmentType;
        IGenum arrayType;
        int dimension;
        bool operator<(const AttrKey& o) const {
            if (name != o.name) return name < o.name;
            if (type != o.type) return type < o.type;
            if (attachmentType != o.attachmentType) return attachmentType < o.attachmentType;
            if (arrayType != o.arrayType) return arrayType < o.arrayType;
            return dimension < o.dimension;
        }
    };

    // Count attributes across all meshes
    std::map<AttrKey, int> attrCount;
    for (auto& mesh : meshes) {
        if (!mesh) continue;
        auto attrSet = mesh->GetAttributeSet();
        if (!attrSet) continue;
        std::set<AttrKey> seen;
        for (int i = 0; i < attrSet->GetNumberOfAttributes(); i++) {
            auto& attr = attrSet->GetAttribute(i);
            if (attr.IsNone()) continue;
            AttrKey key{
                attr.pointer->GetName(),
                attr.type,
                attr.attachmentType,
                attr.pointer->GetArrayType(),
                attr.pointer->GetDimension()
            };
            seen.insert(key);
        }
        for (auto& k : seen) {
            attrCount[k]++;
        }
    }

    IGsize numOutPoints = outMesh->GetNumberOfPoints();
    IGsize numOutFaces = outMesh->GetFaces() ? outMesh->GetFaces()->GetNumberOfCells() : 0;

    int mergedCount = 0;
    for (auto& [key, count] : attrCount) {
        if (count < (int)meshes.size()) continue;

        // Create output array preserving original type
        ArrayObject::Pointer outArray = CreateArrayByType(key.arrayType);
        outArray->SetName(key.name);
        outArray->SetDimension(key.dimension);

        IGsize numElements = (key.attachmentType == IG_POINT) ? numOutPoints : numOutFaces;
        outArray->Resize(numElements);

        // Track which output elements have been filled (first input wins)
        std::vector<bool> filled(numElements, false);

        std::vector<float> elemBufF(16, 0.0f);
        std::vector<double> elemBufD(16, 0.0);
        std::vector<int> elemBufI(16, 0);

        for (size_t m = 0; m < meshes.size(); m++) {
            if (!meshes[m]) continue;
            auto attrSet = meshes[m]->GetAttributeSet();
            if (!attrSet) continue;
            auto& attr = attrSet->GetAttribute(key.name, key.type);
            if (attr.IsNone()) continue;

            int srcDim = attr.pointer->GetDimension();
            IGsize srcNum = attr.pointer->GetNumberOfElements();

            if (key.attachmentType == IG_POINT) {
                const auto& pmap = meshMaps[m].pointMap;
                for (IGsize i = 0; i < srcNum && i < pmap.size(); i++) {
                    igIndex outIdx = pmap[i];
                    if (outIdx < 0 || outIdx >= (igIndex)numElements) continue;
                    if (filled[outIdx]) continue;  // first input wins

                    attr.pointer->GetElement(i, elemBufF.data());
                    attr.pointer->GetElement(i, elemBufD.data());
                    attr.pointer->GetElement(i, elemBufI.data());

                    outArray->SetElement(outIdx, elemBufF.data());
                    filled[outIdx] = true;
                }
            } else {
                igIndex faceOff = meshMaps[m].faceOffset;
                for (IGsize i = 0; i < srcNum; i++) {
                    igIndex outIdx = faceOff + (igIndex)i;
                    if (outIdx < 0 || outIdx >= (igIndex)numElements) continue;
                    if (filled[outIdx]) continue;

                    attr.pointer->GetElement(i, elemBufF.data());
                    attr.pointer->GetElement(i, elemBufD.data());
                    attr.pointer->GetElement(i, elemBufI.data());

                    outArray->SetElement(outIdx, elemBufF.data());
                    filled[outIdx] = true;
                }
            }
        }

        outAttrSet->AddAttribute(key.type, key.attachmentType, outArray);
        mergedCount++;
    }
}

int AppendReduceFilter::CountDegenerateFaces(CellArray::Pointer faces) {
    if (!faces) return 0;
    int degenerate = 0;
    IGsize numFaces = faces->GetNumberOfCells();
    for (IGsize f = 0; f < numFaces; f++) {
        const igIndex* ids = nullptr;
        int numIds = faces->GetCellIds(f, ids);
        if (numIds < 3) {
            degenerate++;
            continue;
        }
        std::unordered_set<igIndex> uniqueIds;
        for (int i = 0; i < numIds; i++) {
            uniqueIds.insert(ids[i]);
        }
        if ((int)uniqueIds.size() < numIds) {
            degenerate++;
        }
    }
    return degenerate;
}

bool AppendReduceFilter::Execute() {
    // Only process explicitly added inputs, do NOT auto-collect from scene
    std::vector<SurfaceMesh::Pointer> meshes;
    int inputCount = this->GetNumberOfInputs();

    for (int i = 0; i < inputCount; i++) {
        auto input = this->GetInput(i);
        if (!input) continue;

        IGenum type = input->GetDataObjectType();

        if (type == IG_SURFACE_MESH) {
            auto mesh = DynamicCast<SurfaceMesh>(input);
            if (mesh) meshes.push_back(mesh);
        } else if (type == IG_VOLUME_MESH || type == IG_UNSTRUCTURED_MESH ||
                   type == IG_STRUCTURED_MESH) {
            m_Message = "AppendReduceFilter: input " + std::to_string(i) +
                " is not a SurfaceMesh. Volume/Unstructured meshes are not supported. "
                "Please extract surface first using ConvertToSurfaceMeshFilter.";
            return false;
        } else if (type == IG_DRAW_OBJECT || type == IG_COMPOSITE_DATA_OBJECT) {
            m_Message = "AppendReduceFilter: input " + std::to_string(i) +
                " is a DrawObject/CompositeDataObject. Please add individual SurfaceMesh "
                "objects via AddInput() instead.";
            return false;
        } else {
            m_Message = "AppendReduceFilter: input " + std::to_string(i) +
                " has unsupported type " + std::to_string(type);
            return false;
        }
    }

    if (meshes.empty()) {
        m_Message = "AppendReduceFilter: no valid SurfaceMesh inputs.";
        return false;
    }

    // Print input statistics
    std::cout << "[AppendReduceFilter] Input statistics:" << std::endl;
    std::cout << "  Mesh count: " << meshes.size() << std::endl;
    IGsize totalInPoints = 0, totalInFaces = 0;
    for (size_t i = 0; i < meshes.size(); i++) {
        IGsize nPts = meshes[i]->GetPoints() ? meshes[i]->GetPoints()->GetNumberOfPoints() : 0;
        IGsize nFaces = meshes[i]->GetFaces() ? meshes[i]->GetFaces()->GetNumberOfCells() : 0;
        totalInPoints += nPts;
        totalInFaces += nFaces;
        std::cout << "  Mesh[" << i << "]: " << nPts << " points, " << nFaces << " faces" << std::endl;
    }
    std::cout << "  Total input: " << totalInPoints << " points, " << totalInFaces << " faces" << std::endl;

    auto outMesh = SurfaceMesh::New();
    auto outPoints = Points::New();
    auto outFaces = CellArray::New();

    std::vector<MeshPointMap> meshMaps(meshes.size());

    if (m_MergePoints) {
        std::unordered_map<int64_t, std::vector<igIndex>> pointHash;
        pointHash.reserve(1024);

        for (size_t i = 0; i < meshes.size(); i++) {
            AppendMeshWithMerge(meshes[i], outPoints, outFaces, pointHash, meshMaps[i].pointMap);
        }
    } else {
        igIndex pointOffset = 0;
        for (size_t i = 0; i < meshes.size(); i++) {
            AppendMeshSimple(meshes[i], outPoints, outFaces, pointOffset, meshMaps[i].pointMap);
        }
    }

    igIndex faceOffset = 0;
    for (size_t i = 0; i < meshes.size(); i++) {
        meshMaps[i].faceOffset = faceOffset;
        auto fcs = meshes[i]->GetFaces();
        if (fcs) {
            faceOffset += (igIndex)fcs->GetNumberOfCells();
        }
    }

    outMesh->SetPoints(outPoints);
    outMesh->SetFaces(outFaces);
    outMesh->RequestEditStatus();
    outMesh->Modified();

    MergeAttributes(meshes, meshMaps, outMesh);

    auto outAttrSet = outMesh->GetAttributeSet();
    if (outAttrSet && outAttrSet->GetNumberOfAttributes() > 0) {
        outAttrSet->ForceReConvertToDrawableData();
    }

    // Check for degenerate faces
    int degenerateCount = CountDegenerateFaces(outFaces);
    if (degenerateCount > 0) {
        std::cout << "[AppendReduceFilter] WARNING: " << degenerateCount
                  << " degenerate face(s) detected after merge." << std::endl;
    }

    // Print output statistics
    std::cout << "[AppendReduceFilter] Output statistics:" << std::endl;
    std::cout << "  Points: " << outPoints->GetNumberOfPoints() << std::endl;
    std::cout << "  Faces: " << outFaces->GetNumberOfCells() << std::endl;
    if (m_MergePoints) {
        IGsize mergedPoints = totalInPoints - outPoints->GetNumberOfPoints();
        std::cout << "  Merged points: " << mergedPoints << std::endl;
    }
    if (outAttrSet) {
        std::cout << "  Attributes: " << outAttrSet->GetNumberOfAttributes() << std::endl;
    }
    if (degenerateCount > 0) {
        std::cout << "  Degenerate faces: " << degenerateCount << std::endl;
    }

    this->SetOutput(outMesh);
    return true;
}

IGAME_NAMESPACE_END
