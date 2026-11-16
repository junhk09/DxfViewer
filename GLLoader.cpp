#include "pch.h"
#include "GLLoader.h"

#define GL_DEFINE_FUNCTION(ret, name, params) PFN_##name name = nullptr;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)
#undef GL_DEFINE_FUNCTION

bool LoadGLFunctions(const char** missing)
{
#define GL_LOAD_FUNCTION(ret, name, params) \
    name = reinterpret_cast<PFN_##name>(wglGetProcAddress(#name)); \
    if (!name) { if (missing) *missing = #name; return false; }
    GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
    return true;
}
