# SurfaceVectorsFilter（向量投影到切平面）使用说明

## 1. 功能说明

SurfaceVectorsFilter 用于将向量属性投影到表面切平面上，去除向量的法向分量，保留切向分量。对标 ParaView 的 **Project Vectors On Surface** Filter。

给定一个表面网格和一个向量属性（点向量或单元向量），计算每个点/单元处的表面法向量，然后执行投影：

```
v_tangent = v - (v · n) * n
```

其中 v 是原始向量，n 是单位法向量，v_tangent 是投影后的切平面向量。

### 核心特性

- **支持点向量和单元向量**：自动识别属性附着类型
- **面积加权点法向量**：点向量使用相邻面的面积加权平均法向量，结果更光滑
- **面法向量直接计算**：单元向量使用各面自身的叉积法向量
- **三角形/四边形/多边形通用**：通过扇形拆分法计算任意多边形面的法向量
- **输出属性名可自定义**：默认输出名为 `TangentVectors`

### 与 ParaView 的对应关系

| 功能 | ParaView | iGameVis |
|------|----------|----------|
| 向量投影到表面 | Project Vectors On Surface | SurfaceVectors |
| 点向量投影 | 支持 | 支持（面积加权法向量） |
| 单元向量投影 | 支持 | 支持（面法向量） |
| 投影公式 | v - (v·n)n | v - (v·n)n |

---

## 2. 调用方式

### 2.1 C++ 代码调用

```cpp
#include <SurfaceVectors/iGameSurfaceVectorsFilter.h>
using namespace iGame;

// 1. 创建 Filter
auto filter = SurfaceVectorsFilter::New();

// 2. 设置输入（表面网格）
filter->SetInput(surfaceMesh);

// 3. 设置要投影的向量属性（按名称或索引二选一）
filter->SetVectorAttributeByName("Velocity");
// 或 filter->SetVectorAttributeByIndex(0);

// 4. （可选）设置输出属性名，默认 "TangentVectors"
filter->SetOutputAttributeName("TangentVelocity");

// 5. 执行
if (filter->Execute()) {
    auto result = filter->GetOutput();
    auto outMesh = DynamicCast<SurfaceMesh>(result);
    // 投影后的向量作为新的向量属性添加到了网格上
    auto& tangentAttr = outMesh->GetAttributeSet()->GetAttribute("TangentVelocity");
    // 使用投影后的向量...
} else {
    std::string msg = filter->GetMessage();
    std::cout << "Error: " << msg << std::endl;
}
```

### 2.2 iGameVis GUI 调用

在 iGameVis 界面中：

1. 加载表面模型文件（VTK、OBJ、STL 等格式）
2. 确保模型上有向量属性（点向量或单元向量）
3. 点击顶部菜单 **「算法处理」→「表面切向向量 (Surface Vectors)」**
4. 在弹出的参数对话框中选择要投影的向量属性
5. 点击「应用」执行，投影结果作为新的向量属性添加到模型上

---

## 3. 参数说明

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| VectorAttribute | string / int | - | 要投影的向量属性，按名称或索引指定，必须设置 |
| OutputAttributeName | string | "TangentVectors" | 输出的切向向量属性名称 |

---

## 4. 使用示例

### 示例 1：点向量投影到切平面

```cpp
// 加载一个带 Velocity 点向量的表面模型
auto mesh = FileIO::ReadFile("SurfaceVectors_plane.vtk");

auto filter = SurfaceVectorsFilter::New();
filter->SetInput(mesh);
filter->SetVectorAttributeByName("Velocity");
filter->SetOutputAttributeName("TangentVelocity");
filter->Execute();

auto outMesh = DynamicCast<SurfaceMesh>(filter->GetOutput());
auto attrSet = outMesh->GetAttributeSet();
int idx = attrSet->GetAttributeIndex("TangentVelocity");
// idx >= 0 表示投影成功，属性已添加
```

### 示例 2：单元向量投影

```cpp
filter->SetVectorAttributeByName("CellVector");
filter->SetOutputAttributeName("CellTangent");
filter->Execute();

// 输出属性的附着类型与输入一致（单元向量 → 单元属性）
auto& attr = outMesh->GetAttributeSet()->GetAttribute("CellTangent");
// attr.attachmentType == IG_CELL
```

### 示例 3：验证投影的正交性

```cpp
// 投影后的切向量应该与法向量正交（点积为 0）
auto& tangentAttr = attrSet->GetAttribute("TangentVectors");

// 对于平面网格（法向为 z 方向），投影后 z 分量应接近 0
float vals[3];
tangentAttr.pointer->GetElement(0, vals);
// vals[2] ≈ 0
```

---

## 5. 注意事项

### 5.1 法向量计算方法

- **单元向量**：直接用面的叉积法向量。对于多边形面，采用扇形拆分（以第 0 个顶点为中心拆分为多个三角形），累加各三角形的叉积得到整体法向量。
- **点向量**：面积加权平均法。每个点的法向量由其相邻所有面的法向量（未归一化，长度≈2×面积）累加后归一化得到。面积越大的面对点法向的贡献越大，结果更符合视觉预期。

### 5.2 输入要求

- 输入类型支持 `SurfaceMesh` 和 `UnstructuredMesh`（会自动提取表面）
- 输入属性必须是向量（维度 ≥ 3），标量属性无法投影
- 支持点向量（IG_POINT）和单元向量（IG_CELL）两种附着类型
- 非表面类型（VolumeMesh、PointSet 等）会返回错误

### 5.3 零法向量处理

对于退化面（所有顶点共线），叉积法向量长度为 0。此时归一化后仍为零向量，投影结果等于原向量（点积为 0，减去的法向分量为 0）。

### 5.4 数值精度

投影公式在数学上保证 `v_tangent · n = 0`，但由于浮点精度误差，实际点积可能是一个很小的非零值（通常 < 1e-6），属于正常现象。

### 5.5 测试模型

`Examples/Models/` 目录下提供了测试模型：

| 文件名 | 描述 | 属性 |
|--------|------|------|
| SurfaceVectors_plane.vtk | 3×3 平面网格（9 点，4 面），法向为 z 方向 | Velocity（点向量）、CellVector（单元向量） |

示例代码位于 `Examples/Filter/TestSurfaceVectors.cpp`，包含 5 组测试用例，运行后自动完成全部测试。
