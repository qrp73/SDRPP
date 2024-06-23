/*
 * This file is part of the SDRPP distribution (https://github.com/qrp73/SDRPP).
 * Copyright (c) 2024 qrp73.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <backend.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_mouse.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include <utils/flog.h>
#include <utils/opengl_include_code.h>
#include <version.h>
#include <core.h>
#include <filesystem>
#include <stb_image.h>
#include <stb_image_resize.h>
#include <gui/gui.h>

namespace backend {
    bool maximized = false;
    bool fullScreen = false;
    int winHeight = 0;
    int winWidth = 0;
    bool _maximized = maximized;
    int _winWidth, _winHeight;
    SDL_Window* window;
    SDL_GLContext gl_context;
    bool isWindowShouldClose = false;

    int init(std::string resDir) {
        // Load config
        core::configManager.acquire();
        winWidth = core::configManager.conf["windowSize"]["w"];
        winHeight = core::configManager.conf["windowSize"]["h"];
        maximized = core::configManager.conf["maximized"];
        fullScreen = core::configManager.conf["fullscreen"];
        core::configManager.release();

        // Setup SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
        {
            flog::error("SDL_Init() failed: {}", SDL_GetError());
            return -1;
        }

        // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
        // GL ES 2.0 + GLSL 100
        const char* glsl_version = "#version 100";
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
        // GL 3.2 Core + GLSL 150
        const char* glsl_version = "#version 150";
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
        // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
        // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
        SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

        // Create window with graphics context
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        //SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        //SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
        SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (maximized) {
            window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_MAXIMIZED);
            _maximized = true;
            //winWidth = 1280;
            //winHeight = 720;
        }
        window = SDL_CreateWindow("SDRPP v" VERSION_STR " (Built at " __TIME__ ", " __DATE__ ")", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winWidth, winHeight, window_flags);
        if (window == nullptr)
        {
            flog::error("SDL_CreateWindow() failed: {}", SDL_GetError());
            return -1;
        }
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr)
        {
            flog::error("SDL_GL_CreateContext() failed: {}", SDL_GetError());
            return -1;
        }
        SDL_GL_MakeCurrent(window, gl_context);
        SDL_GL_SetSwapInterval(1); // Enable vsync

        flog::info("OpenGL: {}", (const char*)(glGetString(GL_VERSION)));
        flog::info("GLSL:   {}", (const char*)(glGetString(GL_SHADING_LANGUAGE_VERSION)));
        GLint samples = 0;
        glGetIntegerv(GL_SAMPLES, &samples);
        flog::info("GL_SAMPLES: {}", samples);

        //if (maximized) {
        //    SDL_MaximizeWindow(window);
        //    _maximized = true;
        //}

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.IniFilename = nullptr;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();

        // Setup Platform/Renderer backends
        if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context))
        {
            flog::warn("ImGui_ImplSDL2_InitForOpenGL() failed");
        }
        if (!ImGui_ImplOpenGL3_Init(glsl_version))
        {
            flog::warn("ImGui_ImplOpenGL3_Init() failed");
        }

        SDL_GetWindowSize(window, &_winWidth, &_winHeight);
        return 0;
    }

    void pollEvents();

    void beginFrame() {
        pollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void render(bool vsync) {
        // Rendering
        ImGui::Render();

        int display_w, display_h;
        SDL_GetWindowSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        glClearColor(gui::themeManager.clearColor.x, gui::themeManager.clearColor.y, gui::themeManager.clearColor.z, gui::themeManager.clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SetSwapInterval(vsync);
        SDL_GL_SwapWindow(window);
    }

    void getMouseScreenPos(double& x, double& y) {
        int ix, iy;
        SDL_GetMouseState(&ix, &iy);
        x = ix;
        y = iy;
    }

    void setMouseScreenPos(double x, double y) {
        // WTF?
        // Tell GLFW to move the cursor and then manually fire the event
        //glfwSetCursorPos(window, x, y);
        //ImGui_ImplGlfw_CursorPosCallback(window, x, y);
        //SDL_WarpMouseInWindow(window, (int)x, (int)y);
        //ImGuiIO& io = ImGui::GetIO();
        //io.AddMousePosEvent((float)x, (float)y);
    }

    void pollEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    isWindowShouldClose = true;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                        isWindowShouldClose = true;
                    }
                    break;
                case SDL_KEYDOWN:
                    if ((event.key.keysym.mod & KMOD_ALT) && event.key.keysym.sym == SDLK_F4) {
                        isWindowShouldClose = true;
                    }
                    break;
            }
        }
    }


    int renderLoop() {
        // Main loop
        while (!isWindowShouldClose) {
            //pollEvents();

            beginFrame();
            
            maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
            if (_maximized != maximized) {
                _maximized = maximized;
                core::configManager.acquire();
                core::configManager.conf["maximized"] = _maximized;
                core::configManager.release(true);
            }

            //ImGuiIO& io = ImGui::GetIO();
            //_winWidth = (int)io.DisplaySize.x;
            //_winHeight = (int)io.DisplaySize.y;
            SDL_GetWindowSize(window, &_winWidth, &_winHeight);

            if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
                fullScreen = !fullScreen;
                if (fullScreen) {
                    flog::info("Fullscreen: ON");
                    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) == 0) {
                        core::configManager.acquire();
                        core::configManager.conf["fullscreen"] = true;
                        core::configManager.release();
                    }
                }
                else {
                    flog::info("Fullscreen: OFF");
                    if (SDL_SetWindowFullscreen(window, 0) != 0) {
                        core::configManager.acquire();
                        core::configManager.conf["fullscreen"] = false;
                        core::configManager.release();
                    }
                }
            }

            if ((_winWidth != winWidth || _winHeight != winHeight) && !maximized && _winWidth > 0 && _winHeight > 0) {
                winWidth = _winWidth;
                winHeight = _winHeight;
                core::configManager.acquire();
                core::configManager.conf["windowSize"]["w"] = winWidth;
                core::configManager.conf["windowSize"]["h"] = winHeight;
                core::configManager.release(true);
            }

            if (_winWidth > 0 && _winHeight > 0) {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(_winWidth, _winHeight));
                gui::mainWindow.draw();
            }

            render();
        }

        return 0;
    }

    int end() {
        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();

        return 0; // TODO: Int really needed?
    }
}
