#include <Model.h>
#include <RootTask.h>

#include <filedevice/rio_FileDeviceMgr.h>
#include <rio.h>
#include <controller/rio_ControllerMgr.h>
#include <controller/win/rio_WinControllerWin.h>
#include <gfx/rio_Projection.h>
#include <gfx/rio_Window.h>
#include <string>
#include <stdio.h>

#if RIO_IS_CAFE
#include <controller/rio_ControllerMgr.h>
#include <controller/cafe/rio_CafeVPadDeviceCafe.h>
#include <controller/cafe/rio_CafeWPadDeviceCafe.h>
#include <helpers/CafeControllerInput.h>
#include <imgui_impl_gx2.h>
#include <imgui_impl_wiiu.h>
#else
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif // RIO_IS_CAFE
#include <format>
#include <helpers/ui/ThemeMgr.h>

RootTask::RootTask()
    : ITask("FFL Testing"), mInitialized(false)
{
}

void RootTask::prepare_()
{
    // Init imgui
    initImgui();
    mInitialized = false;

    FFLInitDesc init_desc;
    init_desc.fontRegion = FFL_FONT_REGION_0;
    init_desc._c = false;
    init_desc._10 = true;

#if RIO_IS_CAFE
    FSInit();
#endif // RIO_IS_CAFE

    {
        std::string resPath;
        resPath.resize(256);
        // Middle
        {
            FFLGetResourcePath(resPath.data(), 256, FFL_RESOURCE_TYPE_MIDDLE, false);
            {
                rio::FileDevice::LoadArg arg;
                arg.path = resPath;
                arg.alignment = 0x2000;

                u8 *buffer = rio::FileDeviceMgr::instance()->getNativeFileDevice()->tryLoad(arg);
                if (buffer == nullptr)
                {
                    RIO_LOG("NativeFileDevice failed to load: %s\n", resPath.c_str());
                    RIO_ASSERT(false);
                    return;
                }

                mResourceDesc.pData[FFL_RESOURCE_TYPE_MIDDLE] = buffer;
                mResourceDesc.size[FFL_RESOURCE_TYPE_MIDDLE] = arg.read_size;
            }
        }
        // High
        {
            FFLGetResourcePath(resPath.data(), 256, FFL_RESOURCE_TYPE_HIGH, false);
            {
                rio::FileDevice::LoadArg arg;
                arg.path = resPath;
                arg.alignment = 0x2000;

                u8 *buffer = rio::FileDeviceMgr::instance()->getNativeFileDevice()->tryLoad(arg);
                if (buffer == nullptr)
                {
                    RIO_LOG("NativeFileDevice failed to load: %s\n", resPath.c_str());
                    RIO_ASSERT(false);
                    return;
                }

                mResourceDesc.pData[FFL_RESOURCE_TYPE_HIGH] = buffer;
                mResourceDesc.size[FFL_RESOURCE_TYPE_HIGH] = arg.read_size;
            }
        }
    }

    FFLResult result = FFLInitResEx(&init_desc, &mResourceDesc);
    if (result != FFL_RESULT_OK)
    {
        RIO_LOG("FFLInitResEx() failed with result: %d\n", (s32)result);
        RIO_ASSERT(false);
        return;
    }

    FFLiEnableSpecialMii(333326543);

    RIO_ASSERT(FFLIsAvailable());

    FFLInitResGPUStep();

    mShader.initialize();

    // Initializing random mii DB.
    {
        miiBufferSize = new u8[FFLGetMiddleDBBufferSize(100)];
        RIO_LOG("Created mii buffer size \n");
        FFLInitMiddleDB(&randomMiddleDB, FFL_MIDDLE_DB_TYPE_RANDOM_PARAM, miiBufferSize, 100);
        FFLUpdateMiddleDB(&randomMiddleDB);
        RIO_LOG("Init'd middle DB \n");
    }

    createModel_(0);

    // Set projection matrix
    updateProjectionMatrix();

    isOpen = true;
    mInitialized = true;
}

void RootTask::createModel_(u16 index)
{
    Model::InitArgMiddleDB arg = {
        .desc = {
            .resolution = FFLResolution(2048),
            .expressionFlag = 1,
            .modelFlag = 1 << 0 | 1 << 1 | 1 << 2,
            .resourceType = FFL_RESOURCE_TYPE_HIGH,
        },
        .data = &randomMiddleDB,
        .index = index};

    mpModel = new Model();
    mpModel->initialize(arg, mShader);
    mpModel->setScale({1 / 16.f, 1 / 16.f, 1 / 16.f});

    mCounter = 0.0f;
}

void RootTask::calc_()
{
    if (!mInitialized)
        return;

    rio::Window::instance()->clearColor(0.2f, 0.3f, 0.3f, 1.0f);
    rio::Window::instance()->clearDepthStencil();

    static const rio::Vector3f CENTER_POS = {0.0f, 2.0f, -0.25f};

    mCamera.at() = CENTER_POS;

    // Move camera
    mCamera.pos().set(
        CENTER_POS.x + std::sin(mCounter) * 10,
        CENTER_POS.y,
        CENTER_POS.z + std::cos(mCounter) * 10);
    mCounter += 1.f / 60;

    // Get view matrix
    rio::BaseMtx34f view_mtx;
    mCamera.getMatrix(&view_mtx);

    // mpModel->enableSpecialDraw();

    mpModel->drawOpa(view_mtx, mProjMtx);
    mpModel->drawXlu(view_mtx, mProjMtx);

    // Rendering GUI
    {
        ImGuiIO &io = *p_io;

#if RIO_IS_CAFE
        ImGui_ImplWiiU_ControllerInput input;

        getCafeVPADDevice(&input);
        getCafeKPADDevice(&input);
        ImGui_ImplWiiU_ProcessInput(&input);

        ImGui_ImplGX2_NewFrame();
#else
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
#endif // RIO_IS_CAFE
        ImGui::NewFrame();
        {
            ImGui::Begin("Mii Utils Panel");
            if (ImGui::Button("Select new random mii"))
            {
                delete mpModel;
                createModel_((rand() % 100));
            }

            ImGui::BeginTable("Random Miis", 2);
            ImVec2 buttonSize = {100, 100};
            for (int i = 0; i < FFLGetMiddleDBStoredSize(&randomMiddleDB); i++)
            {
                std::string formattedButtonID = "Mii " + std::to_string(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Button(formattedButtonID.c_str(), buttonSize))
                {
                    int index = i;
                    delete mpModel;
                    createModel_((u16)(index));
                }
                ImGui::TableNextColumn();
                ImGui::Text(formattedButtonID.c_str());
            }
            ImGui::EndTable();

            ImGui::End();
        }
        ImGui::Render();

#if RIO_IS_CAFE
        ImGui_ImplGX2_RenderDrawData(ImGui::GetDrawData());
#else
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif // RIO_IS_CAFE

#if RIO_IS_CAFE
        // Render keyboard overlay
        rio::Graphics::setViewport(0, 0, io.DisplaySize.x, io.DisplaySize.y, 0.0f, 1.0f);
        rio::Graphics::setScissor(0, 0, io.DisplaySize.x, io.DisplaySize.y);
        ImGui_ImplWiiU_DrawKeyboardOverlay();
#else
        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
#endif // RIO_IS_CAFE
    }
}

void RootTask::exit_()
{
    if (!mInitialized)
        return;

#if RIO_IS_CAFE
    ImGui_ImplGX2_Shutdown();
    ImGui_ImplWiiU_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
#endif // RIO_IS_CAFE
    ImGui::DestroyContext();
    ThemeMgr::destroySingleton();

    delete mpModel; // FFLCharModel destruction must happen before FFLExit
    delete miiBufferSize;
    delete &randomMiddleDB;
    mpModel = nullptr;

    FFLExit();

    rio::MemUtil::free(mResourceDesc.pData[FFL_RESOURCE_TYPE_HIGH]);
    rio::MemUtil::free(mResourceDesc.pData[FFL_RESOURCE_TYPE_MIDDLE]);

    mInitialized = false;
}

void RootTask::initImgui()
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    RootTask::p_io = &io;
#if RIO_IS_CAFE
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
#else
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
#endif                                                // RIO_IS_CAFE
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.Fonts->AddFontFromFileTTF("comic_sans.ttf", 20.f);
    // io.ConfigViewportsNoAutoMerge = true;
    // io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

#if !RIO_IS_CAFE
    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle &style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
#endif

    // Setup platform and renderer backends
#if RIO_IS_CAFE
    // Scale everything by 1.5 for the Wii U
    ImGui::GetStyle().ScaleAllSizes(1.5f);
    io.FontGlobalScale = 1.5f;

    ImGui_ImplWiiU_Init();
    ImGui_ImplGX2_Init();
#else
    ImGui_ImplGlfw_InitForOpenGL(rio::Window::instance()->getNativeWindow().getGLFWwindow(), true);
    ImGui_ImplOpenGL3_Init("#version 130");
#endif // RIO_IS_CAFE

    ThemeMgr::createSingleton();
    ThemeMgr::instance()->applyTheme(ThemeMgr::sDefaultTheme);

    // Setup display sizes and scales
    io.DisplaySize.x = (float)rio::Window::instance()->getWidth();  // set the current display width
    io.DisplaySize.y = (float)rio::Window::instance()->getHeight(); // set the current display height here

#if RIO_IS_WIN
    rio::Window::instance()->setOnResizeCallback(&RootTask::onResizeCallback_);
#endif // RIO_IS_WIN
}

void RootTask::updateProjectionMatrix()
{
    // Get window instance
    const rio::Window *const window = rio::Window::instance();

    // Create perspective projection instance
    rio::PerspectiveProjection proj(
        0.1f,
        100.0f,
        rio::Mathf::deg2rad(45),
        f32(window->getWidth()) / f32(window->getHeight()));

    // Calculate matrix
    mProjMtx = proj.getMatrix();
}

#if RIO_IS_WIN

void RootTask::resize_(s32 width, s32 height)
{
    ImGuiIO &io = *p_io;

    // Setup display sizes and scales
    io.DisplaySize.x = (float)width;  // set the current display width
    io.DisplaySize.y = (float)height; // set the current display height here

    updateProjectionMatrix();
}

void RootTask::onResizeCallback_(s32 width, s32 height)
{
    static_cast<RootTask *>(rio::sRootTask)->resize_(width, height);
}

#endif // RIO_IS_WIN
