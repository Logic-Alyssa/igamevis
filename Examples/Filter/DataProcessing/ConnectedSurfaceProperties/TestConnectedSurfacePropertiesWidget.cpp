#include <IQCore/igQtMainWindow.h>
#include <IQWidgets/igQtConnectedSurfacePropertiesWidget.h>
#include <IQWidgets/igQtModelDrawWidget.h>
#include <IQWidgets/igQtSearchInfoWidget.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QEventLoop>
#include <QPushButton>
#include <QRadioButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckHeap(const char* stage) {
#if defined(_MSC_VER) && defined(_DEBUG)
    if (_CrtCheckMemory() == 0) {
        throw std::runtime_error(std::string("heap corruption detected: ") + stage);
    }
#else
    (void)stage;
#endif
    std::cout << "heap check passed: " << stage << std::endl;
}

QPushButton* FindExecuteButton(QWidget* panel) {
    for (auto* button : panel->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("执行并生成输出")) return button;
    }
    return nullptr;
}

void ProcessEventsFor(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool HasColumn(QTableWidget* table, const QString& name) {
    if (!table) return false;
    for (int column = 0; column < table->columnCount(); ++column) {
        auto* item = table->horizontalHeaderItem(column);
        if (item && item->text() == name) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "starting Qt panel heap test" << std::endl;
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#if defined(Q_OS_WIN)
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    QApplication app(argc, argv);
    std::cout << "QApplication created" << std::endl;

    try {
        iGame::Log::Init();
        std::cout << "log initialized" << std::endl;
        igQtMainWindow owner;
        owner.show();
        app.processEvents();
        CheckHeap("main window constructed");
        owner.initArgs({QStringLiteral("testConnectedSurfacePropertiesWidget"),
                        QStringLiteral("--filepath"),
                        QStringLiteral("./Models/ConnectedSurfacePropertiesTwoBoxes.vtk")});
        ProcessEventsFor(1500);
        CheckHeap("input model loaded");

        QAction* filterAction = nullptr;
        for (auto* action : owner.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("Connected Surface Properties"))) {
                filterAction = action;
                break;
            }
        }
        Check(filterAction != nullptr, "missing Connected Surface Properties action");
        filterAction->trigger();
        ProcessEventsFor(250);
        auto* panel = dynamic_cast<igQtConnectedSurfacePropertiesWidget*>(
                owner.findChild<QDialog*>(QStringLiteral("ConnectedSurfacePropertiesWidget")));
        Check(panel != nullptr && panel->isReady() && panel->isVisible(),
              "filter panel did not open with the selected SurfaceMesh");
        CheckHeap("filter panel opened");
        auto* execute = FindExecuteButton(panel);
        Check(execute != nullptr, "missing execute button");

        CheckHeap("before first execution");
        execute->click();
        app.processEvents();

        // 回归场景：Connected Surface Properties 的对象级结果此前只能在 Filter 弹窗中查看，
        // 关闭弹窗后无法使用项目统一的 SearchInfo 表格查询。执行后应自动打开“查找数据”，
        // 对象数据应完整显示有效性、面积、体积和质心；切回单元数据时，输入自带 FaceValue
        // 与新增的 ObjectIds、Areas、Volumes 必须同时存在。
        // 修复提交：待提交（主题：fix: 在查找数据中展示连通表面属性）。
        auto* searchDock = owner.findChild<QDockWidget*>(QStringLiteral("dockWidget_SearchInfo"));
        auto* searchWidget = owner.findChild<igQtSearchInfoWidget*>();
        auto* objectData = searchWidget
                ? searchWidget->findChild<QRadioButton*>(QStringLiteral("radioButton_Objects"))
                : nullptr;
        auto* cellData = searchWidget
                ? searchWidget->findChild<QRadioButton*>(QStringLiteral("radioButton_Cells"))
                : nullptr;
        auto* resultTable = searchWidget
                ? searchWidget->findChild<QTableWidget*>(QStringLiteral("tableWidget_Results"))
                : nullptr;
        Check(searchDock && searchDock->isVisible(), "SearchInfo dock did not open after execution");
        Check(objectData && objectData->isChecked(), "SearchInfo did not select object data");
        Check(resultTable && resultTable->rowCount() == 2, "object data table must contain two objects");
        Check(HasColumn(resultTable, QStringLiteral("ObjectValidity")) &&
                      HasColumn(resultTable, QStringLiteral("ObjectAreas")) &&
                      HasColumn(resultTable, QStringLiteral("ObjectVolumes")) &&
                      HasColumn(resultTable, QStringLiteral("ObjectCentroids[0]")) &&
                      HasColumn(resultTable, QStringLiteral("ObjectCentroids[1]")) &&
                      HasColumn(resultTable, QStringLiteral("ObjectCentroids[2]")),
              "object data table is missing connected-surface properties");

        cellData->click();
        app.processEvents();
        Check(HasColumn(resultTable, QStringLiteral("FaceValue")),
              "output lost an original cell attribute");
        Check(HasColumn(resultTable, QStringLiteral("ObjectIds")) &&
                      HasColumn(resultTable, QStringLiteral("Areas")) &&
                      HasColumn(resultTable, QStringLiteral("Volumes")),
              "cell data table is missing computed properties");
        CheckHeap("first execution completed");
        panel->close();
        app.processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        Check(!panel->isVisible(),
              "closing must hide the reusable panel without destroying it");
        CheckHeap("filter panel closed");

        filterAction->trigger();
        ProcessEventsFor(250);
        auto* reopenedPanel = dynamic_cast<igQtConnectedSurfacePropertiesWidget*>(
                owner.findChild<QDialog*>(QStringLiteral("ConnectedSurfacePropertiesWidget")));
        Check(panel == reopenedPanel && panel->isVisible(),
              "the same panel instance must reopen for the current model");
        execute->click();
        app.processEvents();
        CheckHeap("reopened panel executed");
        panel->close();
        app.processEvents();
        CheckHeap("filter panel closed a second time");

        owner.close();
        app.processEvents();
        CheckHeap("main window closed");
        std::cout << "Connected-surface-properties Qt panel heap test passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Connected-surface-properties Qt panel test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
