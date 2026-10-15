#ifndef iGameVolumeOfRevolution_h
#define iGameVolumeOfRevolution_h

#include "iGameDataObject.h"
#include "iGameFilter.h"
#include <string>

IGAME_NAMESPACE_BEGIN

/**
 * @class VolumeOfRevolutionFilter
 * @brief 绕任意轴扫掠输入单元，生成旋转体的 Filter，语义对齐 vtkVolumeOfRevolutionFilter。
 *
 * 与 vtkVolumeOfRevolutionFilter 一样，本过滤器不再"提取轮廓"，而是把输入的
 * **每一个单元**按类型扫掠成高一个拓扑维数的单元：
 *
 * | 输入单元                   | 输出单元        | 说明                       |
 * | -------------------------- | --------------- | -------------------------- |
 * | IG_VERTEX                  | IG_LINE         | 点扫成线（不自动串联点云） |
 * | IG_LINE / IG_POLY_LINE     | IG_QUAD         | 线段扫成四边形             |
 * | IG_TRIANGLE                | IG_PRISM        | 三角形扫成楔形体（体单元） |
 * | IG_QUAD                    | IG_HEXAHEDRON   | 四边形扫成六面体（体单元） |
 * | IG_POLYGON / IG_FACE       | IG_POLYHEDRON   | 多边形扫成多面体（体单元） |
 *
 * 因此要得到体单元，输入应当是**二维截面单元**（三角形/四边形/多边形）；
 * 只给一维轮廓（线段/折线）时输出的是四边形面片，这与 ParaView 的行为一致。
 *
 * 输入支持 UnstructuredMesh、SurfaceMesh（逐面片旋转）、StructuredMesh（二维曲面或一维曲线）；
 * 输出统一为 UnstructuredMesh。与 ParaView 一致，只要输入里存在拓扑维数大于 2 的单元
 * （体单元），就整体拒绝——混合输入不会被部分接受。
 *
 * 与 VTK 一致的行为约定：
 * - 点的排布为"层优先"：index = layer * 输入点数 + 输入点号；
 * - 整圈（360 度）扫掠时首尾层合并，否则多生成一层；
 * - 每个生成的单元继承其来源单元的单元属性；
 * - 不做共线性/退化检查，几何退化（如截面贴轴）由调用方按 ParaView 的同样表现处理。
 */
class VolumeOfRevolutionFilter : public Filter {
public:
    I_OBJECT(VolumeOfRevolutionFilter);
    static Pointer New() { return new VolumeOfRevolutionFilter; }

    void SetAxis(const Vector3d& dir, const Vector3d& point) {
        m_AxisDirection = dir;
        m_AxisPoint = point;
    }
    void SetResolution(int res) { m_Resolution = (res < 3) ? 3 : res; }
    void SetAngle(double angle) { m_Angle = angle; }

    int GetResolution() const { return m_Resolution; }
    double GetAngle() const { return m_Angle; }

    bool Execute() override;

    // 执行失败的原因（输入不符合条件时给出可读说明，供界面/调用方展示）。
    // 空字符串表示上一次执行没有可报告的失败原因；执行成功但跳过了不支持的单元时，
    // 这里会给出非致命警告（ParaView 同样会 warn 并跳过，而不是静默忽略）。
    const std::string& GetMessage() const { return m_Message; }

protected:
    VolumeOfRevolutionFilter() {
        SetNumberOfInputs(1);
        SetNumberOfOutputs(1);
    }
    ~VolumeOfRevolutionFilter() override = default;

private:
    // 与 vtkVolumeOfRevolutionFilter 的默认值保持一致。
    int m_Resolution = 12;    // VTK 默认 12（每 30 度一段）
    double m_Angle = 2.0 * 3.141592653589793;
    Vector3d m_AxisDirection{0.0, 0.0, 1.0}; // 轴向方向，默认 (0,0,1)
    Vector3d m_AxisPoint{0.0, 0.0, 0.0};     // 轴上一点，默认 (0,0,0)

    // 记录非致命提示与致命失败原因
    std::string m_Message;
};

IGAME_NAMESPACE_END

#endif
