#include "pch.h"
#include "GLView_VBO.h"
#include "DxfDoc.h"
#include "MainFrm.h"
#include "PathOptimizer.h"

#include <algorithm>

// MFC가 실행 중에 이 클래스 객체를 만들 수 있게 등록 (문서 템플릿이 사용)
IMPLEMENT_DYNCREATE(CGLViewVBO, CView)

// 윈도우 메시지(생성·크기·마우스·타이머 …) → 처리 함수 연결표
BEGIN_MESSAGE_MAP(CGLViewVBO, CView)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_RBUTTONDOWN()
    ON_WM_RBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_CAPTURECHANGED()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// 이 파일 안에서만 쓰는 도우미 (이름 없는 namespace)
namespace {

// 정점 셰이더: 점 위치에 변환 행렬(uMVP)을 곱하고 색을 넘김
const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
uniform float uPointSize;
out vec3 vColor;
void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
    vColor = aColor;
}
)";

// 프래그먼트 셰이더: 색에 밝기(uBright)를 곱함 (강조·시뮬레이션 때 나머지를 어둡게)
const char* kFragmentShader = R"(#version 330 core
in vec3 vColor;
uniform float uBright;
out vec4 fragColor;
void main()
{
    fragColor = vec4(vColor * uBright, 1.0);
}
)";

// 배경색, 원주율, 회전 감도(마우스 1픽셀당 각도)
const float kBackground[3] = { 0.11f, 0.12f, 0.14f };
const double kPi = 3.14159265358979323846;
const double kOrbitDegPerPixel = 0.4;

// 색: 공이동(주황), 시작점(초록), 시뮬레이션 헤드(흰색), 강조(노랑), 끝점(빨강)
const unsigned int kTravelRgb = 0xFF9A1A;
const unsigned int kStartRgb = 0x55FF55;
const unsigned int kHeadRgb = 0xFFFFFF;
const unsigned int kSelectRgb = 0xFFE14D;
const unsigned int kEndRgb = 0xFF4040;
// 시뮬레이션 타이머 번호와 전체 재생 시간(초)
const UINT_PTR kSimTimer = 1;
const double kSimSeconds = 15.0;

// 두 점 사이 직선 거리
double Dist(const Vec3& a, const Vec3& b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 그릴 도형인지 (레이어가 있고 켜져 있고 점이 있음)
bool IsVisible(const DxfDrawing& dwg, const DxfEntity& e)
{
    int k = dwg.FindLayer(e.layer);
    return k >= 0 && dwg.layers[k].visible && !e.pts.empty();
}

// 공이동 선의 꺾은점들: 2D는 직선, 3D는 올라감 → 수평 → 내려감
std::vector<Vec3> TravelPath(const Vec3& a, const Vec3& b, const TravelModel& model)
{
    if (!model.lift || SamePoint(a, b))
        return { a, b };
    double top = std::max({ model.safeZ, a.z, b.z });
    return { a, { a.x, a.y, top }, { b.x, b.y, top }, b };
}

// 도형의 실제 색을 RGB로 (256 = BYLAYER면 레이어 색)
unsigned int EntityRgb(const DxfDrawing& dwg, const DxfEntity& e)
{
    int aci = e.color == 256 ? dwg.layers[dwg.FindLayer(e.layer)].color : e.color;
    aci = std::abs(aci);
    if (aci < 1 || aci > 255)
        aci = 7;
    return AciToRgb(aci);
}

// 셰이더 소스 하나를 컴파일. 실패하면 0과 오류 내용(log)
GLuint CompileShader(GLenum type, const char* source, CString& log)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024] = {};
        glGetShaderInfoLog(shader, sizeof(buf) - 1, nullptr, buf);
        log = CString(buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}

// 이 뷰가 보여 주는 문서(CDxfDoc)
CDxfDoc* CGLViewVBO::GetDocument() const
{
    return reinterpret_cast<CDxfDoc*>(m_pDocument);
}

// 창을 만들기 전 설정: OpenGL용 창 스타일(CS_OWNDC), 십자 커서, 더블클릭 받기
BOOL CGLViewVBO::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CView::PreCreateWindow(cs))
        return FALSE;
    cs.lpszClass = AfxRegisterWndClass(CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                       ::LoadCursor(nullptr, IDC_CROSS), nullptr, nullptr);
    cs.style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    return TRUE;
}

// 창이 만들어지면 OpenGL 초기화
int CGLViewVBO::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;
    m_glOk = InitGL();
    return 0;
}

// 오류 메시지를 저장하고 false 반환 (OnDraw가 화면에 표시함)
bool CGLViewVBO::Fail(const CString& message)
{
    m_glError = message;
    return false;
}

// OpenGL 준비: 픽셀 포맷, 3.3 core 컨텍스트, 함수 불러오기, vsync, 셰이더, VAO/VBO 6쌍, 축 데이터
bool CGLViewVBO::InitGL()
{
    m_hdc = ::GetDC(m_hWnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int format = ChoosePixelFormat(m_hdc, &pfd);
    if (!format || !SetPixelFormat(m_hdc, format, &pfd))
        return Fail(L"OpenGL 픽셀 포맷을 설정하지 못했습니다.");

    HGLRC legacy = wglCreateContext(m_hdc);
    if (!legacy)
        return Fail(L"OpenGL 컨텍스트를 만들지 못했습니다.");
    wglMakeCurrent(m_hdc, legacy);
    auto createContextAttribs = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (createContextAttribs)
    {
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        m_hrc = createContextAttribs(m_hdc, nullptr, attribs);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!m_hrc)
        return Fail(L"OpenGL 3.3 core 컨텍스트를 만들지 못했습니다. 그래픽 드라이버를 확인해 주세요.");
    wglMakeCurrent(m_hdc, m_hrc);

    const char* missing = nullptr;
    if (!LoadGLFunctions(&missing))
        return Fail(CString(L"OpenGL 함수를 불러오지 못했습니다: ") + CString(missing));

    if (auto swapInterval = reinterpret_cast<PFN_wglSwapIntervalEXT>(wglGetProcAddress("wglSwapIntervalEXT")))
        swapInterval(1);

    m_program = CreateProgram();
    if (!m_program)
        return false;
    m_locMvp = glGetUniformLocation(m_program, "uMVP");
    m_locBright = glGetUniformLocation(m_program, "uBright");
    m_locPointSize = glGetUniformLocation(m_program, "uPointSize");

    auto makeVertexArray = [](GLuint& vao, GLuint& vbo) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    };
    makeVertexArray(m_vao, m_vbo);
    makeVertexArray(m_travelVao, m_travelVbo);
    makeVertexArray(m_simVao, m_simVbo);
    makeVertexArray(m_headVao, m_headVbo);
    makeVertexArray(m_selVao, m_selVbo);
    makeVertexArray(m_axisVao, m_axisVbo);

    const float axis[] = {
        0, 0, 0, 1.0f, 0.3f, 0.3f,   1, 0, 0, 1.0f, 0.3f, 0.3f,
        0, 0, 0, 0.3f, 1.0f, 0.3f,   0, 1, 0, 0.3f, 1.0f, 0.3f,
        0, 0, 0, 0.4f, 0.5f, 1.0f,   0, 0, 1, 0.4f, 0.5f, 1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(axis), axis, GL_STATIC_DRAW);
    glBindVertexArray(0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glLineWidth(1.5f);
    return true;
}

// 정점·프래그먼트 셰이더를 컴파일하고 하나의 프로그램으로 링크
GLuint CGLViewVBO::CreateProgram()
{
    CString log;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader, log);
    if (!vs)
    {
        Fail(L"버텍스 셰이더 컴파일 실패:\n" + log);
        return 0;
    }
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader, log);
    if (!fs)
    {
        glDeleteShader(vs);
        Fail(L"프래그먼트 셰이더 컴파일 실패:\n" + log);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[1024] = {};
        glGetProgramInfoLog(program, sizeof(buf) - 1, nullptr, buf);
        glDeleteProgram(program);
        Fail(L"셰이더 링크 실패:\n" + CString(buf));
        return 0;
    }
    return program;
}

// 창이 닫힐 때: 타이머를 끄고 OpenGL 정리
void CGLViewVBO::OnDestroy()
{
    KillTimer(kSimTimer);
    ShutdownGL();
    CView::OnDestroy();
}

// OpenGL 자원(버퍼, VAO, 셰이더, 프레임버퍼, 컨텍스트) 해제
void CGLViewVBO::ShutdownGL()
{
    if (m_hrc)
    {
        wglMakeCurrent(m_hdc, m_hrc);
        if (m_glOk)
        {
            glDeleteBuffers(1, &m_vbo);
            glDeleteVertexArrays(1, &m_vao);
            glDeleteBuffers(1, &m_axisVbo);
            glDeleteVertexArrays(1, &m_axisVao);
            for (GLuint* buffer : { &m_travelVbo, &m_simVbo, &m_headVbo, &m_selVbo })
                glDeleteBuffers(1, buffer);
            for (GLuint* array : { &m_travelVao, &m_simVao, &m_headVao, &m_selVao })
                glDeleteVertexArrays(1, array);
            glDeleteProgram(m_program);
            if (m_fbo)
            {
                glDeleteFramebuffers(1, &m_fbo);
                glDeleteRenderbuffers(1, &m_rbo);
            }
        }
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_hrc);
        m_hrc = nullptr;
    }
    if (m_hdc)
    {
        ::ReleaseDC(m_hWnd, m_hdc);
        m_hdc = nullptr;
    }
    m_glOk = false;
}

// 계단 현상 줄이기(MSAA 4배)용 프레임버퍼를 창 크기에 맞게 준비. 안 되면 일반 방식으로 그림
void CGLViewVBO::EnsureMsaa(int w, int h)
{
    if (m_msaaFailed || (m_fbo && w == m_fbW && h == m_fbH))
        return;
    if (!m_fbo)
    {
        glGenFramebuffers(1, &m_fbo);
        glGenRenderbuffers(1, &m_rbo);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        m_msaaFailed = true;
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteRenderbuffers(1, &m_rbo);
        m_fbo = m_rbo = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_fbW = w;
    m_fbH = h;
}

// 도면 정점 데이터 만들기: 레이어별·선/점별로 모아 VBO에 올림 (구간은 m_ranges에 기록)
void CGLViewVBO::BuildBuffers()
{
    const DxfDrawing& dwg = GetDocument()->m_dwg;
    m_ranges.clear();

    m_origin = {};
    if (dwg.hasExtents)
        m_origin = { (dwg.extMin.x + dwg.extMax.x) / 2, (dwg.extMin.y + dwg.extMax.y) / 2,
                     (dwg.extMin.z + dwg.extMax.z) / 2 };

    std::vector<std::vector<const DxfEntity*>> byLayer(dwg.layers.size());
    for (const DxfEntity& e : dwg.entities)
        if (int k = dwg.FindLayer(e.layer); k >= 0)
            byLayer[k].push_back(&e);

    std::vector<float> v;
    for (size_t layer = 0; layer < byLayer.size(); ++layer)
    {
        for (GLenum mode : { static_cast<GLenum>(GL_LINES), static_cast<GLenum>(GL_POINTS) })
        {
            GLint first = static_cast<GLint>(v.size() / 6);
            for (const DxfEntity* e : byLayer[layer])
            {
                unsigned int rgb = EntityRgb(dwg, *e);
                if (mode == GL_POINTS && e->type == EntType::Point)
                {
                    PushVertex(v, e->pts[0], rgb);
                }
                else if (mode == GL_LINES && e->type != EntType::Point)
                {
                    for (size_t k = 1; k < e->pts.size(); ++k)
                    {
                        PushVertex(v, e->pts[k - 1], rgb);
                        PushVertex(v, e->pts[k], rgb);
                    }
                }
            }
            GLsizei count = static_cast<GLsizei>(v.size() / 6) - first;
            if (count > 0)
                m_ranges.push_back({ static_cast<int>(layer), first, count, mode });
        }
    }

    wglMakeCurrent(m_hdc, m_hrc);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// 정점 하나(위치 3 + 색 3 = float 6개)를 v에 추가. 위치는 도면 중심(m_origin) 기준
void CGLViewVBO::PushVertex(std::vector<float>& v, const Vec3& p, unsigned int rgb) const
{
    v.push_back(static_cast<float>(p.x - m_origin.x));
    v.push_back(static_cast<float>(p.y - m_origin.y));
    v.push_back(static_cast<float>(p.z - m_origin.z));
    v.push_back(((rgb >> 16) & 0xFF) / 255.0f);
    v.push_back(((rgb >> 8) & 0xFF) / 255.0f);
    v.push_back((rgb & 0xFF) / 255.0f);
}

// 공이동 선(도형 끝 → 다음 도형 시작) 정점을 만들어 VBO에 올림
void CGLViewVBO::BuildTravel()
{
    const DxfDrawing& dwg = GetDocument()->m_dwg;
    const TravelModel& model = GetDocument()->m_travelModel;
    std::vector<float> v;
    Vec3 pos = CDxfDoc::PathStart();
    for (const DxfEntity& e : dwg.entities)
    {
        if (!IsVisible(dwg, e))
            continue;
        if (Dist(pos, e.pts.front()) > 0)
        {
            std::vector<Vec3> path = TravelPath(pos, e.pts.front(), model);
            for (size_t k = 1; k < path.size(); ++k)
            {
                PushVertex(v, path[k - 1], kTravelRgb);
                PushVertex(v, path[k], kTravelRgb);
            }
        }
        pos = e.pts.back();
    }
    m_travelCount = static_cast<GLsizei>(v.size() / 6);
    PushVertex(v, CDxfDoc::PathStart(), kStartRgb);

    wglMakeCurrent(m_hdc, m_hrc);
    glBindBuffer(GL_ARRAY_BUFFER, m_travelVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// 공이동 선 보이기 / 숨기기
void CGLViewVBO::ToggleTravel()
{
    m_showTravel = !m_showTravel;
    Invalidate(FALSE);
}

// 시뮬레이션 시작 / 정지: 공이동 + 가공 선분을 순서대로 모아 VBO에 올리고 타이머 시작
void CGLViewVBO::ToggleSimulation()
{
    if (m_simActive)
    {
        StopSimulation();
        return;
    }
    if (!m_glOk)
        return;
    const DxfDrawing& dwg = GetDocument()->m_dwg;
    m_simSegs.clear();
    double total = 0;
    auto add = [&](const Vec3& a, const Vec3& b, unsigned int rgb) {
        double len = Dist(a, b);
        if (len <= 0)
            return;
        total += len;
        m_simSegs.push_back({ a, b, rgb, total });
    };
    Vec3 pos = CDxfDoc::PathStart();
    const TravelModel& model = GetDocument()->m_travelModel;
    for (const DxfEntity& e : dwg.entities)
    {
        if (!IsVisible(dwg, e))
            continue;
        std::vector<Vec3> path = TravelPath(pos, e.pts.front(), model);
        for (size_t k = 1; k < path.size(); ++k)
            add(path[k - 1], path[k], kTravelRgb);
        unsigned int rgb = EntityRgb(dwg, e);
        for (size_t k = 1; k < e.pts.size(); ++k)
            add(e.pts[k - 1], e.pts[k], rgb);
        pos = e.pts.back();
    }
    if (m_simSegs.empty())
        return;

    std::vector<float> v;
    v.reserve(m_simSegs.size() * 12);
    for (const SimSegment& s : m_simSegs)
    {
        PushVertex(v, s.a, s.rgb);
        PushVertex(v, s.b, s.rgb);
    }
    wglMakeCurrent(m_hdc, m_hrc);
    glBindBuffer(GL_ARRAY_BUFFER, m_simVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_simProgress = 0;
    m_simActive = true;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    m_simLastTick = now.QuadPart;
    SetTimer(kSimTimer, 15, nullptr);
    Invalidate(FALSE);
}

// 시뮬레이션 정지
void CGLViewVBO::StopSimulation()
{
    if (!m_simActive)
        return;
    KillTimer(kSimTimer);
    m_simActive = false;
    Invalidate(FALSE);
}

// 시뮬레이션 타이머(15ms)마다 다시 그리기 요청
void CGLViewVBO::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != kSimTimer)
    {
        CView::OnTimer(nIDEvent);
        return;
    }
    Invalidate(FALSE);
}

// 지난 시간만큼 시뮬레이션 진행 거리를 늘림 (전체를 kSimSeconds초 동안 재생)
void CGLViewVBO::AdvanceSimulation()
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    double seconds = static_cast<double>(now.QuadPart - m_simLastTick) / frequency.QuadPart;
    m_simLastTick = now.QuadPart;

    double total = m_simSegs.back().end;
    if (m_simProgress >= total)
        return;
    m_simProgress += total * seconds / kSimSeconds;
    if (m_simProgress >= total)
    {
        m_simProgress = total;
        KillTimer(kSimTimer);
    }
}

// 지금까지 진행한 선분과 움직이는 헤드(흰 점)를 그림
void CGLViewVBO::DrawSimulation()
{
    size_t done = std::upper_bound(m_simSegs.begin(), m_simSegs.end(), m_simProgress,
        [](double progress, const SimSegment& s) { return progress < s.end; }) - m_simSegs.begin();
    glBindVertexArray(m_simVao);
    if (done > 0)
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(done * 2));

    std::vector<float> v;
    Vec3 head = m_simSegs.back().b;
    if (done < m_simSegs.size())
    {
        const SimSegment& s = m_simSegs[done];
        double startLen = done > 0 ? m_simSegs[done - 1].end : 0.0;
        double t = (m_simProgress - startLen) / (s.end - startLen);
        head = { s.a.x + (s.b.x - s.a.x) * t, s.a.y + (s.b.y - s.a.y) * t, s.a.z + (s.b.z - s.a.z) * t };
        PushVertex(v, s.a, s.rgb);
        PushVertex(v, head, s.rgb);
    }
    PushVertex(v, head, kHeadRgb);
    glBindVertexArray(m_headVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_headVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
    GLsizei count = static_cast<GLsizei>(v.size() / 6);
    if (count == 3)
        glDrawArrays(GL_LINES, 0, 2);
    glUniform1f(m_locPointSize, 10.0f);
    glDrawArrays(GL_POINTS, count - 1, 1);
    glUniform1f(m_locPointSize, 5.0f);
}

// 화면 그리기 전체: 배경 → 도면(레이어별) → 공이동 / 시뮬레이션 / 강조 → 3D 축 → 화면에 표시
void CGLViewVBO::Render()
{
    if (m_simActive)
        AdvanceSimulation();
    wglMakeCurrent(m_hdc, m_hrc);
    EnsureMsaa(m_w, m_h);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_w, m_h);
    glClearColor(kBackground[0], kBackground[1], kBackground[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_program);
    glUniform1f(m_locBright, 1.0f);
    glUniform1f(m_locPointSize, 5.0f);
    if (!m_ranges.empty())
    {
        Vec3 shift{ m_origin.x - m_target.x, m_origin.y - m_target.y, m_origin.z - m_target.z };
        float mvp[16];
        BuildMatrix(2.0 * m_scale / m_w, 2.0 * m_scale / m_h, shift, mvp);
        const auto& layers = GetDocument()->m_dwg.layers;
        glUniformMatrix4fv(m_locMvp, 1, GL_FALSE, mvp);

        const bool dim = m_simActive || m_hasSelection;
        glUniform1f(m_locBright, dim ? 0.3f : 1.0f);
        glBindVertexArray(m_vao);
        for (const DrawRange& r : m_ranges)
            if (layers[r.layer].visible)
                glDrawArrays(r.mode, r.first, r.count);
        glUniform1f(m_locBright, 1.0f);

        if (m_simActive)
        {
            DrawSimulation();
        }
        else
        {
            if (m_showTravel)
            {
                glUniform1f(m_locBright, dim ? 0.3f : 1.0f);
                glBindVertexArray(m_travelVao);
                if (m_travelCount > 0)
                    glDrawArrays(GL_LINES, 0, m_travelCount);
                glUniform1f(m_locPointSize, 9.0f);
                glDrawArrays(GL_POINTS, m_travelCount, 1);
                glUniform1f(m_locPointSize, 5.0f);
                glUniform1f(m_locBright, 1.0f);
            }
            if (m_hasSelection)
            {
                glBindVertexArray(m_selVao);
                glLineWidth(3.5f);
                if (m_selLineCount > 0)
                    glDrawArrays(GL_LINES, 0, m_selLineCount);
                glLineWidth(1.5f);
                glUniform1f(m_locPointSize, 10.0f);
                glDrawArrays(GL_POINTS, m_selLineCount, m_selPointCount);
                glUniform1f(m_locPointSize, 5.0f);
            }
        }
    }

    if (m_3d)
    {
        int size = ScaleDpi(72);
        glViewport(ScaleDpi(6), ScaleDpi(6), size, size);
        float axisMvp[16];
        BuildMatrix(0.8, 0.8, Vec3{}, axisMvp);
        glUniformMatrix4fv(m_locMvp, 1, GL_FALSE, axisMvp);
        glBindVertexArray(m_axisVao);
        glDrawArrays(GL_LINES, 0, 6);
        glViewport(0, 0, m_w, m_h);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    if (m_fbo)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, m_w, m_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    SwapBuffers(m_hdc);
}

// WM_PAINT 때 MFC가 부름: OpenGL 초기화에 실패했으면 오류 글자, 아니면 Render
void CGLViewVBO::OnDraw(CDC* pDC)
{
    if (!m_glOk)
    {
        CRect rc;
        GetClientRect(&rc);
        pDC->FillSolidRect(rc, RGB(30, 30, 30));
        pDC->SetTextColor(RGB(255, 120, 120));
        pDC->SetBkMode(TRANSPARENT);
        rc.DeflateRect(16, 16);
        pDC->DrawText(m_glError.IsEmpty() ? CString(L"OpenGL 초기화 실패") : m_glError, rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }
    Render();
}

// 문서가 바뀌었을 때 (hint에 따라): 선택이면 강조만, 그 밖에는 정점·공이동·강조를 다시 만듦
void CGLViewVBO::OnUpdate(CView*, LPARAM lHint, CObject*)
{
	if (lHint == kHintSelection) // 선택이 바뀌었을 때
    {
        if (m_glOk)
            BuildSelection(); //강조데이터
		FocusSelection();   // 선택 영역으로 줌
		Invalidate(FALSE);  // 화면 갱신 요청
        return;
    }

    StopSimulation();
    if ((lHint == kHintReload || lHint == kHintCutOrder) && m_glOk)
        BuildBuffers();
    if (lHint == kHintReload)
    {
        const DxfDrawing& dwg = GetDocument()->m_dwg;
        double span = std::max(dwg.extMax.x - dwg.extMin.x, dwg.extMax.y - dwg.extMin.y);
        m_3d = dwg.hasExtents && dwg.extMax.z - dwg.extMin.z > std::max(span, 1.0) * 1e-6;
        m_yaw = m_3d ? kIsoYaw : 0;
        m_pitch = m_3d ? kIsoPitch : 0;
        m_showTravel = false;
        ZoomExtents();
        if (CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
            frame->ShowLiftClearance(GetDocument()->m_liftClearanceMm);
    }
    if (m_glOk)
    {
        BuildTravel();
        BuildSelection();
    }
    Invalidate(FALSE);
}

// 도면 전체가 화면에 꽉 차게 (92%) 맞춤
void CGLViewVBO::ZoomExtents()
{
    CRect rc;
    GetClientRect(&rc);
    int w = std::max(1, rc.Width()), h = std::max(1, rc.Height());
    const DxfDrawing& dwg = GetDocument()->m_dwg;
    if (!dwg.hasExtents)
    {
        m_target = {};
        m_scale = 1;
        return;
    }
    m_w = w;
    m_h = h;
    ZoomToBox(dwg.extMin, dwg.extMax, 0.92);
}

// 상자(lo ~ hi)가 화면의 fill 비율만큼 차지하도록 중심(m_target)과 배율(m_scale)을 정함
void CGLViewVBO::ZoomToBox(const Vec3& lo, const Vec3& hi, double fill)
{
    m_target = { (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2 };

    double r[3][3];
    Rotation(r);
    double half[3] = { (hi.x - lo.x) / 2, (hi.y - lo.y) / 2, (hi.z - lo.z) / 2 };
    double hw = 0, hh = 0;
    for (int j = 0; j < 3; ++j)
    {
        hw += std::fabs(r[0][j]) * half[j];
        hh += std::fabs(r[1][j]) * half[j];
    }
    double span = std::max(hw, hh);
    if (span < 1e-9)
        return;
    hw = std::max(hw, span * 1e-3);
    hh = std::max(hh, span * 1e-3);
    m_scale = std::min(m_w / (2 * hw), m_h / (2 * hh)) * fill;
}

// 강조 데이터 만들기: m_selectedLog에서 나온 도형을 노랑 선 + 시작(초록) / 끝(빨강) 점으로 VBO에 올림
void CGLViewVBO::BuildSelection()
{
    const CDxfDoc* doc = GetDocument();
    const DxfDrawing& dwg = doc->m_dwg;
    std::vector<float> lines, points;
    m_hasSelection = false;
    for (const DxfEntity& e : dwg.entities)
    {
        if (e.pts.empty())
            continue;
        if (!CDxfDoc::IsFromLog(e, doc->m_selectedLog))
            continue;
        const unsigned int rgb = kSelectRgb;
        for (const Vec3& p : e.pts)
        {
            if (!m_hasSelection)
            {
                m_selLo = m_selHi = p;
                m_hasSelection = true;
            }
            m_selLo = { std::min(m_selLo.x, p.x), std::min(m_selLo.y, p.y), std::min(m_selLo.z, p.z) };
            m_selHi = { std::max(m_selHi.x, p.x), std::max(m_selHi.y, p.y), std::max(m_selHi.z, p.z) };
        }
        if (e.type == EntType::Point)
        {
            PushVertex(points, e.pts[0], rgb);
            continue;
        }
        for (size_t k = 1; k < e.pts.size(); ++k)
        {
            PushVertex(lines, e.pts[k - 1], rgb);
            PushVertex(lines, e.pts[k], rgb);
        }
        PushVertex(points, e.pts.back(), kEndRgb);
        PushVertex(points, e.pts.front(), kStartRgb);
    }
    m_selLineCount = static_cast<GLsizei>(lines.size() / 6);
    m_selPointCount = static_cast<GLsizei>(points.size() / 6);
    lines.insert(lines.end(), points.begin(), points.end());

    wglMakeCurrent(m_hdc, m_hrc);
    glBindBuffer(GL_ARRAY_BUFFER, m_selVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(float)),
                 lines.empty() ? nullptr : lines.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// 강조한 도형으로 확대 (화면의 절반)
void CGLViewVBO::FocusSelection()
{
    if (m_hasSelection)
        ZoomToBox(m_selLo, m_selHi, 0.5);   // 강조한 도형이 화면의 절반을 차지하도록 확대
}

// 보는 방향(yaw, pitch)을 정하고 전체 보기로 맞춤 (위·앞·오른쪽·등각 버튼)
void CGLViewVBO::SetView(double yawDeg, double pitchDeg)
{
    if (yawDeg != 0 || pitchDeg != 0)
        m_3d = true;
    m_yaw = yawDeg;
    m_pitch = pitchDeg;
    ZoomExtents();
    Invalidate(FALSE);
}

// 2D / 3D 모드 전환 (3D는 등각 보기, 2D는 위에서 보기)
void CGLViewVBO::SetMode3D(bool on)
{
    if (on == m_3d)
        return;
    m_3d = on;
    if (on)
        SetView(kIsoYaw, kIsoPitch);
    else
        SetView(0, 0);
}

// yaw · pitch 각도로 회전 행렬 계산
void CGLViewVBO::Rotation(double r[3][3]) const
{
    double yaw = m_yaw * kPi / 180, pitch = m_pitch * kPi / 180;
    double c = std::cos(yaw), s = std::sin(yaw);
    double cp = std::cos(pitch), sp = std::sin(pitch);
    r[0][0] = c;       r[0][1] = -s;      r[0][2] = 0;
    r[1][0] = cp * s;  r[1][1] = cp * c;  r[1][2] = -sp;
    r[2][0] = sp * s;  r[2][1] = sp * c;  r[2][2] = cp;
}

// 회전 + 이동 + 확대를 합친 4x4 변환 행렬 (셰이더의 uMVP)
void CGLViewVBO::BuildMatrix(double kx, double ky, const Vec3& shift, float out[16]) const
{
    double r[3][3];
    Rotation(r);
    std::fill(out, out + 16, 0.0f);
    const double k[2] = { kx, ky };
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 3; ++j)
            out[j * 4 + i] = static_cast<float>(k[i] * r[i][j]);
        out[12 + i] = static_cast<float>(k[i] * (r[i][0] * shift.x + r[i][1] * shift.y + r[i][2] * shift.z));
    }
    out[15] = 1.0f;
}

// 배경 지우기 생략 (OpenGL이 전부 그리므로 깜빡임 방지)
BOOL CGLViewVBO::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

// 창 크기가 바뀌면 크기를 저장하고 다시 그리기
void CGLViewVBO::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    m_w = std::max(1, cx);
    m_h = std::max(1, cy);
    Invalidate(FALSE);
}

// 화면 픽셀 좌표 → 도면 좌표 (상태 표시줄 좌표, 휠 확대 기준점)
Vec3 CGLViewVBO::ScreenToWorld(CPoint p) const
{
    double r[3][3];
    Rotation(r);
    double u = (p.x - m_w / 2.0) / m_scale;
    double v = -(p.y - m_h / 2.0) / m_scale;
    return { m_target.x + r[0][0] * u + r[1][0] * v,
             m_target.y + r[0][1] * u + r[1][1] * v,
             m_target.z + r[0][2] * u + r[1][2] * v };
}

// 휠: 마우스 위치를 기준으로 확대 / 축소
BOOL CGLViewVBO::OnMouseWheel(UINT, short zDelta, CPoint pt)
{
    ScreenToClient(&pt);
    Vec3 w = ScreenToWorld(pt);
    double factor = std::pow(1.2, zDelta / 120.0);
    m_scale = std::clamp(m_scale * factor, 1e-9, 1e12);
    double r[3][3];
    Rotation(r);
    double u = (pt.x - m_w / 2.0) / m_scale;
    double v = -(pt.y - m_h / 2.0) / m_scale;
    m_target = { w.x - r[0][0] * u - r[1][0] * v,
                 w.y - r[0][1] * u - r[1][1] * v,
                 w.z - r[0][2] * u - r[1][2] * v };
    Invalidate(FALSE);
    return TRUE;
}

// 드래그 시작 (마우스 캡처, 이동 또는 회전 모드)
void CGLViewVBO::BeginDrag(CPoint point, Drag mode)
{
    SetFocus();
    SetCapture();
    m_drag = mode;
    m_lastMouse = point;
}

// 드래그 끝
void CGLViewVBO::EndDrag()
{
    if (m_drag != Drag::None)
    {
        m_drag = Drag::None;
        ReleaseCapture();
    }
}

// 마우스 버튼: 왼쪽 · 가운데 = 이동, 오른쪽 = 회전 (3D일 때만)
void CGLViewVBO::OnLButtonDown(UINT, CPoint point) { BeginDrag(point, Drag::Pan); }
void CGLViewVBO::OnLButtonUp(UINT, CPoint) { EndDrag(); }
void CGLViewVBO::OnMButtonDown(UINT, CPoint point) { BeginDrag(point, Drag::Pan); }
void CGLViewVBO::OnMButtonUp(UINT, CPoint) { EndDrag(); }
void CGLViewVBO::OnRButtonDown(UINT, CPoint point)
{
    if (m_3d)
        BeginDrag(point, Drag::Orbit);
}
void CGLViewVBO::OnRButtonUp(UINT, CPoint) { EndDrag(); }

// 다른 창이 마우스를 가져가면 드래그 취소
void CGLViewVBO::OnCaptureChanged(CWnd* pWnd)
{
    m_drag = Drag::None;
    CView::OnCaptureChanged(pWnd);
}

// 도면 화면 더블클릭: 전체 보기
void CGLViewVBO::OnLButtonDblClk(UINT, CPoint)
{
    ZoomExtents();
    Invalidate(FALSE);
}

// 마우스 이동: 드래그 중이면 이동 / 회전하고, 상태 표시줄에 좌표 표시
void CGLViewVBO::OnMouseMove(UINT, CPoint point)
{
    int dx = point.x - m_lastMouse.x, dy = point.y - m_lastMouse.y;
    if (m_drag == Drag::Pan)
    {
        double r[3][3];
        Rotation(r);
        double u = dx / m_scale, v = -dy / m_scale;
        m_target = { m_target.x - r[0][0] * u - r[1][0] * v,
                     m_target.y - r[0][1] * u - r[1][1] * v,
                     m_target.z - r[0][2] * u - r[1][2] * v };
        Invalidate(FALSE);
    }
    else if (m_drag == Drag::Orbit)
    {
        m_yaw = std::fmod(m_yaw + dx * kOrbitDegPerPixel, 360.0);
        m_pitch = std::clamp(m_pitch - dy * kOrbitDegPerPixel, -180.0, 0.0);
        Invalidate(FALSE);
    }
    m_lastMouse = point;

    if (CMainFrame* frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
    {
        Vec3 w = ScreenToWorld(point);
        CString text;
        if (m_3d)
            text.Format(L"X: %.4f  Y: %.4f  Z: %.4f", w.x, w.y, w.z);
        else
            text.Format(L"X: %.4f  Y: %.4f", w.x, w.y);
        frame->SetCoordText(text);
    }
}
