# ExtractCellsByRegionFilter 使用说明

## 功能

从非结构化网格提取位于 Box（轴对齐盒子）或 Sphere（球体）区域内的完整单元，生成独立子网格。输出保留被选中单元的类型、共享点拓扑，以及输入的点属性和单元属性。原模型不会被改写。

本 Filter 的“所有顶点都在区域内”选项对应 ParaView `Extract Cells By Region` 的 `Extraction Side = inside` 且 `Extract Intersected = Off`；取消勾选是宽松提取，在随附模型中与 `Extract Intersected = On` 的结果相同。区域边界上的点视为在区域内。本接口只实现 Box/Sphere 的区域内提取。

当前宽松模式按“至少一个顶点在区域内”判断；两份随附模型与 ParaView 的相交模式结果一致。若单元只有边或面穿过区域、所有顶点都在外部，当前算法不会选中它。此处是现有接口的范围限制。

## 图形界面调用

1. 加载并选中非结构化网格，打开“按区域提取单元”。
2. 选 Box 或 Sphere 后，场景立即显示空心线框和中心点。Sphere 采用 ParaView 风格的 16 条经线和 6 条纬线（纬向分辨率 8），无填充表面；经纬线都可左键拖动平移。左键拖中心点或线框边界可整体平移，尺寸不变；也可按住鼠标中键拖区域（包含面内）平移。左键拖 Box 红色面控制点只调整该侧范围；Sphere 红色半径点改变半径、球心不变。右键拖区域可整体缩放：Box 按拾取位置到中心的距离比例缩放，远离中心放大、靠近中心缩小，中心与各轴比例保持不变；Sphere 向上拖放大、向下拖缩小，球心保持不变。空白处拖动仍操作相机。边界悬停时线框变黄；数值框与预览同步更新，数值框也可精确调整位置和大小。不支持旋转区域。
3. 勾选“所有顶点都在区域内”时，只保留完整落在区域内的单元；取消勾选时还会保留至少一个顶点在区域内的单元。按“预览”查看将被提取的单元数量和高亮，不会创建模型。
4. 按“执行”生成结果。窗口保持打开；继续调整后再次执行会更新同一个结果节点。无效参数会提示；区域内没有单元时显示 0 并清除旧高亮，不创建空结果，也不覆盖已有结果。关闭窗口会移除临时区域控件，并恢复打开前的单元选择。

缩放最小尺寸/半径为 `max(模型最大跨度 × 10⁻⁶, 10⁻⁷)`，避免翻转或负半径。控制点重叠或某轴接近视线时，先切换视角。

Box 控件保持轴对齐。拖动沿当前视角的屏幕平面移动；若需沿其他方向移动，可切换视角再拖动，或直接输入坐标。

测试界面时，请启动本功能分支新构建的 `iGameVis.exe`，不要使用旧的主分支构建或先前的压缩包。加载 `Examples/Models/ExtractCellsByRegion_BoxCases.vtk` 后，在模型树选中输入模型，再从“算法处理 → 开发中filter → 按区域提取单元”打开本 Filter。区域初始位置与尺寸由模型包围盒计算，无需先填写坐标即可拖动中心点。Box 六个数值设为 `-1,-1,-1` 和 `1,1,1`：勾选严格模式预览为 1 个单元；取消勾选预览为 3 个。第一次执行产生一个结果节点，移动 Box 后再次执行应更新原节点。将形状切为 Sphere，还可检查中心拖动、半径不变和数值同步。

## C++ 调用示例

```cpp
#include <Selection/iGameExtractCellsByRegionFilter.h>
#include <iGameFileIO.h>

auto input = iGame::FileIO::ReadFile("./Models/ExtractCellsByRegion_BoxCases.vtk");
auto filter = iGame::ExtractCellsByRegionFilter::New();
filter->SetInput(input);
filter->SetBox(iGame::Vector3d(-1, -1, -1), iGame::Vector3d(1, 1, 1));
filter->SetRequireAllPoints(false); // ParaView: Extract Intersected = On
if (filter->Preview()) {
    const auto& inputCellIds = filter->GetSelectedCellIds();
    // 此模型的预期 ID 为 {0, 1, 3}。
}
if (filter->Execute()) {
    auto output = filter->GetOutput(); // 三个单元、十二个点
}
```

`Preview()` 只计算输入单元 ID，`Execute()` 用同一规则构建输出。改变 Box/Sphere 或严格选项后可再次预览和执行。无效 Box（任一最小值大于最大值）或非正球半径会返回 `false`。

## 测试与 ParaView 对照

测试模型固定为 `Examples/Models/ExtractCellsByRegion_BoxCases.vtk` 和 `Examples/Models/ExtractCellsByRegion_SphereCases.vtk`，分别含 4、5 个四面体，覆盖完全在内、跨边界、完全在外、恰好落在边界，以及盒内但球外的单元。两份模型都内置 float 点属性 `PreviewPointValue` 和 int 单元属性 `MaterialId`。示例不再使用 `Tet_Plane.vtk`。

| 模型 | 区域 | 严格（ParaView 相交关闭） | 宽松（ParaView 相交开启） |
| --- | --- | --- | --- |
| BoxCases | Box `[-1,1]³` | `{0}` | `{0,1,3}` |
| BoxCases | Sphere 中心 `(0,0,0)` 半径 `1` | `{0}` | `{0,1,3}` |
| SphereCases | Box `[-1,1]³` | `{0,4}` | `{0,1,3,4}` |
| SphereCases | Sphere 中心 `(0,0,0)` 半径 `1` | `{0}` | `{0,1,3}` |

使用 ParaView 6.2 的 `pvpython` 从仓库根目录运行：

```powershell
pvpython Examples/Filter/Selection/ParaViewExtractCellsByRegionReference.py
```

脚本用 ParaView 原生 Filter 读取同一模型、设置 `Extraction Side = inside`，分别运行 Box/Sphere 和 `Extract Intersected` 开/关，直接断言原始单元 ID、点数与两类属性值。手工 GUI 复现时，打开模型，选择 `Filters → Extract Cells By Region`，设置相同参数并点 `Apply`；拖动其区域控件后再次点 `Apply`，对照选中单元与属性。

iGameVis C++ 示例目标为 `testExtractCellsByRegion`，构建后从 `Examples` 的构建目录运行。程序使用固定的 `./Models/...` 相对路径，无需输入参数。测试还校验预览与执行 ID 一致、连续修改参数后预览更新、空区域、无效参数和 float 点属性/int 单元属性的完整保留。

交互参考：本机 ParaView 6.2.0 / VTK 9.7.1 的 `vtkBoxWidget2`、`vtkSphereWidget2` 及对应 Representation。中键平移、右键缩放沿用其基本规则；额外保留本接口已有的左键拖线框平移与 Sphere 半径点缩放。Box 保持轴对齐，不实现 ParaView 的旋转功能。

- [Box 控件事件](https://vtk.org/doc/nightly/html/classvtkBoxWidget2.html)
- [Sphere 控件事件](https://vtk.org/doc/nightly/html/classvtkSphereWidget2.html)
- [Box 9.7.1 源码](https://github.com/Kitware/VTK/blob/v9.7.1/Interaction/Widgets/vtkBoxRepresentation.cxx)
- [Sphere 9.7.1 源码](https://github.com/Kitware/VTK/blob/v9.7.1/Interaction/Widgets/vtkSphereRepresentation.cxx)
