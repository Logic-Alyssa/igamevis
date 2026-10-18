// Regression trigger: the numerical filter test passed while the real Qt
// entry had no usable Preview/result. Expected: menu -> dialog -> Preview ->
// Apply creates one result; changing shape/parameters and applying again
// updates the same model. Fix: "feat: integrate region extraction with latest main".
// Find its SHA with git log --diff-filter=A --format="%h %s" -- Examples/Filter/Selection/TestExtractCellsByRegionUi.cpp
#include <IQComponents/igQtFilterDialogDockWidget.h>
#include <IQComponents/igQtModelDialogWidget.h>
#include <IQCore/igQtMainWindow.h>
#include <IQWidgets/igQtModelDrawWidget.h>
#include <iGameFileIO.h>
#include <iGameBoxStyle.h>
#include <iGameCamera.h>
#include <iGameInteractor.h>
#include <iGameRadialStyle.h>
#include <iGameScene.h>
#include <iGameUnstructuredMesh.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStringList>
#include <cmath>
#include <iostream>

static bool HasCount(igQtFilterDialogDockWidget* dialog, int cells) {
    const QString expected = QStringLiteral("预览：%1 个单元").arg(cells);
    for (auto* label : dialog->findChildren<QLabel*>())
        if (label->text() == expected) return true;
    return false;
}

static igm::vec2 Project(iGame::Interactor* interactor, const iGame::Point& point) {
    const auto clip = interactor->GetMVP() * igm::vec4(point[0], point[1], point[2], 1.f);
    return {(clip.x / clip.w + 1.f) * interactor->GetWidth() * 0.5f,
            (1.f - clip.y / clip.w) * interactor->GetHeight() * 0.5f};
}

static void Drag(iGame::Interactor* interactor, const igm::vec2& from,
                 const igm::vec2& to, iGame::MouseButton button) {
    interactor->FilterEvent({iGame::IEvent::MousePress, button, from, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseMove, button, to, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseRelease, button, to, 0.});
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::cerr << "Creating iGameVis window\n";
    igQtMainWindow window;
    window.show();
    app.processEvents();

    auto input = iGame::FileIO::ReadFile("./Models/ExtractCellsByRegion_BoxCases.vtk");
    if (!input) { std::cerr << "Could not load region fixture\n"; return 1; }
    input->SetName("ExtractCellsByRegion_BoxCases");
    window.modelTreeWidget->addDataObjectToModelTree(input, File);
    auto scene = window.rendererWidget->GetScene();
    if (!scene || !scene->GetCurrentModel()) { std::cerr << "Input is not selected\n"; return 2; }
    app.processEvents();

    auto* menu = window.findChild<QMenu*>(QStringLiteral("menu_developing_filters"));
    QAction* action = nullptr;
    if (menu) for (auto* candidate : menu->actions())
        if (candidate->data().toString() == QStringLiteral("extract_cells_by_region"))
            action = candidate;
    if (!action) { std::cerr << "Region menu entry missing\n"; return 3; }
    action->trigger();
    app.processEvents();
    igQtFilterDialogDockWidget* dialog = nullptr;
    for (auto* candidate : window.findChildren<igQtFilterDialogDockWidget*>())
        if (candidate->windowTitle() == QStringLiteral("按区域提取单元")) dialog = candidate;
    if (!dialog || !dialog->isVisible()) { std::cerr << "Region dialog missing\n"; return 4; }
    auto* previewButton = dialog->findChild<QPushButton*>(QStringLiteral("previewButton"));
    auto* applyButton = dialog->findChild<QPushButton*>(QStringLiteral("applyButton"));
    if (!previewButton || !previewButton->isVisible() || !applyButton || !applyButton->isVisible()) {
        std::cerr << "Region Preview button is hidden\n"; return 5;
    }

    // Verify the first interaction needs no typed coordinates: the region
    // starts near the loaded model and its visible center translates it.
    auto initialInteractor = scene->GetInteractor();
    auto initialBox = iGame::DynamicCast<iGame::BoxStyle>(
        initialInteractor->GetSpecialInteractor("SelectBox"));
    if (!initialBox || !initialBox->GetBox()) return 26;
    const auto initialBounds = initialBox->GetBox()->GetExtremePoint();
    const auto initialCenter = initialBox->GetBox()->GetMidPoint();
    const QString initialX = qobject_cast<QLineEdit*>(dialog->getWidget(1))->text();
    const auto initialHandle = Project(initialInteractor, initialCenter);
    const QPoint centerOnScreen = window.rendererWidget->mapToGlobal(
        QPoint(static_cast<int>(initialHandle.x), static_cast<int>(initialHandle.y)));
    if (dialog->frameGeometry().contains(centerOnScreen)) {
        std::cerr << "Region dialog covers the center drag handle\n"; return 32;
    }
    if (argc > 3) window.grab().save(QString::fromLocal8Bit(argv[3]));
    // A slightly imprecise click must still grab the visible center instead
    // of rotating the camera when the user starts a drag.
    Drag(initialInteractor, initialHandle + igm::vec2{18.f, 0.f},
         initialHandle + igm::vec2{68.f, 30.f},
         iGame::LeftButton);
    app.processEvents();
    const auto draggedBounds = initialBox->GetBox()->GetExtremePoint();
    if ((initialBox->GetBox()->GetMidPoint() - initialCenter).length() < 1e-5 ||
        ((draggedBounds.second - draggedBounds.first) -
         (initialBounds.second - initialBounds.first)).length() > 1e-4 ||
        qobject_cast<QLineEdit*>(dialog->getWidget(1))->text() == initialX) {
        std::cerr << "Initial Box cannot be moved without typing coordinates\n";
        return 27;
    }
    if (argc > 4) window.grab().save(QString::fromLocal8Bit(argv[4]));
    previewButton->click();
    app.processEvents();
    bool previewAfterDrag = false;
    for (auto* label : dialog->findChildren<QLabel*>())
        previewAfterDrag |= label->text().startsWith(QStringLiteral("预览："));
    if (!previewAfterDrag) return 28;

    auto* initialShape = qobject_cast<QComboBox*>(dialog->getWidget(0));
    if (!initialShape) return 29;
    const auto translatedBoxCenter = initialBox->GetBox()->GetMidPoint();
    initialShape->setCurrentIndex(1);
    app.processEvents();
    auto initialSphere = iGame::DynamicCast<iGame::RadialStyle>(
        initialInteractor->GetSpecialInteractor("ExtractCellsByRegionSphere"));
    if (!initialSphere ||
        (initialSphere->GetStartPoint() - translatedBoxCenter).length() > 1e-4) {
        std::cerr << "Sphere did not appear at the Box center\n"; return 30;
    }
    if (argc > 5) window.grab().save(QString::fromLocal8Bit(argv[5]));
    const auto initialSphereCenter = initialSphere->GetStartPoint();
    const double initialSphereRadius =
        (initialSphere->GetEndPoint() - initialSphereCenter).length();
    const auto sphereHandle = Project(initialInteractor, initialSphereCenter);
    Drag(initialInteractor, sphereHandle + igm::vec2{18.f, 0.f},
         sphereHandle + igm::vec2{63.f, 20.f},
         iGame::LeftButton);
    app.processEvents();
    if ((initialSphere->GetStartPoint() - initialSphereCenter).length() < 1e-5 ||
        std::abs((initialSphere->GetEndPoint() -
                  initialSphere->GetStartPoint()).length() - initialSphereRadius) > 1e-4) {
        std::cerr << "Initial Sphere cannot be moved without typing coordinates\n";
        return 31;
    }
    if (argc > 6) window.grab().save(QString::fromLocal8Bit(argv[6]));
    initialShape->setCurrentIndex(0);
    app.processEvents();

    // Regression: boundary handles were hidden in center-only mode, and the
    // generic Box drag moved the opposite face when clamped. Fix: "feat: finalize region controls and polyhedron extraction" (same commit).
    // Exercise every face in an oblique view, round trips and crossing the
    // opposite face: only the chosen coordinate may change; camera stays still.
    auto camera = initialInteractor->GetCamera();
    const auto savedPosition = camera->GetPosition();
    const auto focal = camera->GetFocal();
    const float distance = (savedPosition - focal).length();
    camera->SetPosition(focal + igm::vec3(0.65f, 0.5f, 1.f) * distance);
    app.processEvents();
    const auto resizeCamera = camera->GetPosition();
    constexpr int axes[] = {1, 1, 0, 0, 2, 2};
    constexpr bool maximum[] = {true, false, false, true, true, false};
    auto resetBox = [&]() {
        for (int id = 1; id <= 6; ++id)
            qobject_cast<QLineEdit*>(dialog->getWidget(id))->setText(id <= 3 ? "-1" : "1");
        QMetaObject::invokeMethod(dialog->getWidget(6), "editingFinished");
        app.processEvents();
    };
    for (int face = 0; face < 6; ++face) {
        for (float shift : {0.35f, -0.35f, -4.f}) {
            resetBox();
            const auto before = initialBox->GetBox()->GetExtremePoint();
            const auto handle = initialBox->GetBox()->GetOpePoints()[face];
            auto offset = handle;
            const int axis = axes[face];
            const float sign = maximum[face] ? 1.f : -1.f;
            offset[axis] += 2.f;
            const auto from = Project(initialInteractor, handle);
            // Add a perpendicular mouse component: it must not amplify resize.
            const auto pixels = (Project(initialInteractor, offset) - from) / 2.f;
            const auto perpendicular = igm::vec2(-pixels.y, pixels.x) * 0.1f;
            Drag(initialInteractor, from, from + pixels * (sign * shift) + perpendicular,
                 iGame::LeftButton);
            app.processEvents();
            const auto after = initialBox->GetBox()->GetExtremePoint();
            for (int a = 0; a < 3; ++a) {
                const bool lowFixed = a != axis || maximum[face];
                const bool highFixed = a != axis || !maximum[face];
                if ((lowFixed && std::abs(after.first[a] - before.first[a]) > 1.e-5) ||
                    (highFixed && std::abs(after.second[a] - before.second[a]) > 1.e-5)) {
                    std::cerr << "Box resize moved a fixed face: " << face << "\n"; return 33;
                }
                for (int side = 0; side < 2; ++side) {
                    const double coordinate = side ? after.second[a] : after.first[a];
                    if (std::abs(qobject_cast<QLineEdit*>(dialog->getWidget(1 + a + side * 3))->text().toDouble() - coordinate) > 1.e-5)
                        return 34;
                }
            }
            const float extent = after.second[axis] - after.first[axis];
            if ((shift > 0 && extent <= 2.f) || (shift < 0 && extent >= 2.f) ||
                extent <= 0.f || (shift < -2 && extent > 1.e-5f) ||
                (camera->GetPosition() - resizeCamera).length() > 1.e-6f) {
                std::cerr << "Box resize direction/clamp/camera failed: " << face << " extent " << extent << "\n"; return 35;
            }
        }
    }
    resetBox();
    // Regression: grabbing the outline either did nothing or resized the
    // region. Fix: "feat: finalize region controls and polyhedron extraction" (same commit). A line (away from resize handles) must translate
    // without a grab jump, preserve all extents and leave the camera fixed.
    bool edgeTested = false;
    for (const auto& edge : initialBox->GetBox()->GetAllEdges()) {
        const auto p = edge.first + (edge.second - edge.first) * .25f;
        const auto from = Project(initialInteractor, p);
        bool clear = (from - Project(initialInteractor, initialBox->GetBox()->GetMidPoint())).length() > 35.f;
        for (const auto& handle : initialBox->GetBox()->GetOpePoints())
            clear &= (from - Project(initialInteractor, handle)).length() > 20.f;
        if (!clear) continue;
        const auto before = initialBox->GetBox()->GetExtremePoint();
        const auto center = initialBox->GetBox()->GetMidPoint();
        initialInteractor->FilterEvent({iGame::IEvent::MousePress, iGame::LeftButton, from, 0.});
        initialInteractor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, from, 0.});
        if ((initialBox->GetBox()->GetMidPoint() - center).length() > 1.e-5) return 45;
        const auto to = from + igm::vec2{35.f, 20.f};
        initialInteractor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, to, 0.});
        initialInteractor->FilterEvent({iGame::IEvent::MouseRelease, iGame::LeftButton, to, 0.});
        app.processEvents();
        const auto after = initialBox->GetBox()->GetExtremePoint();
        if ((initialBox->GetBox()->GetMidPoint() - center).length() < 1.e-5 ||
            ((after.second - after.first) - (before.second - before.first)).length() > 1.e-4 ||
            (camera->GetPosition() - resizeCamera).length() > 1.e-6 ||
            std::abs(qobject_cast<QLineEdit*>(dialog->getWidget(1))->text().toDouble() - after.first[0]) > 1.e-5) return 46;
        edgeTested = true;
        break;
    }
    if (!edgeTested) return 47;
    resetBox();
    // Regression: ParaView middle/right gestures previously fell through to
    // camera motion. Fix: "feat: finalize region controls and polyhedron extraction" (same commit). Middle translates the picked box; right
    // scales all axes around a fixed center, including an oblique view.
    {
        const auto bounds = initialBox->GetBox()->GetExtremePoint();
        const auto center = initialBox->GetBox()->GetMidPoint();
        const auto from = Project(initialInteractor, center);
        Drag(initialInteractor, from, from + igm::vec2{25.f, 15.f}, iGame::MiddleButton);
        app.processEvents();
        const auto moved = initialBox->GetBox()->GetExtremePoint();
        if ((initialBox->GetBox()->GetMidPoint() - center).length() < 1.e-5 ||
            ((moved.second - moved.first) - (bounds.second - bounds.first)).length() > 1.e-4 ||
            (camera->GetPosition() - resizeCamera).length() > 1.e-6) return 52;
        resetBox();
        for (float factor : {1.4f, .6f}) {
            const auto before = initialBox->GetBox()->GetExtremePoint();
            const auto c = initialBox->GetBox()->GetMidPoint();
            const auto cp = Project(initialInteractor, c);
            const auto hp = Project(initialInteractor, initialBox->GetBox()->GetOpePoints()[3]);
            Drag(initialInteractor, hp, cp + (hp - cp) * factor, iGame::RightButton);
            app.processEvents();
            const auto after = initialBox->GetBox()->GetExtremePoint();
            const auto oldSize = before.second - before.first, newSize = after.second - after.first;
            const double ratio = newSize[0] / oldSize[0];
            if ((initialBox->GetBox()->GetMidPoint() - c).length() > 1.e-5 ||
                (factor > 1.f ? ratio <= 1. : ratio >= 1.) ||
                std::abs(newSize[1] / oldSize[1] - ratio) > 1.e-4 ||
                std::abs(newSize[2] / oldSize[2] - ratio) > 1.e-4 ||
                (camera->GetPosition() - resizeCamera).length() > 1.e-6 ||
                std::abs(qobject_cast<QLineEdit*>(dialog->getWidget(4))->text().toDouble() - after.second[0]) > 1.e-5)
                return 53;
        }
        resetBox();
    }
    const auto returnHandle = initialBox->GetBox()->GetOpePoints()[3];
    const auto returnFrom = Project(initialInteractor, returnHandle);
    const auto returnTo = returnFrom + igm::vec2{40.f, 15.f};
    initialInteractor->FilterEvent({iGame::IEvent::MousePress, iGame::LeftButton, returnFrom, 0.});
    initialInteractor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, returnTo, 0.});
    initialInteractor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, returnFrom, 0.});
    initialInteractor->FilterEvent({iGame::IEvent::MouseRelease, iGame::LeftButton, returnFrom, 0.});
    if (std::abs(initialBox->GetBox()->GetExtremePoint().second[0] - 1.f) > 1.e-5) return 36;
    camera->SetPosition(savedPosition);
    app.processEvents();

    const char* values[] = {"-1", "-1", "-1", "1", "1", "1"};
    for (int id = 1; id <= 6; ++id) {
        auto* edit = qobject_cast<QLineEdit*>(dialog->getWidget(id));
        if (!edit) return 6;
        edit->setText(QString::fromLatin1(values[id - 1]));
        QMetaObject::invokeMethod(edit, "editingFinished");
    }
    app.processEvents();
    previewButton->click();
    app.processEvents();
    if (!HasCount(dialog, 1)) { std::cerr << "Strict Box preview is not 1\n"; return 7; }
    if (argc > 1) dialog->grab().save(QString::fromLocal8Bit(argv[1]));

    auto* strict = qobject_cast<QCheckBox*>(dialog->getWidget(8));
    if (!strict) return 8;
    strict->setChecked(false);
    previewButton->click();
    app.processEvents();
    if (!HasCount(dialog, 3)) { std::cerr << "Loose Box preview is not 3\n"; return 9; }
    applyButton->click();
    app.processEvents();
    auto result = scene->GetCurrentModel();
    auto resultMesh = result ? iGame::DynamicCast<iGame::UnstructuredMesh>(result->GetDataObject()) : nullptr;
    if (!resultMesh || resultMesh->GetNumberOfCells() != 3) {
        std::cerr << "First Apply did not create visible 3-cell result\n"; return 10;
    }
    if (argc > 2) window.grab().save(QString::fromLocal8Bit(argv[2]));
    const auto resultId = scene->GetCurrentModelID();

    strict->setChecked(true);
    previewButton->click();
    app.processEvents();
    if (!HasCount(dialog, 1)) return 11;
    applyButton->click();
    app.processEvents();
    result = scene->GetCurrentModel();
    resultMesh = result ? iGame::DynamicCast<iGame::UnstructuredMesh>(result->GetDataObject()) : nullptr;
    if (scene->GetCurrentModelID() != resultId || !resultMesh || resultMesh->GetNumberOfCells() != 1) {
        std::cerr << "Second Apply failed to update the same result node\n"; return 12;
    }

    auto interactor = scene->GetInteractor();
    auto box = iGame::DynamicCast<iGame::BoxStyle>(interactor->GetSpecialInteractor("SelectBox"));
    if (!box || !box->GetBox()) { std::cerr << "Box widget missing\n"; return 17; }
    QStringList boxValuesBefore;
    for (int id = 1; id <= 6; ++id)
        boxValuesBefore << qobject_cast<QLineEdit*>(dialog->getWidget(id))->text();
    const auto boundsBeforeDrag = box->GetBox()->GetExtremePoint();
    const auto centerBeforeDrag = box->GetBox()->GetMidPoint();
    const auto boxHandle = Project(interactor, centerBeforeDrag);
    Drag(interactor, boxHandle, boxHandle + igm::vec2{60.f, 25.f}, iGame::LeftButton);
    app.processEvents();
    bool boxChanged = false;
    for (int id = 1; id <= 6; ++id)
        boxChanged |= qobject_cast<QLineEdit*>(dialog->getWidget(id))->text() != boxValuesBefore[id - 1];
    const auto boundsAfterDrag = box->GetBox()->GetExtremePoint();
    if (!boxChanged || (box->GetBox()->GetMidPoint() - centerBeforeDrag).length() < 1e-5 ||
        ((boundsAfterDrag.second - boundsAfterDrag.first) -
         (boundsBeforeDrag.second - boundsBeforeDrag.first)).length() > 1e-4) {
        std::cerr << "Box center drag did not translate while preserving size and coordinates\n";
        return 18;
    }
    applyButton->click();
    app.processEvents();
    if (scene->GetCurrentModelID() != resultId) return 19;

    auto* shape = qobject_cast<QComboBox*>(dialog->getWidget(0));
    auto* radius = qobject_cast<QLineEdit*>(dialog->getWidget(7));
    if (!shape || !radius) return 13;
    const auto centerBeforeSwitch = box->GetBox()->GetMidPoint();
    shape->setCurrentIndex(1);
    app.processEvents();
    auto sphere = iGame::DynamicCast<iGame::RadialStyle>(
        interactor->GetSpecialInteractor("ExtractCellsByRegionSphere"));
    if (!sphere || (sphere->GetStartPoint() - centerBeforeSwitch).length() > 1e-4) {
        std::cerr << "Sphere did not appear at the Box center\n"; return 29;
    }
    for (int id = 1; id <= 3; ++id) {
        auto* edit = qobject_cast<QLineEdit*>(dialog->getWidget(id));
        edit->setText(QStringLiteral("0"));
        QMetaObject::invokeMethod(edit, "editingFinished");
    }
    radius->setText(QStringLiteral("1"));
    QMetaObject::invokeMethod(radius, "editingFinished");
    previewButton->click();
    app.processEvents();
    if (!HasCount(dialog, 1)) { std::cerr << "Sphere preview is not 1\n"; return 14; }
    if (!sphere) { std::cerr << "Sphere widget missing\n"; return 20; }
    const auto centerBefore = sphere->GetStartPoint();
    const double radiusBefore = (sphere->GetEndPoint() - centerBefore).length();
    const auto centerHandle = Project(interactor, centerBefore);
    Drag(interactor, centerHandle, centerHandle + igm::vec2{45.f, 25.f}, iGame::LeftButton);
    app.processEvents();
    if ((sphere->GetStartPoint() - centerBefore).length() < 1e-5 ||
        std::abs((sphere->GetEndPoint() - sphere->GetStartPoint()).length() - radiusBefore) > 1e-4) {
        std::cerr << "Sphere center drag failed or changed radius\n"; return 21;
    }
    if (radius->text() != QStringLiteral("1")) {
        std::cerr << "Sphere center drag changed the radius field\n"; return 32;
    }
    if (qobject_cast<QLineEdit*>(dialog->getWidget(1))->text().toDouble() == centerBefore[0]) {
        std::cerr << "Sphere center drag did not synchronize coordinates\n"; return 22;
    }
    previewButton->click();
    app.processEvents();
    applyButton->click();
    app.processEvents();
    if (scene->GetCurrentModelID() != resultId) return 15;

    // Regression: the sphere outline was not a translation target. Fix: "feat: finalize region controls and polyhedron extraction" (same commit).
    // Use a view-facing arc away from the radius dot; grabbing must not snap
    // the center to the border, and movement must preserve radius and camera.
    const auto borderCenter = sphere->GetStartPoint();
    const auto borderCamera = camera->GetPosition();
    const double borderRadius = (sphere->GetEndPoint() - borderCenter).length();
    const auto borderPixel = Project(interactor, borderCenter + iGame::Point(0.f, borderRadius * .5f, borderRadius * .8660254f));
    interactor->FilterEvent({iGame::IEvent::MousePress, iGame::LeftButton, borderPixel, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, borderPixel, 0.});
    if ((sphere->GetStartPoint() - borderCenter).length() > 1.e-5) return 48;
    const auto borderTo = borderPixel + igm::vec2{35.f, 20.f};
    interactor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, borderTo, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseRelease, iGame::LeftButton, borderTo, 0.});
    app.processEvents();
    if ((sphere->GetStartPoint() - borderCenter).length() < 1.e-5 ||
        std::abs((sphere->GetEndPoint() - sphere->GetStartPoint()).length() - borderRadius) > 1.e-4 ||
        (camera->GetPosition() - borderCamera).length() > 1.e-6 ||
        std::abs(qobject_cast<QLineEdit*>(dialog->getWidget(1))->text().toDouble() - sphere->GetStartPoint()[0]) > 1.e-5)
        return 49;
    // Regression: a drawn latitude could not be grabbed when picking still
    // used only three great circles. Fix: "feat: finalize region controls and polyhedron extraction" (same commit). Grab the middle of a new
    // latitude segment away from center/radius controls; preserve the radius.
    {
        const auto c = sphere->GetStartPoint();
        const double r = (sphere->GetEndPoint() - c).length();
        constexpr double pi = 3.14159265358979323846;
        auto p = [&](double theta) {
            const double phi = 2. * pi / 7.;
            return c + iGame::Point(r * std::sin(phi) * std::cos(theta),
                                   r * std::sin(phi) * std::sin(theta), r * std::cos(phi));
        };
        const auto from = Project(interactor, (p(pi / 4.) + p(3. * pi / 8.)) * .5f);
        Drag(interactor, from, from + igm::vec2{25.f, 10.f}, iGame::LeftButton);
        app.processEvents();
        if ((sphere->GetStartPoint() - c).length() < 1.e-5 ||
            std::abs((sphere->GetEndPoint() - sphere->GetStartPoint()).length() - r) > 1.e-4 ||
            (camera->GetPosition() - borderCamera).length() > 1.e-6) return 56;
    }
    // Regression: Sphere right drag used to zoom the camera. Fix: "feat: finalize region controls and polyhedron extraction" (same commit).
    // VTK's upward drag grows the radius, downward shrinks; center stays fixed.
    {
        const auto c = sphere->GetStartPoint();
        const double r = (sphere->GetEndPoint() - c).length();
        const auto from = Project(interactor, c);
        Drag(interactor, from, from + igm::vec2{25.f, 15.f}, iGame::MiddleButton);
        app.processEvents();
        if ((sphere->GetStartPoint() - c).length() < 1.e-5 ||
            std::abs((sphere->GetEndPoint() - sphere->GetStartPoint()).length() - r) > 1.e-4 ||
            (camera->GetPosition() - borderCamera).length() > 1.e-6) return 54;
        const auto fixed = sphere->GetStartPoint();
        for (float dy : {-30.f, 30.f}) {
            const double before = (sphere->GetEndPoint() - fixed).length();
            const auto hp = Project(interactor, sphere->GetRadiusHandle());
            Drag(interactor, hp, hp + igm::vec2{0.f, dy}, iGame::RightButton);
            app.processEvents();
            const double after = (sphere->GetEndPoint() - fixed).length();
            if ((sphere->GetStartPoint() - fixed).length() > 1.e-5 ||
                (dy < 0.f ? after <= before : after >= before) ||
                std::abs(radius->text().toDouble() - after) > 1.e-5 ||
                (camera->GetPosition() - borderCamera).length() > 1.e-6) {
                std::cerr << "Sphere right-scale failed: dy=" << dy << " before=" << before << " after=" << after
                          << " centerDelta=" << (sphere->GetStartPoint() - fixed).length()
                          << " field=" << radius->text().toDouble()
                          << " cameraDelta=" << (camera->GetPosition() - borderCamera).length() << "\n";
                return 55;
            }
        }
    }
    const auto fixedCenter = sphere->GetStartPoint();
    const auto fixedCamera = camera->GetPosition();
    for (float factor : {1.5f, 0.5f, -1.f, 1.2f}) {
        radius->setText(QStringLiteral("1"));
        QMetaObject::invokeMethod(radius, "editingFinished");
        app.processEvents();
        const auto centerPixel = Project(interactor, fixedCenter);
        const auto handlePixel = Project(interactor, sphere->GetRadiusHandle());
        Drag(interactor, handlePixel, centerPixel + (handlePixel - centerPixel) * factor,
             iGame::LeftButton);
        app.processEvents();
        const double r = (sphere->GetEndPoint() - fixedCenter).length();
        const double expected = factor > 0.f ? factor : 2.2e-6;
        if ((sphere->GetStartPoint() - fixedCenter).length() > 1.e-6 ||
            std::abs(r - expected) > 2.e-5 ||
            std::abs(radius->text().toDouble() - r) > 1.e-5 ||
            (camera->GetPosition() - fixedCamera).length() > 1.e-6) {
            std::cerr << "Sphere radius resize/clamp/sync failed: " << r << "\n"; return 37;
        }
        previewButton->click(); app.processEvents();
        int chosen = -1;
        for (auto* label : dialog->findChildren<QLabel*>()) {
            if (label->text().startsWith(QStringLiteral("预览：")))
                chosen = label->text().section(QChar(0xff1a), 1).section(' ', 0, 0).toInt();
        }
        if (chosen < 0) return 38;
        applyButton->click(); app.processEvents();
        if (scene->GetCurrentModelID() != resultId) return 39;
        if (chosen > 0) {
            auto mesh = iGame::DynamicCast<iGame::UnstructuredMesh>(scene->GetCurrentModel()->GetDataObject());
            if (!mesh || mesh->GetNumberOfCells() != chosen) return 40;
        }
    }
    const auto radiusBeforeView = (sphere->GetEndPoint() - fixedCenter).length();
    camera->SetPosition(camera->GetFocal() + igm::vec3(1.f, 0.5f, 0.7f) * distance);
    const auto viewHandle = sphere->GetRadiusHandle();
    if (std::abs((viewHandle - fixedCenter).length() - radiusBeforeView) > 1.e-4 ||
        (sphere->GetStartPoint() - fixedCenter).length() > 1.e-6) return 41;
    const auto obliqueCenterPixel = Project(interactor, fixedCenter);
    const auto obliqueHandlePixel = Project(interactor, viewHandle);
    Drag(interactor, obliqueHandlePixel,
         obliqueCenterPixel + (obliqueHandlePixel - obliqueCenterPixel) * 0.8f, iGame::LeftButton);
    app.processEvents();
    if ((sphere->GetStartPoint() - fixedCenter).length() > 1.e-6 ||
        std::abs((sphere->GetEndPoint() - fixedCenter).length() - radiusBeforeView * 0.8) > 1.e-4)
        return 43;
    const auto roundTrip = Project(interactor, sphere->GetRadiusHandle());
    const auto roundRadius = (sphere->GetEndPoint() - fixedCenter).length();
    interactor->FilterEvent({iGame::IEvent::MousePress, iGame::LeftButton, roundTrip, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, roundTrip + igm::vec2{40.f, 0.f}, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseMove, iGame::LeftButton, roundTrip, 0.});
    interactor->FilterEvent({iGame::IEvent::MouseRelease, iGame::LeftButton, roundTrip, 0.});
    if (std::abs((sphere->GetEndPoint() - fixedCenter).length() - roundRadius) > 1.e-4) return 44;
    camera->SetPosition(fixedCamera);

    radius->setText(QStringLiteral("-1"));
    previewButton->click();
    app.processEvents();
    bool invalidShown = false;
    for (auto* label : dialog->findChildren<QLabel*>())
        invalidShown |= label->text() == QStringLiteral("区域参数无效");
    if (!invalidShown) return 16;
    radius->setText(QStringLiteral("1"));
    qobject_cast<QLineEdit*>(dialog->getWidget(1))->setText(QStringLiteral("100"));
    previewButton->click();
    app.processEvents();
    if (!HasCount(dialog, 0)) { std::cerr << "Empty preview is not 0\n"; return 23; }
    applyButton->click();
    app.processEvents();
    if (scene->GetCurrentModelID() != resultId) {
        std::cerr << "Empty Apply replaced the result\n"; return 24;
    }
    dialog->close();
    app.processEvents();
    if (interactor->GetSpecialInteractor("ExtractCellsByRegionSphere")) return 42;
    std::cout << "iGameVis region menu, dialog, Box/Sphere Preview and reusable Apply passed\n";
    window.close();
    return 0;
}
