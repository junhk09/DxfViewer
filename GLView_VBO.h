#pragma once

#include "GLLoader.h"
#include "DxfParser.h"

class CDxfDoc;

class CGLViewVBO : public CView
{
protected:
    CGLViewVBO() = default;
    DECLARE_DYNCREATE(CGLViewVBO)

public:
    static constexpr double kIsoYaw = 45.0;
    static constexpr double kIsoPitch = -54.7356;

    CDxfDoc* GetDocument() const;
    void ZoomExtents();
    void SetView(double yawDeg, double pitchDeg);
    bool Is3D() const { return m_3d; }
    void SetMode3D(bool on);

    bool IsTravelShown() const { return m_showTravel; }
    void ToggleTravel();
    bool IsSimulating() const { return m_simActive; }
    void ToggleSimulation();

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    void OnDraw(CDC* pDC) override;
    void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) override;

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    DECLARE_MESSAGE_MAP()

private:
    class DrawRange
    {
    public:
        int layer;
        GLint first;
        GLsizei count;
        GLenum mode;
    };

    enum class Drag { None, Pan, Orbit };

    class SimSegment
    {
    public:
        Vec3 a, b;
        unsigned int rgb;
        double end;
    };

    bool InitGL();
    void ShutdownGL();
    bool Fail(const CString& message);
    GLuint CreateProgram();
    void EnsureMsaa(int w, int h);
    void BuildBuffers();
    void BuildTravel();
    void BuildSelection();
    void FocusSelection(bool zoom);
    void ZoomToBox(const Vec3& lo, const Vec3& hi, double fill);
    void Render();
    void AdvanceSimulation();
    void DrawSimulation();
    void StopSimulation();
    void PushVertex(std::vector<float>& v, const Vec3& p, unsigned int rgb) const;
    void BeginDrag(CPoint point, Drag mode);
    void EndDrag();
    void Rotation(double r[3][3]) const;
    void BuildMatrix(double kx, double ky, const Vec3& shift, float out[16]) const;
    Vec3 ScreenToWorld(CPoint p) const;

    HDC m_hdc = nullptr;
    HGLRC m_hrc = nullptr;
    bool m_glOk = false;
    CString m_glError;

    GLuint m_program = 0, m_vao = 0, m_vbo = 0;
    GLuint m_axisVao = 0, m_axisVbo = 0;
    GLuint m_travelVao = 0, m_travelVbo = 0;
    GLuint m_simVao = 0, m_simVbo = 0;
    GLuint m_headVao = 0, m_headVbo = 0;
    GLuint m_selVao = 0, m_selVbo = 0;
    GLint m_locMvp = -1, m_locBright = -1, m_locPointSize = -1;
    GLuint m_fbo = 0, m_rbo = 0;
    int m_fbW = 0, m_fbH = 0;
    bool m_msaaFailed = false;
    std::vector<DrawRange> m_ranges;

    int m_w = 1, m_h = 1;
    bool m_3d = false;
    Vec3 m_target;
    double m_scale = 1;
    double m_yaw = 0, m_pitch = 0;
    Vec3 m_origin;

    Drag m_drag = Drag::None;
    CPoint m_lastMouse;

    bool m_showTravel = false;
    GLsizei m_travelCount = 0;

    bool m_hasSelection = false;
    GLsizei m_selLineCount = 0;
    GLsizei m_selPointCount = 0;
    Vec3 m_selLo, m_selHi;

    bool m_simActive = false;
    std::vector<SimSegment> m_simSegs;
    double m_simProgress = 0;
    LONGLONG m_simLastTick = 0;
};
