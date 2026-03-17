#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 先把大量星点渲染到离屏纹理；
// 再用全屏四边形做后处理，把程序化星云叠加到星空之上。
constexpr int kInitialWidth = 1600;
constexpr int kInitialHeight = 900;
constexpr int kMaxStars = 60000;

struct OpenGLConfig {
    int major;
    int minor;
    const char* glslVersion;
};

constexpr OpenGLConfig kOpenGLConfig =
#ifdef __APPLE__
    OpenGLConfig{4, 1, "#version 410 core"};
#else
    OpenGLConfig{3, 3, "#version 330 core"};
#endif
;

struct Camera {
    // 这里使用欧拉角定义观察方向，便于通过 yaw / pitch 直接推导前向向量。
    glm::vec3 cameraPos {0.0f, 5.0f, 40.0f};
    float yaw = -90.0f;
    float pitch = -8.0f;
    float fov = 60.0f;

    glm::vec3 front() const {
        // 把球面角转换成单位方向向量，供 lookAt 和 shader 使用。
        glm::vec3 dir;
        dir.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        dir.y = std::sin(glm::radians(pitch));
        dir.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        return glm::normalize(dir);
    }

    glm::mat4 viewMatrix() const {
        // 视图矩阵只描述“相机怎么看世界”，不包含投影参数。
        return glm::lookAt(cameraPos, cameraPos + front(), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

struct RenderSettings {
    // 这一组参数几乎都直接映射到 ImGui 控件，便于实时观察视觉变化。
    int starCount = 32000;
    float pointSize = 7.0f;
    float spaceScale = 0.4f;

    float starSpeed  = 0.019f;

    float nebulaScale = 2.2f;
    float nebulaDensity = 1.25f;
    float nebulaSpeed = 0.1f;
    float nebulaContrast = 1.35f;
    float nebulaBrightness = 1.25f;
    float starBoost = 2.0f;

    glm::vec3 nebulaColorA {0.05f, 0.01f, 0.04f};
    glm::vec3 nebulaColorB {0.86f, 0.12f, 0.25f};
    glm::vec3 nebulaColorC {0.95f, 0.34f, 0.60f};
};

struct AppState {
    // 这里保存窗口级别的状态，而不是渲染参数。
    int width = kInitialWidth;
    int height = kInitialHeight;
    bool framebufferResized = false;

    enum class ControlMode {
        SceneOnly,
        ImGuiUI
    };

    ControlMode mode = ControlMode::ImGuiUI;
    bool modeTogglePressed = false;
};

struct Framebuffer {
    // fbo 负责组织附件；colorTex 作为后处理输入；depthRbo 用于星点遮挡测试。
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthRbo = 0;
};

Camera gCamera;
AppState gApp;
void configureOpenGLWindowHints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kOpenGLConfig.major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kOpenGLConfig.minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
}

std::string prepareShaderSource(const char* source) {
    std::string prepared = source ? source : "";

    const auto firstNonWhitespace = prepared.find_first_not_of(" \t\r\n");
    if (firstNonWhitespace == std::string::npos) {
        return std::string(kOpenGLConfig.glslVersion) + "\n";
    }

    prepared.erase(0, firstNonWhitespace);

    if (prepared.rfind("#version", 0) == 0) {
        const auto lineEnd = prepared.find('\n');
        if (lineEnd == std::string::npos) {
            prepared.clear();
        } else {
            prepared.erase(0, lineEnd + 1);
        }
    }

    return std::string(kOpenGLConfig.glslVersion) + "\n" + prepared;
}

void logOpenGLContextInfo() {
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);

    std::cout << "OpenGL version: "
              << (version ? reinterpret_cast<const char*>(version) : "unknown")
              << std::endl;
    std::cout << "GLSL version: "
              << (glslVersion ? reinterpret_cast<const char*>(glslVersion) : "unknown")
              << std::endl;
    std::cout << "Renderer: "
              << (renderer ? reinterpret_cast<const char*>(renderer) : "unknown")
              << std::endl;
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    // GLFW 在最小化窗口时给出 0，这里至少钳制到 1，避免后续除零。
    gApp.width = std::max(1, width);
    gApp.height = std::max(1, height);
    gApp.framebufferResized = true;
}

GLuint compileShader(GLenum type, const char* src) {
    const std::string preparedSource = prepareShaderSource(src);
    const char* sourcePtr = preparedSource.c_str();

    // 单独封装编译步骤，方便统一处理 GLSL 报错日志。
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint createProgram(const char* vsSrc, const char* fsSrc) {
    // Program 是顶点着色器和片元着色器链接后的可执行 GPU 管线对象。
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        std::cerr << "Program link error:\n" << log << std::endl;
        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

bool readTextFile(const std::filesystem::path& filePath, std::string& outText) {
    // shader 文件很小，直接整文件读入字符串最简单。
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    outText = buffer.str();
    return true;
}

std::vector<std::filesystem::path> shaderSearchPaths(const std::filesystem::path& exeDir,
                                                     const std::string& relativePath) {
    // 支持从源码目录运行，也支持从 build 目录运行，减少路径问题。
    return {
        std::filesystem::current_path() / relativePath,
        std::filesystem::current_path() / ".." / relativePath,
        exeDir / relativePath,
        exeDir / ".." / relativePath
    };
}

std::filesystem::path findFirstExistingPath(const std::vector<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            return path;
        }
    }
    return {};
}

GLuint createProgramFromFiles(const std::filesystem::path& exeDir,
                              const std::string& vertexRelativePath,
                              const std::string& fragmentRelativePath) {
    const auto vertexCandidates = shaderSearchPaths(exeDir, vertexRelativePath);
    const auto fragmentCandidates = shaderSearchPaths(exeDir, fragmentRelativePath);
    const std::filesystem::path vertexPath = findFirstExistingPath(vertexCandidates);
    const std::filesystem::path fragmentPath = findFirstExistingPath(fragmentCandidates);

    if (vertexPath.empty() || fragmentPath.empty()) {
        // 路径查找失败时把所有候选路径都打印出来，调试最直接。
        std::cerr << "Shader file not found." << std::endl;
        std::cerr << "Vertex shader candidates:" << std::endl;
        for (const auto& p : vertexCandidates) {
            std::cerr << "  - " << p << std::endl;
        }
        std::cerr << "Fragment shader candidates:" << std::endl;
        for (const auto& p : fragmentCandidates) {
            std::cerr << "  - " << p << std::endl;
        }
        return 0;
    }

    std::string vsSource;
    std::string fsSource;
    if (!readTextFile(vertexPath, vsSource) || !readTextFile(fragmentPath, fsSource)) {
        std::cerr << "Failed to read shader files: " << vertexPath << " / " << fragmentPath << std::endl;
        return 0;
    }

    return createProgram(vsSource.c_str(), fsSource.c_str());
}

bool createSceneFramebuffer(Framebuffer& fb, int width, int height) {
    if (fb.fbo) {
        // 重建前先释放旧附件；窗口缩放时会走到这里。
        glDeleteFramebuffers(1, &fb.fbo);
        glDeleteTextures(1, &fb.colorTex);
        glDeleteRenderbuffers(1, &fb.depthRbo);
        fb = {};
    }

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    glGenTextures(1, &fb.colorTex);
    glBindTexture(GL_TEXTURE_2D, fb.colorTex);
    // 颜色附件用普通 8-bit RGBA 纹理即可，已经足够承载当前的发光星点结果。
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.colorTex, 0);

    glGenRenderbuffers(1, &fb.depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.depthRbo);
    // 深度模板附件不需要被采样，因此用 renderbuffer 比纹理更直接。
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.depthRbo);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        std::cerr << "Framebuffer is incomplete." << std::endl;
    }
    return complete;
}

std::vector<glm::vec4> generateStars(int count) {
    // 每颗星用 vec4 表示：xyz 是位置，w 存亮度。
    std::vector<glm::vec4> stars;
    stars.reserve(static_cast<size_t>(count));

    // 固定随机种子意味着每次启动都看到同一片星空，便于调试和截图对比。
    std::mt19937 rng(20260226);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> axis(-1.0f, 1.0f);

    // 星星被放在一个厚球壳里，而不是集中在相机附近。
    constexpr float minR = 700.0f;
    constexpr float maxR = 6000.0f;
    const float minR3 = minR * minR * minR;
    const float maxR3 = maxR * maxR * maxR;

    while (static_cast<int>(stars.size()) < count) {
        glm::vec3 dir(axis(rng), axis(rng), axis(rng));
        const float len2 = glm::dot(dir, dir);
        if (len2 < 1e-4f || len2 > 1.0f) {
            // 只接受单位球内的随机点，随后再归一化成方向向量。
            continue;
        }
        dir = glm::normalize(dir);

        const float t = unit(rng);
        // 使用立方根把半径分布改成“体积均匀”，避免星星都挤在外层球壳。
        const float radius = std::cbrt(minR3 + t * (maxR3 - minR3));
        // 亮度故意偏向高值，这样少量亮星能拉开层次。
        const float brightness = 0.35f + 0.65f * std::pow(unit(rng), 0.2f);

        stars.emplace_back(dir * radius, brightness);
    }

    return stars;
}

void setControlMode(AppState::ControlMode mode) {
    // 目前模式切换很简单，但仍单独封装，便于将来集中处理鼠标状态。
    gApp.mode = mode;
}

void toggleControlMode() {
    const AppState::ControlMode nextMode = (gApp.mode == AppState::ControlMode::SceneOnly)
        ? AppState::ControlMode::ImGuiUI
        : AppState::ControlMode::SceneOnly;
    setControlMode(nextMode);
}

void processInput(GLFWwindow* window) {
    // 保留 ESC 关闭窗口；项目里的自定义交互键现在只剩 Z。
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    const bool zDown = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    // 只在按下瞬间切一次，避免长按 Z 时在两种模式之间来回抖动。
    if (zDown && !gApp.modeTogglePressed) {
        toggleControlMode();
    }
    gApp.modeTogglePressed = zDown;
}

}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW." << std::endl;
        return EXIT_FAILURE;
    }

    configureOpenGLWindowHints();

    GLFWwindow* window = glfwCreateWindow(kInitialWidth, kInitialHeight, "Nebula Shader Scene", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    // 开启垂直同步，避免这个 demo 以极高帧率空转。
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    logOpenGLContextInfo();
    int fbWidth = 0;
    int fbHeight = 0;
    // framebuffer 尺寸才是真正的像素尺寸；在 Retina 屏上可能与窗口逻辑尺寸不同。
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    gApp.width = std::max(1, fbWidth);
    gApp.height = std::max(1, fbHeight);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // UI 使用 ImGui 的默认深色主题，和星空背景更协调。
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(kOpenGLConfig.glslVersion);

    setControlMode(AppState::ControlMode::ImGuiUI);

    std::filesystem::path exeDir = std::filesystem::current_path();
    if (argc > 0 && argv && argv[0]) {
        std::error_code ec;
        const auto exePath = std::filesystem::absolute(argv[0], ec);
        if (!ec) {
            // shader 查找优先参考可执行文件所在目录，兼顾从 IDE 和命令行启动两种情况。
            exeDir = exePath.parent_path();
        }
    }

    // 星点和星云分别使用两套 shader：前者负责几何点精灵，后者负责屏幕后处理。
    const GLuint starProgram = createProgramFromFiles(exeDir, "shaders/star.vert", "shaders/star.frag");
    const GLuint postProgram = createProgramFromFiles(exeDir, "shaders/screen_quad.vert", "shaders/nebula.frag");
    if (!starProgram || !postProgram) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    const std::vector<glm::vec4> stars = generateStars(kMaxStars);

    GLuint starVAO = 0;
    GLuint starVBO = 0;
    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(stars.size() * sizeof(glm::vec4)),
                 stars.data(),
                 GL_STATIC_DRAW);
    // 顶点属性 0 直接把一个 vec4 喂给 shader 中的 aStar。
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    constexpr float quadVertices[] = {
        // 两个三角形拼成全屏矩形，供后处理 shader 覆盖整个屏幕。
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    Framebuffer sceneFB;
    if (!createSceneFramebuffer(sceneFB, gApp.width, gApp.height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    RenderSettings settings;

    while (!glfwWindowShouldClose(window)) {
        // 时间统一从 GLFW 获取，shader 动画和 CPU 侧状态用同一时钟。
        const float timeNow = static_cast<float>(glfwGetTime());

        glfwPollEvents();
        processInput(window);

        // 窗口尺寸变化后，离屏缓冲也要按新尺寸重建。
        if (gApp.framebufferResized) {
            createSceneFramebuffer(sceneFB, gApp.width, gApp.height);
            gApp.framebufferResized = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (gApp.mode == AppState::ControlMode::SceneOnly) {
            // 场景模式下不显示控制面板，让画面完整铺满窗口。
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        if (gApp.mode == AppState::ControlMode::ImGuiUI) {
            // 这里的每个控件基本都对应一个 shader uniform，调参时会立刻反映到下一帧。
            ImGui::Begin("Nebula Controls");
            ImGui::Text("Z: Toggle Controls");
            ImGui::Text("Current Mode: Controls");

            int starCount = settings.starCount;
            if (ImGui::SliderInt("Star Count", &starCount, 4000, kMaxStars)) {
                settings.starCount = starCount;
            }
            ImGui::SliderFloat("Star Size",  &settings.pointSize, 1.0f, 16.0f);
            ImGui::SliderFloat("Star Speed", &settings.starSpeed,  0.0f, 0.3f);

            ImGui::Separator();
            ImGui::SliderFloat("Nebula Density", &settings.nebulaDensity, 0.1f, 2.4f);
            ImGui::SliderFloat("Nebula Speed", &settings.nebulaSpeed, 0.0f, 0.25f);
            ImGui::SliderFloat("Nebula Contrast", &settings.nebulaContrast, 0.5f, 2.8f);
            ImGui::SliderFloat("Nebula Brightness", &settings.nebulaBrightness, 0.2f, 2.8f);
            ImGui::ColorEdit3("Color A", glm::value_ptr(settings.nebulaColorA));
            ImGui::ColorEdit3("Color B", glm::value_ptr(settings.nebulaColorB));
            ImGui::ColorEdit3("Color C", glm::value_ptr(settings.nebulaColorC));

            ImGui::Separator();
            ImGui::SliderFloat("FOV", &gCamera.fov, 25.0f, 95.0f);

            ImGui::End();
        } else {

            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("##hint", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
            ImGui::Text("Z - Open Controls");
            ImGui::End();
        }

        // 第一遍先把星点渲染到离屏 framebuffer。
        const glm::mat4 view = gCamera.viewMatrix();
        const glm::mat4 proj = glm::perspective(
            glm::radians(gCamera.fov),
            static_cast<float>(gApp.width) / static_cast<float>(gApp.height),
            0.1f,
            22000.0f
        );

        glBindFramebuffer(GL_FRAMEBUFFER, sceneFB.fbo);
        glViewport(0, 0, gApp.width, gApp.height);
        glEnable(GL_DEPTH_TEST);
        // 点大小由顶点着色器写入 gl_PointSize，因此必须启用这个状态。
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        // 加色混合会把亮点叠加得更像星光，而不是普通半透明覆盖。
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glClearColor(0.003f, 0.004f, 0.010f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(starProgram);
        // 这批 uniform 描述“星空几何该如何投影到相机前方”。
        glUniformMatrix4fv(glGetUniformLocation(starProgram, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(starProgram, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform1f(glGetUniformLocation(starProgram, "uPointSize"),  settings.pointSize);
        glUniform1f(glGetUniformLocation(starProgram, "uSpaceScale"),  settings.spaceScale);
        glUniform1f(glGetUniformLocation(starProgram, "uTime"),        timeNow);
        glUniform1f(glGetUniformLocation(starProgram, "uStarSpeed"),   settings.starSpeed);
        glBindVertexArray(starVAO);
        glDrawArrays(GL_POINTS, 0, settings.starCount);
        glBindVertexArray(0);

        // 第二遍再采样第一遍的结果，叠加星云后处理效果。
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, gApp.width, gApp.height);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(postProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneFB.colorTex);
        // 第二遍 shader 不再关心点数据，只关心第一遍输出的整张颜色纹理。
        glUniform1i(glGetUniformLocation(postProgram, "uScene"), 0);
        glUniform2f(glGetUniformLocation(postProgram, "uResolution"), static_cast<float>(gApp.width), static_cast<float>(gApp.height));
        glUniform1f(glGetUniformLocation(postProgram, "uTime"), timeNow);
        glUniform1f(glGetUniformLocation(postProgram, "uNebulaScale"), settings.nebulaScale);
        glUniform1f(glGetUniformLocation(postProgram, "uNebulaDensity"), settings.nebulaDensity);
        glUniform1f(glGetUniformLocation(postProgram, "uNebulaSpeed"), settings.nebulaSpeed);
        glUniform1f(glGetUniformLocation(postProgram, "uNebulaContrast"), settings.nebulaContrast);
        glUniform1f(glGetUniformLocation(postProgram, "uNebulaBrightness"), settings.nebulaBrightness);
        glUniform1f(glGetUniformLocation(postProgram, "uStarBoost"), settings.starBoost);
        glUniform3fv(glGetUniformLocation(postProgram, "uNebulaColorA"), 1, glm::value_ptr(settings.nebulaColorA));
        glUniform3fv(glGetUniformLocation(postProgram, "uNebulaColorB"), 1, glm::value_ptr(settings.nebulaColorB));
        glUniform3fv(glGetUniformLocation(postProgram, "uNebulaColorC"), 1, glm::value_ptr(settings.nebulaColorC));

        const glm::vec3 camFront = gCamera.front();
        const glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camFront));
        // 把相机基向量传给片元着色器，让它能从屏幕像素反推出一条世界空间射线。
        glUniform3fv(glGetUniformLocation(postProgram, "uCamPos"), 1, glm::value_ptr(gCamera.cameraPos));
        glUniform3fv(glGetUniformLocation(postProgram, "uCamFront"), 1, glm::value_ptr(camFront));
        glUniform3fv(glGetUniformLocation(postProgram, "uCamRight"), 1, glm::value_ptr(camRight));
        glUniform3fv(glGetUniformLocation(postProgram, "uCamUp"), 1, glm::value_ptr(camUp));
        glUniform1f(glGetUniformLocation(postProgram, "uAspect"),
                    static_cast<float>(gApp.width) / static_cast<float>(gApp.height));
        glUniform1f(glGetUniformLocation(postProgram, "uTanHalfFov"),
                    std::tan(glm::radians(gCamera.fov) * 0.5f));

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        ImGui::Render();
        // ImGui 始终在场景之后绘制，相当于一层最终叠加的 HUD。
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    glDeleteFramebuffers(1, &sceneFB.fbo);
    glDeleteTextures(1, &sceneFB.colorTex);
    glDeleteRenderbuffers(1, &sceneFB.depthRbo);

    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &starVBO);
    glDeleteVertexArrays(1, &starVAO);
    glDeleteProgram(starProgram);
    glDeleteProgram(postProgram);

    // 按初始化的逆序释放资源，避免依赖对象提前失效。
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
