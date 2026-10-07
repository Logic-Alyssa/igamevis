#include "iGameSurfaceVectorsFilter.h"
#include "iGameUnstructuredMesh.h"
#include "iGameArrayObject.h"
#include "iGameAttributeSet.h"
#include <cmath>
#include <iostream>
#include <vector>

IGAME_NAMESPACE_BEGIN

SurfaceVectorsFilter::SurfaceVectorsFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

std::array<float, 3> SurfaceVectorsFilter::ComputeFaceNormal(
    SurfaceMesh::Pointer mesh, igIndex faceId) {
    std::array<float, 3> normal = {0.0f, 0.0f, 0.0f};
    if (!mesh) return normal;

    auto points = mesh->GetPoints();
    auto faces = mesh->GetFaces();
    if (!points || !faces) return normal;

    const igIndex* ids = nullptr;
    int numIds = faces->GetCellIds(faceId, ids);
    if (numIds < 3) return normal;

    // 用第0个顶点作为基准，累加所有三角形的法向量（扇形拆分）
    Point p0 = points->GetPoint(ids[0]);
    for (int i = 2; i < numIds; i++) {
        Point p1 = points->GetPoint(ids[i - 1]);
        Point p2 = points->GetPoint(ids[i]);

        float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
        float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];

        float cx = e1y * e2z - e1z * e2y;
        float cy = e1z * e2x - e1x * e2z;
        float cz = e1x * e2y - e1y * e2x;

        normal[0] += cx;
        normal[1] += cy;
        normal[2] += cz;
    }
    return normal; // 未归一化，长度 ≈ 2*面积
}

bool SurfaceVectorsFilter::ComputePointNormals(
    SurfaceMesh::Pointer mesh,
    std::vector<std::array<float, 3>>& outNormals) {
    if (!mesh) return false;

    auto points = mesh->GetPoints();
    auto faces = mesh->GetFaces();
    if (!points || !faces) return false;

    IGsize numPoints = points->GetNumberOfPoints();
    IGsize numFaces = faces->GetNumberOfCells();

    outNormals.assign(numPoints, {0.0f, 0.0f, 0.0f});

    // 累加每个相邻面的法向量（面积加权，因为 ComputeFaceNormal 返回的是面积叉积）
    for (IGsize f = 0; f < numFaces; f++) {
        auto faceNormal = ComputeFaceNormal(mesh, f);

        const igIndex* ids = nullptr;
        int numIds = faces->GetCellIds(f, ids);
        for (int i = 0; i < numIds; i++) {
            igIndex pid = ids[i];
            if (pid < numPoints) {
                outNormals[pid][0] += faceNormal[0];
                outNormals[pid][1] += faceNormal[1];
                outNormals[pid][2] += faceNormal[2];
            }
        }
    }

    // 归一化
    for (auto& n : outNormals) {
        Normalize(n);
    }

    return true;
}

void SurfaceVectorsFilter::Normalize(std::array<float, 3>& v) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-12f) {
        float invLen = 1.0f / len;
        v[0] *= invLen;
        v[1] *= invLen;
        v[2] *= invLen;
    }
}

bool SurfaceVectorsFilter::Execute() {
    auto input = GetInput(0);
    if (!input) {
        m_Message = "SurfaceVectorsFilter: no input data.";
        return false;
    }

    SurfaceMesh::Pointer surfaceMesh = nullptr;
    IGenum dataType = input->GetDataObjectType();

    if (dataType == IG_SURFACE_MESH) {
        surfaceMesh = DynamicCast<SurfaceMesh>(input);
    } else if (dataType == IG_UNSTRUCTURED_MESH) {
        auto um = DynamicCast<UnstructuredMesh>(input);
        if (um) surfaceMesh = um->TransferToSurfaceMesh();
    } else {
        m_Message = "SurfaceVectorsFilter: only SurfaceMesh and UnstructuredMesh are supported.";
        return false;
    }

    if (!surfaceMesh) {
        m_Message = "SurfaceVectorsFilter: failed to get surface mesh.";
        return false;
    }

    auto attrSet = surfaceMesh->GetAttributeSet();
    if (!attrSet || attrSet->GetNumberOfAttributes() == 0) {
        m_Message = "SurfaceVectorsFilter: input has no attributes.";
        return false;
    }

    // 定位向量属性
    int attrIndex = m_VectorAttrIndex;
    if (attrIndex < 0 && !m_VectorAttrName.empty()) {
        attrIndex = attrSet->GetAttributeIndex(m_VectorAttrName);
    }
    if (attrIndex < 0 || attrIndex >= attrSet->GetNumberOfAttributes()) {
        m_Message = "SurfaceVectorsFilter: vector attribute not found.";
        return false;
    }

    auto& attr = attrSet->GetAttribute(attrIndex);
    if (attr.IsNone()) {
        m_Message = "SurfaceVectorsFilter: attribute is invalid.";
        return false;
    }

    auto vecArray = attr.pointer;
    if (vecArray->GetDimension() < 3) {
        m_Message = "SurfaceVectorsFilter: attribute dimension must be >= 3 (vector).";
        return false;
    }

    IGenum attachmentType = attr.attachmentType;
    IGsize numElements = vecArray->GetNumberOfElements();

    // 创建输出数组
    FloatArray::Pointer outArray = FloatArray::New();
    outArray->SetName(m_OutputAttrName);
    outArray->SetDimension(3);
    outArray->Reserve(numElements);

    std::vector<float> vecBuf(3, 0.0f);

    if (attachmentType == IG_CELL) {
        // ========== 单元向量：每个面用自己的法向量 ==========
        IGsize numFaces = surfaceMesh->GetFaces() ? surfaceMesh->GetFaces()->GetNumberOfCells() : 0;
        if (numElements != numFaces) {
            m_Message = "SurfaceVectorsFilter: cell attribute size mismatch.";
            return false;
        }

        for (IGsize i = 0; i < numFaces; i++) {
            auto normal = ComputeFaceNormal(surfaceMesh, i);
            Normalize(normal);

            vecArray->GetElement(i, vecBuf.data());

            // 点积 = v·n
            float dot = vecBuf[0] * normal[0] + vecBuf[1] * normal[1] + vecBuf[2] * normal[2];

            // v_tangent = v - (v·n) * n
            float tx = vecBuf[0] - dot * normal[0];
            float ty = vecBuf[1] - dot * normal[1];
            float tz = vecBuf[2] - dot * normal[2];

            outArray->AddElement3(tx, ty, tz);
        }

        attrSet->AddAttribute(IG_VECTOR, IG_CELL, outArray);

    } else if (attachmentType == IG_POINT) {
        // ========== 点向量：面积加权平均得到点法向量 ==========
        std::vector<std::array<float, 3>> pointNormals;
        if (!ComputePointNormals(surfaceMesh, pointNormals)) {
            m_Message = "SurfaceVectorsFilter: failed to compute point normals.";
            return false;
        }

        IGsize numPoints = surfaceMesh->GetNumberOfPoints();
        if (numElements != numPoints) {
            m_Message = "SurfaceVectorsFilter: point attribute size mismatch.";
            return false;
        }

        for (IGsize i = 0; i < numPoints; i++) {
            const auto& normal = pointNormals[i];

            vecArray->GetElement(i, vecBuf.data());

            float dot = vecBuf[0] * normal[0] + vecBuf[1] * normal[1] + vecBuf[2] * normal[2];

            float tx = vecBuf[0] - dot * normal[0];
            float ty = vecBuf[1] - dot * normal[1];
            float tz = vecBuf[2] - dot * normal[2];

            outArray->AddElement3(tx, ty, tz);
        }

        attrSet->AddAttribute(IG_VECTOR, IG_POINT, outArray);

    } else {
        m_Message = "SurfaceVectorsFilter: unsupported attribute attachment type.";
        return false;
    }

    attrSet->ForceReConvertToDrawableData();

    std::cout << "[SurfaceVectorsFilter] Projected '" << vecArray->GetName()
              << "' to tangent plane -> '" << m_OutputAttrName << "'" << std::endl;
    std::cout << "  Elements: " << numElements
              << ", Type: " << (attachmentType == IG_POINT ? "point" : "cell")
              << std::endl;

    if (dataType == IG_SURFACE_MESH) {
        SetOutput(surfaceMesh);
    } else {
        SetOutput(input);
    }
    return true;
}

IGAME_NAMESPACE_END
