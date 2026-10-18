//
// 数据表视图（对标 ParaView Spreadsheet View）
//

/**
 * @class   igQtDataSpreadsheetWidget
 * @brief   以表格形式直接显示当前模型的点/单元属性数组。
 *
 * 【对标 ParaView Spreadsheet View】
 *   - 下拉框切换 Point Data / Cell Data（对应 ParaView 的 Attribute 切换）；
 *   - 第一列为元素 ID，之后每个数组按分量展开为 Name:0 / Name:1 ... 列
 *     （一维数组列名即数组名，与 ParaView 一致）；
 *   - 点击列头可按数值排序（与 ParaView 点击表头排序一致）；
 *   - 主窗口在选中模型变化时调用 SetDataObject() 刷新。
 *
 * 【用途】
 *   用于与 ParaView 逐点/逐单元核对 Filter 输出数值（如 Surface Vectors），
 *   无需导出 CSV。
 */
#pragma once

#include <IQCore/igQtExportModule.h>
#include <QWidget>
#include <iGameDataObject.h>

class QComboBox;
class QLabel;
class QTableWidget;

class IG_QT_MODULE_EXPORT igQtDataSpreadsheetWidget : public QWidget {
    Q_OBJECT
public:
    explicit igQtDataSpreadsheetWidget(QWidget* parent = nullptr);

    /// 设置要显示的数据对象（nullptr 时清空表格）
    void SetDataObject(iGame::DataObject::Pointer obj);

private slots:
    /// 按当前「属性类型」下拉框重新填充表格
    void Refill();

private:
    iGame::DataObject::Pointer m_Obj{nullptr};
    QComboBox* m_AttachCombo{nullptr};
    QTableWidget* m_Table{nullptr};
    QLabel* m_Summary{nullptr};
};
