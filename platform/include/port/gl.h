// The OpenGL flavour each target renders with (D-008, D-017).
//
//   Android  OpenGL ES 3.2 (GalaxyQuest's original target)
//   Web      OpenGL ES 3.0 = WebGL2 (Emscripten)
//   macOS    OpenGL 4.1 core
//
// The renderer uses only what all three share: ES 3.0 plus integer textures
// and uniform blocks, no shader storage buffers, no separate vertex attribute
// binding, no texture storage.  Per-draw data lives in data textures read
// with texelFetch (gl_shadergen.cpp).
#pragma once

#if defined(__ANDROID__)
#include <GLES3/gl32.h>
#define PORT_GL_ES 1
#define PORT_GL_ANDROID 1
#elif defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#define PORT_GL_ES 1
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#define PORT_GL_ES 0
#else
#error "port/gl.h: no OpenGL configuration for this platform yet"
#endif

namespace gpu {

// First lines of every generated shader.
inline const char* glslPrelude() {
#if PORT_GL_ES
    return "#version 300 es\n"
           "precision highp float;\nprecision highp int;\n"
           "precision highp sampler2D;\nprecision highp usampler2D;\n";
#else
    return "#version 410 core\n";
#endif
}

// Data textures holding the renderer's per-frame arrays: kDataTexWidth texels
// per row, so element i sits at (i % width, i / width).
constexpr int kDataTexWidth = 2048;
// Texture units the data textures occupy (units 0-7 are the TEV's).
constexpr int kUnitRows = 8, kUnitRecs = 9, kUnitStates = 10, kUnitVtxRec = 11;

}  // namespace gpu
