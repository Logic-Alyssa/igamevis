#include <IQWidgets/igQtDataSpreadsheetWidget.h>

#include <iGameAttributeSet.h>
#include <iGamePoints.h>
#include <iGameCellType.h>
#include <iGameCellArray.h>
#include <iGameSurfaceMesh.h>
#include <iGameVolumeMesh.h>
#include <iGameUnstructuredMesh.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {
// 超过该行数时截断显示，防止超大模型把表格卡死（测试模型均远小于此值）
constexpr int kMaxRows = 50000;
} // namespace

igQtDataSpreadsheetWidget::igQtDataSpreadsheetWidget(QWidget* parent) : QWidget(parent) {
    // 深色样式：与主界面暗色主题一致（对标 ParaView Spreadsheet 的深色观感），
    // 显式指定全部颜色，避免继承系统浅色调色板导致隔行文字不可见
    setStyleSheet(QStringLiteral(R"(
        QWidget { background-color: #252526; color: #e0e0e0; }
        QTableWidget {
            background-color: #252526; alternate-background-color: #2d2d30;
            color: #e0e0e0; gridline-color: #3f3f46; border: 1px solid #3f3f46;
            selection-background-color: #094771; selection-color: #ffffff;
        }
        QHeaderView::section {
            background-color: #1e1e1e; color: #e0e0e0; border: none;
            border-right: 1px solid #3f3f46; border-bottom: 1px solid #3f3f46;
            padding: 4px 8px;
        }
        QTableCornerButton::section { background-color: #1e1e1e; border: none; }
        QComboBox {
            background-color: #2d2d30; color: #e0e0e0;
            border: 1px solid #3f3f46; padding: 3px 8px;
        }
        QComboBox QAbstractItemView {
            background-color: #2d2d30; color: #e0e0e0; selection-background-color: #094771;
        }
        QLabel { color: #c0c0c0; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    topBar->addWidget(new QLabel(QStringLiteral("Attribute:"), this));
    m_AttachCombo = new QComboBox(this);
    // userData 存 IGenum，Refill 时取回
    m_AttachCombo->addItem(QStringLiteral("Point Data"), static_cast<int>(IG_POINT));
    m_AttachCombo->addItem(QStringLiteral("Cell Data"), static_cast<int>(IG_CELL));
    topBar->addWidget(m_AttachCombo, 1);
    root->addLayout(topBar);

    m_Table = new QTableWidget(this);
    m_Table->setAlternatingRowColors(true);
    m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_Table->verticalHeader()->setVisible(false);
    root->addWidget(m_Table, 1);

    m_Summary = new QLabel(QStringLiteral("未选择模型"), this);
    root->addWidget(m_Summary);

    connect(m_AttachCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &igQtDataSpreadsheetWidget::Refill);
}

void igQtDataSpreadsheetWidget::SetDataObject(iGame::DataObject::Pointer obj) {
    m_Obj = obj;
    Refill();
}

void igQtDataSpreadsheetWidget::Refill() {
    m_Table->setSortingEnabled(false);
    m_Table->clear();
    m_Table->setRowCount(0);
    m_Table->setColumnCount(0);

    const IGenum attach = static_cast<IGenum>(m_AttachCombo->currentData().toInt());
    const QString attachLabel = (attach == IG_POINT) ? QStringLiteral("点") : QStringLiteral("单元");

    if (!m_Obj) {
        m_Summary->setText(QStringLiteral("未选择模型"));
        return;
    }
    auto attrs = m_Obj->GetAttributeSet();
    if (!attrs) {
        m_Summary->setText(QStringLiteral("当前模型没有属性数据"));
        return;
    }

    // ===== 列布局与 ParaView Spreadsheet 一致 =====
    // Point Data: Point ID | Points (x/y/z) | Points_Magnitude | 数组按名字字母序（多维数组后跟 Name_Magnitude）
    // Cell Data:  Cell ID | Cell Type | 数组按名字字母序（多维数组后跟 Name_Magnitude）
    // 每个数值列携带一个取值函数与有效行数，填表时统一调用
    std::vector<std::pair<QString, std::function<double(IGsize)>>> cols;
    std::vector<IGsize> colLimits;
    IGsize totalRows = 0;

    // 1) 点坐标列（仅 Point Data，ParaView 中 Point Data 总是先显示 Points）
    iGame::Points::Pointer pts = m_Obj->GetPoints();
    if (attach == IG_POINT && pts) {
        const IGsize nPts = pts->GetNumberOfPoints();
        totalRows = std::max(totalRows, nPts);
        for (int comp = 0; comp < 3; ++comp) {
            cols.emplace_back(QStringLiteral("Points:%1").arg(comp),
                              [pts, comp](IGsize r) { return pts->GetPoint(r)[comp]; });
            colLimits.push_back(nPts);
        }
        cols.emplace_back(QStringLiteral("Points_Magnitude"), [pts](IGsize r) {
            const auto& p = pts->GetPoint(r);
            return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        });
        colLimits.push_back(nPts);
    }

    // 2) Cell Type 列（仅 Cell Data，对标 ParaView 的 Cell Type 列，类型名与 ParaView 一致）
    //    注意转换顺序：VolumeMesh 继承自 SurfaceMesh，必须先判体网格，再判面网格
    std::vector<QString> cellTypes;
    if (attach == IG_CELL) {
        if (auto umesh = iGame::DynamicCast<iGame::UnstructuredMesh>(m_Obj)) {
            const IGsize n = umesh->GetNumberOfCells();
            cellTypes.reserve(n);
            for (IGsize i = 0; i < n; ++i) {
                cellTypes.emplace_back(iGame::GetCellTypeAsString(umesh->GetCellType(i)));
            }
        } else if (auto vmesh = iGame::DynamicCast<iGame::VolumeMesh>(m_Obj)) {
            auto* vols = vmesh->GetVolumes();
            if (vols) {
                const IGsize n = vols->GetNumberOfCells();
                cellTypes.reserve(n);
                for (IGsize i = 0; i < n; ++i) {
                    cellTypes.emplace_back(iGame::GetCellTypeAsString(
                            iGame::VolumeMesh::GetVolumeTypeWithPointNum(vols->GetCellSize(i))));
                }
            }
        } else if (auto smesh = iGame::DynamicCast<iGame::SurfaceMesh>(m_Obj)) {
            auto* faces = smesh->GetFaces();
            if (faces) {
                const IGsize n = faces->GetNumberOfCells();
                cellTypes.reserve(n);
                for (IGsize i = 0; i < n; ++i) {
                    cellTypes.emplace_back(iGame::GetCellTypeAsString(
                            iGame::SurfaceMesh::GetFaceTypeWithPointNum(faces->GetCellSize(i))));
                }
            }
        }
        totalRows = std::max(totalRows, static_cast<IGsize>(cellTypes.size()));
    }

    // 3) 收集该挂载位置下的数组并按名字字母序排序（ParaView 的列顺序）
    std::vector<iGame::ArrayObject::Pointer> arrays;
    for (IGsize i = 0; i < static_cast<IGsize>(attrs->GetNumberOfAttributes()); ++i) {
        auto& attr = attrs->GetAttribute(i);
        if (attr.isDeleted || !attr.pointer) continue;
        if (attr.attachmentType != attach) continue;
        const int dim = attr.pointer->GetDimension();
        if (dim <= 0) continue;
        arrays.push_back(attr.pointer);
        totalRows = std::max(totalRows, attr.pointer->GetNumberOfElements() / dim);
    }
    std::sort(arrays.begin(), arrays.end(),
              [](const auto& a, const auto& b) { return a->GetName() < b->GetName(); });

    for (const auto& a : arrays) {
        const QString name = QString::fromStdString(a->GetName());
        const int dim = a->GetDimension();
        const IGsize n = a->GetNumberOfElements() / dim;
        for (int comp = 0; comp < dim; ++comp) {
            cols.emplace_back(dim == 1 ? name : QStringLiteral("%1:%2").arg(name).arg(comp),
                              [a, dim, comp](IGsize r) {
                                  return a->GetValue(r * dim + comp);
                              });
            colLimits.push_back(n);
        }
        // ParaView 对多维数组额外显示一列模长
        if (dim > 1) {
            cols.emplace_back(name + QStringLiteral("_Magnitude"), [a, dim](IGsize r) {
                double s = 0.0;
                for (int c = 0; c < dim; ++c) {
                    const double v = a->GetValue(r * dim + c);
                    s += v * v;
                }
                return std::sqrt(s);
            });
            colLimits.push_back(n);
        }
    }

    const bool hasCellType = !cellTypes.empty();
    if (cols.empty() && !hasCellType) {
        m_Summary->setText(QStringLiteral("当前模型没有%1数据数组").arg(attachLabel));
        return;
    }

    const bool truncated = totalRows > static_cast<IGsize>(kMaxRows);
    const int rows = truncated ? kMaxRows : static_cast<int>(totalRows);

    m_Table->setColumnCount(1 + (hasCellType ? 1 : 0) + static_cast<int>(cols.size()));
    m_Table->setRowCount(rows);

    QStringList headers;
    // ParaView 首列即元素 ID（Point Data 为 Point ID，Cell Data 为 Cell ID），
    // Cell Data 下紧跟 Cell Type 列
    headers << ((attach == IG_POINT) ? QStringLiteral("Point ID") : QStringLiteral("Cell ID"));
    if (hasCellType) headers << QStringLiteral("Cell Type");
    for (const auto& c : cols) headers << c.first;
    m_Table->setHorizontalHeaderLabels(headers);

    m_Table->setUpdatesEnabled(false);
    for (int r = 0; r < rows; ++r) {
        auto* idItem = new QTableWidgetItem();
        idItem->setData(Qt::DisplayRole, r);
        m_Table->setItem(r, 0, idItem);
        int colIdx = 1;
        if (hasCellType) {
            if (static_cast<IGsize>(r) < static_cast<IGsize>(cellTypes.size())) {
                m_Table->setItem(r, colIdx, new QTableWidgetItem(cellTypes[r]));
            }
            ++colIdx;
        }
        for (size_t j = 0; j < cols.size(); ++j, ++colIdx) {
            // 防御：某列有效行数小于总行数时越界留空（正常数据不会触发）
            if (static_cast<IGsize>(r) >= colLimits[j]) continue;
            auto* item = new QTableWidgetItem();
            item->setData(Qt::DisplayRole, cols[j].second(static_cast<IGsize>(r)));
            m_Table->setItem(r, colIdx, item);
        }
    }
    m_Table->setUpdatesEnabled(true);
    m_Table->setSortingEnabled(true);

    m_Summary->setText(QStringLiteral("%1 个%2，%3 个数组%4")
                               .arg(totalRows)
                               .arg(attachLabel)
                               .arg(arrays.size())
                               .arg(truncated ? QStringLiteral("（仅显示前 %1 行）").arg(kMaxRows)
                                              : QString()));
}
