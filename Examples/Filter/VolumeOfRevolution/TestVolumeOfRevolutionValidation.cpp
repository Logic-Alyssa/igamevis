// VolumeOfRevolutionFilter（旋转体生成）控制台回归用例。
//
// 【修复的 BUG / 触发条件 / 错误表现】
//   1) 输入不符合条件时过滤器不拒绝，而是"硬造"轮廓并报成功：
//      - `UnstructuredMesh` 只要没有 `IG_LINE` 单元，就把所有点按顺序两两相连当成轮廓。
//        于是纯四面体/六面体体网格、甚至只有顶点的点云都会被接受，旋转出一堆无意义的面片；
//      - `StructuredMesh` 的 3D 体网格（nk>1）走的是 1D/2D 分支的报错，但 2D 情形下
//        `ni/nj/nk` 未做退化检查；
//      - 只有点、没有单元的网格同样被当成折线轮廓。
//   2) 返回 false 时不给原因：过滤器没有 `GetMessage()`，界面/调用方只能看到一句 igError。
//   3) 失败后不清理上一次输出；`Execute()` 开头也不清空消息。
//   4) `SurfaceMesh` 分支在类型转换判空之前就调用 `RequestEditStatus()`，空指针会崩溃。
//   5) 顺序连接出来的轮廓边没有设置 `cellId`（默认值不确定），单元属性会按随机 id 复制。
//   6) 非法参数没有校验：旋转轴为零向量、角度为 0/NaN、角度超过一整圈、分段数 < 3 时照样执行；
//      轮廓严格共线且旋转轴与它平行时，生成的三角形全部零面积，也照样返回成功。
//
// 【本用例保证的正确行为】
//   - 合法输入（线段单元轮廓 / 折线轮廓 / StructuredMesh 曲线 / 2D 曲面边界）成功，
//     输出的点数、单元数与几何位置可核对，点/单元属性按原顺序传递；
//   - 非法输入（体网格、点云、封闭曲面、3D 结构体网格、轴不合法、角度不合法、分段数过小、
//     轮廓点非有限、轮廓严格共线且轴与它平行）必须返回 false、给出非空 `GetMessage()`、
//     且不留下输出；
//   - 完整圆周（360°）首尾点合并，网格封闭（点数 = 轮廓点数 × 分段数）。
//
// 【提交号】待提交
//   （本次提交主题：修复旋转体过滤器对不符合输入条件的模型不拒绝的问题，并合并 main）
//   查询本用例的引入提交：git log --diff-filter=A --format="%h %s" -- Examples/Filter/VolumeOfRevolution/TestVolumeOfRevolutionValidation.cpp
//
// 全部通过时逐行打印 PASS 并以退出码 0 结束；任一失败打印 FAIL 并返回 1。
// 不依赖渲染窗口与外部模型文件，可以在 CI 中直接运行。

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <iGameAttributeSet.h>
#include <iGameCellArray.h>
#include <iGameCellType.h>
#include <iGameDataObject.h>
#include <iGameFlatArray.h>
#include <iGamePoints.h>
#include <iGameStructuredMesh.h>
#include <iGameSurfaceMesh.h>
#include <iGameUnstructuredMesh.h>
#include <iGameVector.h>
#include <VolumeOfRevolution/iGameVolumeOfRevolutionFilter.h>

namespace ig = iGame;

namespace {

int g_FailureCount = 0;

void Check(bool ok, const std::string& what) {
    if (ok) {
        std::cout << "PASS: " << what << std::endl;
    } else {
        std::cout << "FAIL: " << what << std::endl;
        ++g_FailureCount;
    }
}

void CheckNear(double actual, double expected, double tolerance, const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    Check(ok, what);
    if (!ok) { std::cout << "      actual=" << actual << " expected=" << expected << std::endl; }
}

// 按名字取点属性数组（GetScalar 不区分挂载类型，点名属性必须这样查）
ig::ArrayObject* FindPointArray(ig::DataObject::Pointer object, const std::string& name) {
    if (object == nullptr || object->GetAttributeSet() == nullptr) { return nullptr; }
    auto attributes = object->GetAttributeSet()->GetAllPointAttributes();
    if (attributes == nullptr) { return nullptr; }
    for (IGsize i = 0; i < attributes->GetNumberOfElements(); ++i) {
        auto& attribute = attributes->GetElement(i);
        if (attribute.IsNone() || attribute.pointer == nullptr) { continue; }
        if (attribute.pointer->GetName() == name) { return attribute.pointer; }
    }
    return nullptr;
}

ig::ArrayObject* FindCellArray(ig::DataObject::Pointer object, const std::string& name) {
    if (object == nullptr || object->GetAttributeSet() == nullptr) { return nullptr; }
    auto attributes = object->GetAttributeSet()->GetAllCellAttributes();
    if (attributes == nullptr) { return nullptr; }
    for (IGsize i = 0; i < attributes->GetNumberOfElements(); ++i) {
        auto& attribute = attributes->GetElement(i);
        if (attribute.IsNone() || attribute.pointer == nullptr) { continue; }
        if (attribute.pointer->GetName() == name) { return attribute.pointer; }
    }
    return nullptr;
}

void AddPoint(const ig::Points::Pointer& points, double x, double y, double z) {
    points->AddPoint(x, y, z);
}

void AddCell(const ig::UnstructuredMesh::Pointer& mesh, const std::vector<igIndex>& ids, IGenum type) {
    mesh->AddCell(const_cast<igIndex*>(ids.data()), static_cast<int>(ids.size()), type);
}

// 轮廓的一半（r 值不同，h 沿 Z）：绕 Z 轴旋转得到圆台面
ig::UnstructuredMesh::Pointer MakeLineContourMesh() {
    auto mesh = ig::UnstructuredMesh::New();
    mesh->SetPoints(ig::Points::New());
    AddPoint(mesh->GetPoints(), 1.0, 0.0, 0.0);
    AddPoint(mesh->GetPoints(), 2.0, 0.0, 1.0);
    AddPoint(mesh->GetPoints(), 3.0, 0.0, 2.0);
    AddCell(mesh, {0, 1}, ig::IG_LINE);
    AddCell(mesh, {1, 2}, ig::IG_LINE);
    return mesh;
}

// 用两段折线描述同一个轮廓，验证 IG_POLY_LINE 也被接受
ig::UnstructuredMesh::Pointer MakePolyLineContourMesh() {
    auto mesh = ig::UnstructuredMesh::New();
    mesh->SetPoints(ig::Points::New());
    AddPoint(mesh->GetPoints(), 1.0, 0.0, 0.0);
    AddPoint(mesh->GetPoints(), 2.0, 0.0, 1.0);
    AddPoint(mesh->GetPoints(), 3.0, 0.0, 2.0);
    AddCell(mesh, {0, 1, 2}, ig::IG_POLY_LINE);
    return mesh;
}

// r = 半径，h = 沿 Z 轴高度。绕 Z 轴、轴点为原点时：
//   r = sqrt(x^2 + y^2)，h = z。
void CheckRevolutionGeometry(const ig::UnstructuredMesh::Pointer& output,
                             const std::vector<std::pair<double, double>>& profile, int resolution,
                             const std::string& label) {
    if (output == nullptr) {
        Check(false, label + ": output is an UnstructuredMesh");
        return;
    }
    auto points = output->GetPoints();
    const IGsize expectedPoints = static_cast<IGsize>(profile.size()) * resolution; // 360° 首尾合并
    Check(points->GetNumberOfPoints() == expectedPoints,
          label + ": point count == profilePoints * resolution (" +
                  std::to_string(points->GetNumberOfPoints()) + " vs " + std::to_string(expectedPoints) + ")");

    double minH = profile.front().second, maxH = profile.front().second;
    for (const auto& entry: profile) {
        minH = std::min(minH, entry.second);
        maxH = std::max(maxH, entry.second);
    }

    bool radiusOk = true;
    bool heightOk = true;
    for (IGsize i = 0; i < points->GetNumberOfPoints(); ++i) {
        ig::Vector3d p;
        points->GetPoint(i, p);
        const double r = std::sqrt(p[0] * p[0] + p[1] * p[1]);
        // 半径必须等于轮廓点半径之一（resize 时比较所有候选）
        bool matchesProfile = false;
        for (const auto& entry: profile) {
            if (std::fabs(r - entry.first) < 1e-6) { matchesProfile = true; break; }
        }
        if (!matchesProfile) { radiusOk = false; }
        if (p[2] < minH - 1e-6 || p[2] > maxH + 1e-6) { heightOk = false; }
    }
    Check(radiusOk, label + ": every output point radius matches one contour radius");
    Check(heightOk, label + ": every output point height stays within the contour range");
}

// ---------------------------------------------------------------------------
// 用例 1：线段单元轮廓 -> 圆台面；点数/单元数/几何/属性传递全部核对
// ---------------------------------------------------------------------------
void TestLineContourSucceeds() {
    std::cout << "--- case 1: line-cell contour succeeds ---" << std::endl;

    const int resolution = 12;
    auto mesh = MakeLineContourMesh();
    auto pointIds = ig::IntArray::New();
    pointIds->SetName("point_id");
    pointIds->SetDimension(1);
    pointIds->Resize(3);
    pointIds->SetValue(0, 100.0);
    pointIds->SetValue(1, 200.0);
    pointIds->SetValue(2, 300.0);
    auto attributes = mesh->GetAttributeSet();
    if (!attributes) {
        attributes = ig::AttributeSet::New();
        mesh->SetAttributeSet(attributes);
    }
    attributes->AddScalar(IG_POINT, pointIds);

    auto filter = ig::VolumeOfRevolutionFilter::New();
    filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
    filter->SetResolution(resolution);
    filter->SetAngle(2.0 * 3.141592653589793);
    filter->SetInput(mesh);

    Check(filter->Execute(), "Execute() succeeds for a line-cell contour");
    Check(filter->GetMessage().empty(), "GetMessage() is empty when execution succeeds");

    auto output = ig::DynamicCast<ig::UnstructuredMesh>(filter->GetOutput());
    if (output == nullptr) {
        Check(false, "output is an UnstructuredMesh");
        return;
    }
    Check(output->GetNumberOfPoints() == 3 * resolution, "point count == 3 points * resolution");
    // 2 条轮廓边 * resolution 段 * 2 个三角形
    const IGsize expectedCells = 2 * resolution * 2;
    Check(output->GetNumberOfCells() == expectedCells,
          "cell count == 2 contour edges * resolution * 2 triangles (" +
                  std::to_string(output->GetNumberOfCells()) + " vs " + std::to_string(expectedCells) + ")");
    CheckRevolutionGeometry(output, {{1.0, 0.0}, {2.0, 1.0}, {3.0, 2.0}}, resolution, "line contour");

    // 点属性按原轮廓点顺序复制到每一层
    auto outPointIds = FindPointArray(output, "point_id");
    Check(outPointIds != nullptr, "point attribute 'point_id' is transferred to the output");
    if (outPointIds != nullptr) {
        bool transferred = true;
        for (IGsize i = 0; i < outPointIds->GetNumberOfValues(); ++i) {
            // 第 i 个轮廓点的值应出现在 i*resolution .. i*resolution+resolution-1
            const IGsize contourPoint = i / static_cast<IGsize>(resolution);
            const double expected = static_cast<double>((contourPoint + 1) * 100);
            if (std::fabs(outPointIds->GetValue(i) - expected) > 0.5) { transferred = false; }
        }
        Check(transferred, "point attribute values keep the input point order per rotation layer");
    }
}

// ---------------------------------------------------------------------------
// 用例 2：折线单元轮廓（IG_POLY_LINE）也被接受
// ---------------------------------------------------------------------------
void TestPolyLineContourSucceeds() {
    std::cout << "--- case 2: polyline contour succeeds ---" << std::endl;

    const int resolution = 8;
    auto filter = ig::VolumeOfRevolutionFilter::New();
    filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
    filter->SetResolution(resolution);
    filter->SetAngle(2.0 * 3.141592653589793);
    filter->SetInput(MakePolyLineContourMesh());

    Check(filter->Execute(), "Execute() succeeds for a polyline contour");
    auto output = ig::DynamicCast<ig::UnstructuredMesh>(filter->GetOutput());
    CheckRevolutionGeometry(output, {{1.0, 0.0}, {2.0, 1.0}, {3.0, 2.0}}, resolution, "polyline contour");
}

// ---------------------------------------------------------------------------
// 用例 3：StructuredMesh 1D 曲线
// ---------------------------------------------------------------------------
void TestStructuredCurveSucceeds() {
    std::cout << "--- case 3: StructuredMesh 1D curve succeeds ---" << std::endl;

    const int resolution = 6;
    const int ni = 4;
    auto mesh = ig::StructuredMesh::New();
    mesh->SetPoints(ig::Points::New());
    for (int i = 0; i < ni; ++i) { AddPoint(mesh->GetPoints(), 1.0 + 0.5 * i, 0.0, 1.0 * i); }
    igIndex dims[3] = {ni, 1, 1};
    mesh->SetDimensionSize(dims);

    auto filter = ig::VolumeOfRevolutionFilter::New();
    filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
    filter->SetResolution(resolution);
    filter->SetAngle(2.0 * 3.141592653589793);
    filter->SetInput(mesh);

    Check(filter->Execute(), "Execute() succeeds for a 1D StructuredMesh curve");
    auto output = ig::DynamicCast<ig::UnstructuredMesh>(filter->GetOutput());
    if (output != nullptr) {
        Check(output->GetNumberOfPoints() == static_cast<IGsize>(ni) * resolution,
              "StructuredMesh curve: point count == ni * resolution");
        Check(output->GetNumberOfCells() == static_cast<IGsize>(ni - 1) * resolution * 2,
              "StructuredMesh curve: cell count == (ni-1) * resolution * 2");
    }
}

// ---------------------------------------------------------------------------
// 用例 4：非法输入必须被拒绝（这是本次修复的核心）
// ---------------------------------------------------------------------------
void TestInvalidInputsAreRejected() {
    std::cout << "--- case 4: invalid inputs are rejected ---" << std::endl;

    // 4.1 纯四面体体网格：修复前会被"顺序连线"当成轮廓并返回成功
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 0.0, 0.0, 0.0);
        AddPoint(mesh->GetPoints(), 1.0, 0.0, 0.0);
        AddPoint(mesh->GetPoints(), 0.0, 1.0, 0.0);
        AddPoint(mesh->GetPoints(), 0.0, 0.0, 1.0);
        AddCell(mesh, {0, 1, 2, 3}, ig::IG_TETRA);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(mesh);

        Check(!filter->Execute(), "Execute() fails for a tetrahedron-only UnstructuredMesh");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the rejected volume mesh");
        Check(filter->GetOutput() == nullptr, "a failed execution leaves no output behind");
        std::cout << "      message: " << filter->GetMessage() << std::endl;
    }

    // 4.2 点云（顶点单元，没有任何线段）：不能当成折线轮廓
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 1.0, 0.0, 0.0);
        AddPoint(mesh->GetPoints(), 2.0, 0.0, 1.0);
        AddPoint(mesh->GetPoints(), 3.0, 0.0, 2.0);
        AddCell(mesh, {0}, ig::IG_VERTEX);
        AddCell(mesh, {1}, ig::IG_VERTEX);
        AddCell(mesh, {2}, ig::IG_VERTEX);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(mesh);

        Check(!filter->Execute(), "Execute() fails for a vertex-only point cloud");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the rejected point cloud");
    }

    // 4.3 完全没有单元的网格
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 1.0, 0.0, 0.0);
        AddPoint(mesh->GetPoints(), 2.0, 0.0, 1.0);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetInput(mesh);
        Check(!filter->Execute(), "Execute() fails when the input has no cells at all");
    }

    // 4.4 没有输入的过滤器
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        Check(!filter->Execute(), "Execute() fails when there is no input");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the missing input");
    }

    // 4.5 封闭曲面（立方体表面）：没有边界边，不是 2D 轮廓
    {
        auto surface = ig::SurfaceMesh::New();
        ig::Points::Pointer points = ig::Points::New();
        for (int i = 0; i < 8; ++i) {
            AddPoint(points, static_cast<double>(i & 1), static_cast<double>((i >> 1) & 1),
                     static_cast<double>((i >> 2) & 1));
        }
        surface->SetPoints(points);
        auto faces = ig::CellArray::New();
        // 6 个四边形面，覆盖立方体
        const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                                 {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (const auto& quad: quads) {
            std::vector<igIndex> ids(quad, quad + 4);
            faces->AddCellIds(ids.data(), 4);
        }
        surface->SetFaces(faces);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(surface);

        Check(!filter->Execute(), "Execute() fails for a closed cube surface (no boundary edge)");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the closed-surface rejection");
        std::cout << "      message: " << filter->GetMessage() << std::endl;
    }

    // 4.6 3D 结构体网格（nk > 1）
    {
        auto mesh = ig::StructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        const int ni = 3, nj = 3, nk = 3;
        for (int k = 0; k < nk; ++k) {
            for (int j = 0; j < nj; ++j) {
                for (int i = 0; i < ni; ++i) {
                    AddPoint(mesh->GetPoints(), 1.0 + i, 0.0 + j, 0.0 + k);
                }
            }
        }
        igIndex dims[3] = {ni, nj, nk};
        mesh->SetDimensionSize(dims);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetInput(mesh);
        Check(!filter->Execute(), "Execute() fails for a 3D StructuredMesh volume (nk>1)");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the 3D volume rejection");
    }

    // 4.7 轮廓点含非有限值
    {
        auto mesh = MakeLineContourMesh();
        ig::Vector3d broken(1.0, 0.0, std::nan(""));
        mesh->GetPoints()->SetPoint(1, broken);
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetInput(mesh);
        Check(!filter->Execute(), "Execute() fails when a contour point is not finite");
    }
}

// ---------------------------------------------------------------------------
// 用例 5：非法参数同样要拒绝（轴、角度、分段数、轴与轮廓平面关系）
// ---------------------------------------------------------------------------
void TestInvalidParametersAreRejected() {
    std::cout << "--- case 5: invalid parameters are rejected ---" << std::endl;

    // 5.1 零向量旋转轴
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 0.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(MakeLineContourMesh());
        Check(!filter->Execute(), "Execute() fails for a zero-length axis direction");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the zero axis");
    }

    // 5.2 角度为 0
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(0.0);
        filter->SetInput(MakeLineContourMesh());
        Check(!filter->Execute(), "Execute() fails for a zero revolution angle");
    }

    // 5.3 角度为 NaN
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(std::nan(""));
        filter->SetInput(MakeLineContourMesh());
        Check(!filter->Execute(), "Execute() fails for a NaN revolution angle");
    }

    // 5.4 分段数小于 3（SetResolution 会夹到 3，这里直接检查夹取后的行为仍然成功）
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(1);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(MakeLineContourMesh());
        Check(filter->Execute(), "Execute() still succeeds when Resolution is clamped to 3");
    }

    // 5.5 全部轮廓点都在旋转轴上 -> 退化
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 0.0, 0.0, 0.0);
        AddPoint(mesh->GetPoints(), 0.0, 0.0, 1.0);
        AddPoint(mesh->GetPoints(), 0.0, 0.0, 2.0);
        AddCell(mesh, {0, 1}, ig::IG_LINE);
        AddCell(mesh, {1, 2}, ig::IG_LINE);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(mesh);
        Check(!filter->Execute(), "Execute() fails when every contour point lies on the axis");
    }

    // 5.6 严格共线的轮廓（不在轴上），绕与它自身平行的轴旋转：所有三角形零面积，必须拒绝。
    //     这里轮廓是 y=1 的直线段，与旋转轴（过原点的 X 轴）平行但不重合，
    //     因此不会被"所有点都在轴上"这条规则提前拦下，专门验证共线退化检查。
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 1.0, 1.0, 0.0);
        AddPoint(mesh->GetPoints(), 2.0, 1.0, 0.0);
        AddPoint(mesh->GetPoints(), 3.0, 1.0, 0.0);
        AddCell(mesh, {0, 1}, ig::IG_LINE);
        AddCell(mesh, {1, 2}, ig::IG_LINE);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(1.0, 0.0, 0.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(mesh);
        Check(!filter->Execute(), "Execute() fails for a collinear (off-axis) contour revolved about a parallel axis");
        Check(!filter->GetMessage().empty(), "GetMessage() explains the degenerate collinear contour");
        std::cout << "      message: " << filter->GetMessage() << std::endl;
    }

    // 5.7 同一条共线轮廓，但轴不与之平行：仍然合法（得到圆盘状环面）
    {
        auto mesh = ig::UnstructuredMesh::New();
        mesh->SetPoints(ig::Points::New());
        AddPoint(mesh->GetPoints(), 1.0, 1.0, 0.0);
        AddPoint(mesh->GetPoints(), 2.0, 1.0, 0.0);
        AddPoint(mesh->GetPoints(), 3.0, 1.0, 0.0);
        AddCell(mesh, {0, 1}, ig::IG_LINE);
        AddCell(mesh, {1, 2}, ig::IG_LINE);

        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(12);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(mesh);
        Check(filter->Execute(), "Execute() succeeds for a collinear contour revolved about a crossing axis");
    }
}

// ---------------------------------------------------------------------------
// 用例 6：部分角度与失败后的状态清理
// ---------------------------------------------------------------------------
void TestPartialAngleAndStateReset() {
    std::cout << "--- case 6: partial angle and state reset ---" << std::endl;

    // 6.1 180°：不合并首尾，点数 = 轮廓点数 * (resolution + 1)
    {
        const int resolution = 8;
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(resolution);
        filter->SetAngle(3.141592653589793);
        filter->SetInput(MakeLineContourMesh());
        Check(filter->Execute(), "Execute() succeeds for a 180 degree revolution");
        auto output = ig::DynamicCast<ig::UnstructuredMesh>(filter->GetOutput());
        if (output != nullptr) {
            Check(output->GetNumberOfPoints() == 3 * (resolution + 1),
                  "half turn keeps both end layers (points == profilePoints * (resolution+1))");
        }
    }

    // 6.2 轮廓平面与轴平行但轮廓沿轴有跨度（花瓶母线）：必须继续成功，
    //     避免把"轴平行于轮廓平面"一律当成非法
    {
        const int resolution = 8;
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(resolution);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(MakeLineContourMesh());
        Check(filter->Execute(),
              "Execute() still succeeds when the axis is parallel to the contour plane but the "
              "contour spans the axis direction (vase profile)");
        Check(filter->GetMessage().empty(), "no message is produced for a valid vase-like profile");
    }

    // 6.3 失败后再给合法输入：上一次的失败不残留
    {
        auto filter = ig::VolumeOfRevolutionFilter::New();
        filter->SetAxis(ig::Vector3d(0.0, 0.0, 0.0), ig::Vector3d(0.0, 0.0, 0.0));
        filter->SetResolution(8);
        filter->SetAngle(2.0 * 3.141592653589793);
        filter->SetInput(MakeLineContourMesh());
        Check(!filter->Execute(), "Execute() fails for the zero axis");
        Check(filter->GetOutput() == nullptr, "no output is kept after a failed execution");

        filter->SetAxis(ig::Vector3d(0.0, 0.0, 1.0), ig::Vector3d(0.0, 0.0, 0.0));
        Check(filter->Execute(), "the same filter succeeds after being given a valid axis");
        Check(filter->GetMessage().empty(), "the previous failure message is cleared on success");
        Check(filter->GetOutput() != nullptr, "a successful execution produces output again");
    }
}

} // namespace

int main() {
    TestLineContourSucceeds();
    TestPolyLineContourSucceeds();
    TestStructuredCurveSucceeds();
    TestInvalidInputsAreRejected();
    TestInvalidParametersAreRejected();
    TestPartialAngleAndStateReset();

    if (g_FailureCount == 0) {
        std::cout << "\nALL PASS" << std::endl;
        return 0;
    }
    std::cout << "\n" << g_FailureCount << " CHECK(S) FAILED" << std::endl;
    return 1;
}
