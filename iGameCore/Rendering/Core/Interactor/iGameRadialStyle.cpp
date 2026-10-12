#include "iGameRadialStyle.h"
#include "iGameInteractor.h"
#include "iGameLine.h"
#include "iGameScene.h"
#include <cmath>
#include <limits>
using namespace std;
IGAME_NAMESPACE_BEGIN

float RadialStyle::CalculatePickTolerance() {


    const float pixelTolerance = 5.0f;

    float ndcToleranceX = 2.0f * pixelTolerance / m_Interactor->GetWidth();
    float ndcToleranceY = 2.0f * pixelTolerance / m_Interactor->GetHeight();
    float ndcTolerance = std::max(ndcToleranceX, ndcToleranceY);

    igm::vec4 nearPlanePoint{0, 0, -1, 1};
    igm::vec4 farPlanePoint{0, 0, 1, 1};

    igm::vec4 nearWorld = m_InvertedMVP * nearPlanePoint;
    nearWorld /= nearWorld.w;

    igm::vec4 farWorld = m_InvertedMVP * farPlanePoint;
    farWorld /= farWorld.w;

    float depthScale = (farWorld - nearWorld).length() / 2.0f;

    return ndcTolerance * depthScale;
}

void RadialStyle::DrawRadial() {
    if (!m_Show) return;
    auto painter = m_Scene->GetPainter3D();

    if (m_CenterTranslationMode) {
        painter->SetPen(m_HoveredPoint == &m_StartPoint ? 16 : 11);
        painter->SetPen(m_HoveredPoint == &m_StartPoint ? Color::Magenta : Color::Orange);
        m_DrawHandles.push_back(painter->DrawPoint(m_StartPoint));
        if (m_ShowSphere) {
            const float radius = (m_EndPoint - m_StartPoint).length();
            painter->SetPen(3);
            painter->SetPen(0, 125, 240);
            // DrawCircle can inherit a filled brush from the scene. Use lines
            // so the region always remains hollow over the input model.
            constexpr int segments = 64;
            constexpr double pi = 3.14159265358979323846;
            for (int plane = 0; plane < 3; ++plane) {
                for (int i = 0; i < segments; ++i) {
                    const double a = 2.0 * pi * i / segments;
                    const double b = 2.0 * pi * (i + 1) / segments;
                    auto ringPoint = [&](double angle) {
                        Point p = m_StartPoint;
                        const float u = radius * static_cast<float>(std::cos(angle));
                        const float v = radius * static_cast<float>(std::sin(angle));
                        if (plane == 0) { p[1] += u; p[2] += v; }
                        else if (plane == 1) { p[0] += u; p[2] += v; }
                        else { p[0] += u; p[1] += v; }
                        return p;
                    };
                    m_DrawHandles.push_back(painter->DrawLine(ringPoint(a), ringPoint(b)));
                }
            }
        }
        return;
    }

    painter->SetPen(m_HoveredPoint == &m_StartPoint ? 13 : 9);
    painter->SetPen(Color::Blue);
    int startPointHandle = painter->DrawPoint(m_StartPoint);
    m_DrawHandles.push_back(startPointHandle);

    painter->SetPen(3);
    painter->SetPen(Color::White);
    int lineHandle = painter->DrawLine(m_StartPoint, m_EndPoint);
    m_DrawHandles.push_back(lineHandle);

    painter->SetPen(m_HoveredPoint == &m_EndPoint ? 13 : 9);
    painter->SetPen(Color::Red);
    int endPointHandle = painter->DrawPoint(m_EndPoint);
    m_DrawHandles.push_back(endPointHandle);
    if (m_ShowSphere) {
        const float radius = (m_EndPoint - m_StartPoint).length();
        painter->SetPen(2);
        painter->SetPen(Color::Yellow);
        m_DrawHandles.push_back(painter->DrawCircle(m_StartPoint, Vector3f(1, 0, 0), radius, 64));
        m_DrawHandles.push_back(painter->DrawCircle(m_StartPoint, Vector3f(0, 1, 0), radius, 64));
        m_DrawHandles.push_back(painter->DrawCircle(m_StartPoint, Vector3f(0, 0, 1), radius, 64));
    }
}

void RadialStyle::ClearDrawRadial() {
    auto painter = m_Scene->GetPainter3D();
    for (auto& drawHandle: m_DrawHandles) painter->Delete(drawHandle);
    m_DrawHandles.clear();
}

void RadialStyle::CallBack() {
    for (auto& callBack: m_PointMoveCallBacks) callBack.second();
}

void RadialStyle::MousePressEvent(IEvent event) {
    BasicStyle::MousePressEvent(event);

    m_SelectedPoint = nullptr;
    if (!m_Show) return;

    if (event.button != MouseButton::MiddleButton &&
        !((m_AllowLeftHandleDrag || m_CenterTranslationMode) &&
          event.button == MouseButton::LeftButton)) return;

    m_MVP = m_Interactor->GetMVP();
    m_InvertedMVP = m_MVP.invert();

    auto& pos = event.pos;

    const auto screenDistance = [this, &pos](const Point& point) {
        const igm::vec4 clip = m_MVP * igm::vec4(point[0], point[1], point[2], 1.f);
        if (clip.w <= 0.f) return std::numeric_limits<float>::max();
        const float x = (clip.x / clip.w + 1.f) * m_Interactor->GetWidth() * 0.5f;
        const float y = (1.f - clip.y / clip.w) * m_Interactor->GetHeight() * 0.5f;
        const float dx = x - pos.x, dy = y - pos.y;
        return std::sqrt(dx * dx + dy * dy);
    };
    const float distToStart = screenDistance(m_StartPoint);
    const float distToEnd = screenDistance(m_EndPoint);
    const bool startSelected = distToStart <= (m_CenterTranslationMode ? 26.f : 12.f);
    const bool endSelected = !m_CenterTranslationMode && distToEnd <= 12.f;

    if (startSelected && endSelected) {
        m_SelectedPoint =
                (distToStart <= distToEnd) ? &m_StartPoint : &m_EndPoint;
    } else if (startSelected) {
        m_SelectedPoint = &m_StartPoint;
    } else if (endSelected) {
        m_SelectedPoint = &m_EndPoint;
    }

    if (m_SelectedPoint == nullptr) return;

    igm::vec4 p{m_SelectedPoint->operator[](0), m_SelectedPoint->operator[](1),
                m_SelectedPoint->operator[](2), 1.f};
    p = m_MVP * p;
    m_SelectedNDCZ = p.z / p.w;
}

void RadialStyle::MouseMoveEvent(IEvent event) {
    if (!m_Show) return;
    if (m_SelectedPoint == nullptr) {
        m_MVP = m_Interactor->GetMVP();
        const auto distance = [this, &event](const Point& point) {
            const igm::vec4 clip = m_MVP * igm::vec4(point[0], point[1], point[2], 1.f);
            if (clip.w <= 0.f) return std::numeric_limits<float>::max();
            const float x = (clip.x / clip.w + 1.f) * m_Interactor->GetWidth() * 0.5f;
            const float y = (1.f - clip.y / clip.w) * m_Interactor->GetHeight() * 0.5f;
            const float dx = x - event.pos.x, dy = y - event.pos.y;
            return std::sqrt(dx * dx + dy * dy);
        };
        const float a = distance(m_StartPoint), b = distance(m_EndPoint);
        Point* hovered = nullptr;
        if (m_CenterTranslationMode) {
            if (a <= 26.f) hovered = &m_StartPoint;
        } else if (a <= 12.f || b <= 12.f) {
            hovered = a <= b ? &m_StartPoint : &m_EndPoint;
        }
        if (hovered != m_HoveredPoint) {
            m_HoveredPoint = hovered;
            ClearDrawRadial();
            DrawRadial();
        }
        return;
    }
    if (!IsInteractionActive()) return;

    igm::vec2 pos = event.pos;

    igm::vec2 NDC(2.0f * pos.x / m_Interactor->GetWidth() - 1.0f,
                  1.0f - (2.0f * pos.y / m_Interactor->GetHeight()));

    igm::vec4 Point_NDC{NDC, m_SelectedNDCZ, 1.f};
    igm::vec4 newPoint_WorldCoord = m_InvertedMVP * Point_NDC;
    newPoint_WorldCoord /= newPoint_WorldCoord.w;

    const Point newPoint(newPoint_WorldCoord.x, newPoint_WorldCoord.y,
                         newPoint_WorldCoord.z);
    if (m_MoveEndWithStart && m_SelectedPoint == &m_StartPoint)
        m_EndPoint = m_EndPoint + (newPoint - m_StartPoint);
    *m_SelectedPoint = newPoint;
    ClearDrawRadial();
    DrawRadial();
    CallBack();
}

void RadialStyle::MouseReleaseEvent(IEvent event) {
    BasicStyle::MouseReleaseEvent(event);
    m_SelectedPoint = nullptr;
}

void RadialStyle::SetShow(bool show) {
    if (m_Show == show) return;
    m_Show = show;
    if (m_Show == false) {
        ClearDrawRadial();
        m_HoveredPoint = nullptr;
        m_SelectedPoint = nullptr;
    }
    else
        DrawRadial();
}

bool RadialStyle::GetShow() const { return m_Show; }

void RadialStyle::SetPoint(const Point& startPoint, const Point& endPoint) {
    if (m_Show) ClearDrawRadial();
    m_StartPoint = startPoint;
    m_EndPoint = endPoint;
    m_MaxDis = (m_StartPoint - m_EndPoint).length() * 0.02;
    if (m_Show) DrawRadial();
}

const Point& RadialStyle::GetStartPoint() { return m_StartPoint; }

const Point& RadialStyle::GetEndPoint() { return m_EndPoint; }

void RadialStyle::_SetPointMoveCallBack(const std::string& name,
                                       std::function<void()> callBack) {
    m_PointMoveCallBacks[name] = callBack;
}

void RadialStyle::RemovePointMoveCallBack(const std::string& name) {
    m_PointMoveCallBacks.erase(name);
}
IGAME_NAMESPACE_END
