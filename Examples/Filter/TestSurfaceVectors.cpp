/**
 * TestSurfaceVectors.cpp
 * 测试 SurfaceVectorsFilter — 将向量投影到表面切平面
 *
 * 运行方式：双击运行，自动完成所有测试并输出 PASS/FAIL
 * 测试模型：Examples/Models/SurfaceVectors_plane.vtk
 */
#include <SurfaceVectors/iGameSurfaceVectorsFilter.h>
#include <iGameFileIO.h>
#include <iostream>
#include <string>
#include <cmath>

using namespace iGame;

static int g_passCount = 0;
static int g_failCount = 0;

static void TestCheck(const std::string& name, bool condition) {
    if (condition) {
        std::cout << "  [PASS] " << name << std::endl;
        g_passCount++;
    } else {
        std::cout << "  [FAIL] " << name << std::endl;
        g_failCount++;
    }
}

static bool Near(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) < eps;
}

int main(int argc, char* argv[]) {
    std::cout << "===== TestSurfaceVectors =====" << std::endl;

    // 读取测试模型
    const std::string modelPath = "./Models/SurfaceVectors_plane.vtk";
    auto data = FileIO::ReadFile(modelPath);
    if (!data) {
        std::cout << "ERROR: Failed to read " << modelPath << std::endl;
        std::cout << "Please run from the Examples/ directory." << std::endl;
        std::cin.get();
        return 1;
    }
    auto mesh = DynamicCast<SurfaceMesh>(data);
    if (!mesh) {
        std::cout << "ERROR: Input is not a SurfaceMesh." << std::endl;
        std::cin.get();
        return 1;
    }
    std::cout << "Loaded model: " << mesh->GetNumberOfPoints() << " points, "
              << mesh->GetNumberOfFaces() << " faces" << std::endl;

    // ===================================================================
    // Test 1: 点向量投影到切平面
    // 平面在 z=0，法向量为 (0,0,1)，所以投影后 z 分量应该为 0
    // ===================================================================
    std::cout << "\n[Test 1] Point vector projection to tangent plane" << std::endl;
    {
        auto filter = SurfaceVectorsFilter::New();
        filter->SetInput(mesh);
        filter->SetVectorAttributeByName("Velocity");
        filter->SetOutputAttributeName("TangentVectors");
        bool ok = filter->Execute();
        TestCheck("Execute returns true", ok);

        auto outMesh = DynamicCast<SurfaceMesh>(filter->GetOutput());
        TestCheck("Output is SurfaceMesh", outMesh != nullptr);

        if (outMesh) {
            auto attrSet = outMesh->GetAttributeSet();
            int idx = attrSet ? attrSet->GetAttributeIndex("TangentVectors") : -1;
            TestCheck("Output attribute 'TangentVectors' exists", idx >= 0);

            if (idx >= 0) {
                auto& attr = attrSet->GetAttribute(idx);
                TestCheck("Output is point attribute", attr.attachmentType == IG_POINT);
                TestCheck("Output dimension is 3", attr.pointer->GetDimension() == 3);
                TestCheck("Output element count == 9",
                          attr.pointer->GetNumberOfElements() == 9);

                // 验证每个点的投影：z 分量应该接近 0（平面法向是 z 方向）
                bool allZZero = true;
                bool allXYPreserved = true;
                for (IGsize i = 0; i < attr.pointer->GetNumberOfElements(); i++) {
                    float vals[3];
                    attr.pointer->GetElement(i, vals);
                    if (std::abs(vals[2]) > 1e-4f) allZZero = false;
                    // x,y 分量应该和原始 Velocity 的 x,y 分量接近
                    float origVals[3];
                    mesh->GetAttributeSet()->GetAttribute("Velocity").pointer->GetElement(i, origVals);
                    if (!Near(vals[0], origVals[0]) || !Near(vals[1], origVals[1])) {
                        allXYPreserved = false;
                    }
                }
                TestCheck("All tangent vectors have z≈0 (plane normal is z)", allZZero);
                TestCheck("Tangent vectors preserve x,y components", allXYPreserved);
            }
        }
    }

    // ===================================================================
    // Test 2: 单元向量投影到切平面
    // ===================================================================
    std::cout << "\n[Test 2] Cell vector projection to tangent plane" << std::endl;
    {
        auto filter = SurfaceVectorsFilter::New();
        filter->SetInput(mesh);
        filter->SetVectorAttributeByName("CellVector");
        filter->SetOutputAttributeName("CellTangentVectors");
        bool ok = filter->Execute();
        TestCheck("Execute returns true", ok);

        auto outMesh = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (outMesh) {
            auto attrSet = outMesh->GetAttributeSet();
            int idx = attrSet ? attrSet->GetAttributeIndex("CellTangentVectors") : -1;
            TestCheck("Output cell attribute exists", idx >= 0);

            if (idx >= 0) {
                auto& attr = attrSet->GetAttribute(idx);
                TestCheck("Output is cell attribute", attr.attachmentType == IG_CELL);
                TestCheck("Output element count == 8",
                          attr.pointer->GetNumberOfElements() == 8);

                // 单元法向量也是 z 方向，投影后 z≈0
                bool allZZero = true;
                for (IGsize i = 0; i < attr.pointer->GetNumberOfElements(); i++) {
                    float vals[3];
                    attr.pointer->GetElement(i, vals);
                    if (std::abs(vals[2]) > 1e-4f) allZZero = false;
                }
                TestCheck("All cell tangent vectors have z≈0", allZZero);
            }
        }
    }

    // ===================================================================
    // Test 3: 按索引指定向量属性
    // ===================================================================
    std::cout << "\n[Test 3] Set vector attribute by index" << std::endl;
    {
        auto filter = SurfaceVectorsFilter::New();
        filter->SetInput(mesh);
        filter->SetVectorAttributeByIndex(0); // Velocity 是第一个向量属性
        bool ok = filter->Execute();
        TestCheck("Execute returns true", ok);

        auto outMesh = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (outMesh) {
            int idx = outMesh->GetAttributeSet()->GetAttributeIndex("TangentVectors");
            TestCheck("Default output name 'TangentVectors' exists", idx >= 0);
        }
    }

    // ===================================================================
    // Test 4: 不存在的属性名（错误处理）
    // ===================================================================
    std::cout << "\n[Test 4] Invalid attribute name (error handling)" << std::endl;
    {
        auto filter = SurfaceVectorsFilter::New();
        filter->SetInput(mesh);
        filter->SetVectorAttributeByName("NoSuchVector");
        bool ok = filter->Execute();
        TestCheck("Execute returns false for invalid attribute", !ok);
        TestCheck("GetMessage is not empty", !filter->GetMessage().empty());
    }

    // ===================================================================
    // Test 5: 投影向量的长度验证
    // 对于平面，v_tangent 的长度应该 = |v| * sin(theta)，其中 theta 是 v 与法向的夹角
    // 对于 z 法向平面，|v_tangent| = sqrt(vx^2 + vy^2)
    // ===================================================================
    std::cout << "\n[Test 5] Tangent vector magnitude verification" << std::endl;
    {
        auto filter = SurfaceVectorsFilter::New();
        filter->SetInput(mesh);
        filter->SetVectorAttributeByName("Velocity");
        filter->Execute();

        auto outMesh = DynamicCast<SurfaceMesh>(filter->GetOutput());
        if (outMesh) {
            auto attrSet = outMesh->GetAttributeSet();
            int idx = attrSet->GetAttributeIndex("TangentVectors");
            auto& outAttr = attrSet->GetAttribute(idx);
            auto& inAttr = mesh->GetAttributeSet()->GetAttribute("Velocity");

            bool magnitudeCorrect = true;
            for (IGsize i = 0; i < outAttr.pointer->GetNumberOfElements(); i++) {
                float outV[3], inV[3];
                outAttr.pointer->GetElement(i, outV);
                inAttr.pointer->GetElement(i, inV);

                // 切向量长度应该等于 sqrt(inV.x^2 + inV.y^2)
                float expectedMag = std::sqrt(inV[0] * inV[0] + inV[1] * inV[1]);
                float actualMag = std::sqrt(outV[0] * outV[0] + outV[1] * outV[1] + outV[2] * outV[2]);

                if (!Near(actualMag, expectedMag, 1e-3f)) {
                    magnitudeCorrect = false;
                    break;
                }
            }
            TestCheck("Tangent vector magnitude matches sqrt(vx^2+vy^2)", magnitudeCorrect);
        }
    }

    // ===================================================================
    // 汇总
    // ===================================================================
    std::cout << "\n===== Summary =====" << std::endl;
    std::cout << "  PASS: " << g_passCount << std::endl;
    std::cout << "  FAIL: " << g_failCount << std::endl;
    std::cout << "  Total: " << g_passCount + g_failCount << std::endl;

    if (g_failCount == 0) {
        std::cout << "\n  All tests PASSED!" << std::endl;
    } else {
        std::cout << "\n  Some tests FAILED!" << std::endl;
    }

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    return g_failCount > 0 ? 1 : 0;
}
