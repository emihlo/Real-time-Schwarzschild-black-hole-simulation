#include "gl_api.h"

#include <cstdio>

#define X(ret, name, sig) PFN_##name gl##name = nullptr;
GL_FUNCTIONS(X)
#undef X

void glLoadAll(GLProc (*getProcAddress)(const char*))
{
#define X(ret, name, sig)                                            \
    gl##name = reinterpret_cast<PFN_##name>(getProcAddress("gl" #name)); \
    if (!gl##name) std::fprintf(stderr, "[gl] missing function: gl%s\n", #name);
    GL_FUNCTIONS(X)
#undef X
}
