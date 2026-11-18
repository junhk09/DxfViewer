#include "pch.h"
#include "GLLoader.h"

// 사용하는 OpenGL 함수마다 함수 포인터 변수를 하나씩 만듦 (처음에는 nullptr)
#define GL_DEFINE_FUNCTION(ret, name, params) PFN_##name name = nullptr;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)
#undef GL_DEFINE_FUNCTION

// OpenGL 3.3 함수들의 주소를 그래픽 드라이버에서 받아 위 포인터에 채움. 하나라도 없으면 false와 그 함수 이름
bool LoadGLFunctions(const char** missing)
{
#define GL_LOAD_FUNCTION(ret, name, params) \
    name = reinterpret_cast<PFN_##name>(wglGetProcAddress(#name)); \
    if (!name) { if (missing) *missing = #name; return false; }
    GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
    return true;
}
