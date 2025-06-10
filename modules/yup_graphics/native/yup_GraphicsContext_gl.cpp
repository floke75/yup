/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#if YUP_RIVE_USE_OPENGL || YUP_LINUX || YUP_WASM || YUP_ANDROID
#include "rive/renderer/rive_renderer.hpp"
#include "rive/renderer/gl/gles3.hpp"
#include "rive/renderer/gl/render_buffer_gl_impl.hpp"
#include "rive/renderer/gl/render_context_gl_impl.hpp"
#include "rive/renderer/gl/render_target_gl.hpp"

namespace yup
{

#if RIVE_DESKTOP_GL && DEBUG
static void GLAPIENTRY err_msg_callback (GLenum source,
                                         GLenum type,
                                         GLuint id,
                                         GLenum severity,
                                         GLsizei length,
                                         const GLchar* message,
                                         const void* userParam)
{
    if (type == GL_DEBUG_TYPE_ERROR)
    {
        printf ("GL ERROR: %s\n", message);
        fflush (stdout);

        assert (false);
    }
    else if (type == GL_DEBUG_TYPE_PERFORMANCE)
    {
        if (strcmp (message,
                    "API_ID_REDUNDANT_FBO performance warning has been generated. Redundant state "
                    "change in glBindFramebuffer API call, FBO 0, \"\", already bound.")
            == 0)
        {
            return;
        }

        if (strstr (message, "is being recompiled based on GL state."))
        {
            return;
        }

        printf ("GL PERF: %s\n", message);
        fflush (stdout);
    }
}
#endif

class LowLevelRenderContextGL : public GraphicsContext
{
public:
    LowLevelRenderContextGL (Options options)
    {
#if RIVE_DESKTOP_GL
        // Load the OpenGL API using glad.
        if (! gladLoadCustomLoader ((GLADloadproc) options.loaderFunction))
        {
            fprintf (stderr, "Failed to initialize glad.\n");
            exit (-1);
        }
#endif

        m_plsContext = rive::gpu::RenderContextGLImpl::MakeContext (rive::gpu::RenderContextGLImpl::ContextOptions());
        if (! m_plsContext)
        {
            fprintf (stderr, "Failed to create a renderer.\n");
            exit (-1);
        }

        printf ("GL_VENDOR:   %s\n", glGetString (GL_VENDOR));
        printf ("GL_RENDERER: %s\n", glGetString (GL_RENDERER));
        printf ("GL_VERSION:  %s\n", glGetString (GL_VERSION));

#if RIVE_DESKTOP_GL
        printf ("GL_ANGLE_shader_pixel_local_storage_coherent: %i\n", GLAD_GL_ANGLE_shader_pixel_local_storage_coherent);
#endif

#if DEBUG && ! RIVE_ANDROID
        int n;
        glGetIntegerv (GL_NUM_EXTENSIONS, &n);
        for (size_t i = 0; i < n; ++i)
            printf ("  %s\n", glGetStringi (GL_EXTENSIONS, i));
#endif

#if RIVE_DESKTOP_GL && DEBUG
        if (GLAD_GL_KHR_debug)
        {
            glEnable (GL_DEBUG_OUTPUT);
            glDebugMessageControlKHR (GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageCallbackKHR (&err_msg_callback, nullptr);
        }
#endif
    }

    float dpiScale (void*) const override
    {
#if RIVE_DESKTOP_GL && __APPLE__
        return 2;
#else
        return 1;
#endif
    }

    rive::Factory* factory() override { return m_plsContext.get(); }

    rive::gpu::RenderContext* renderContext() override { return m_plsContext.get(); }

    rive::gpu::RenderTarget* renderTarget() override { return m_renderTarget.get(); }

    void onSizeChanged (void* window, int width, int height, uint32_t sampleCount) override
    {
        m_renderTarget = rive::make_rcp<rive::gpu::FramebufferRenderTargetGL> (width, height, 0, sampleCount);
    }

    std::unique_ptr<rive::Renderer> makeRenderer (int width, int height) override
    {
        return std::make_unique<rive::RiveRenderer> (m_plsContext.get());
    }

    void begin (const rive::gpu::RenderContext::FrameDescriptor& frameDescriptor) override
    {
        m_plsContext->static_impl_cast<rive::gpu::RenderContextGLImpl>()->invalidateGLState();
        m_plsContext->beginFrame (frameDescriptor);
    }

    void end (void*) override
    {
        m_plsContext->flush ({ m_renderTarget.get() });

        m_plsContext->static_impl_cast<rive::gpu::RenderContextGLImpl>()->unbindGLInternalResources();
    }

private:
    std::unique_ptr<rive::gpu::RenderContext> m_plsContext;
    rive::rcp<rive::gpu::RenderTargetGL> m_renderTarget;
};

std::unique_ptr<GraphicsContext> yup_constructOpenGLGraphicsContext (GraphicsContext::Options options)
{
    return std::make_unique<LowLevelRenderContextGL> (options);
}

} // namespace yup
#endif
