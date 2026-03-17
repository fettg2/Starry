#include <glad/glad.h>// 加载OpenGL函数指针
#include <GLFW/glfw3.h>// 窗口库

#include <glm/glm.hpp>// 数学库
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

#include "Scene.h"
#include "Octree.h"

//顶点着色器
const char* vertexShaderSource = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out float lightIntensity;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vec3 worldPos = (model * vec4(aPos, 1.0)).xyz;
    vec3 normal = normalize(aPos);
    vec3 dirToCenter = normalize(-worldPos);
    lightIntensity = max(dot(normal, dirToCenter), 0.15);})glsl";
//片段着色器
const char* fragmentShaderSource = R"glsl(
#version 330 core
in float lightIntensity;
out vec4 FragColor;
uniform vec4 objectColor;
uniform bool isGrid; // Add this uniform
uniform bool GLOW;
void main() {
    if (isGrid) {
        // If it's the grid, use the original color without lighting
        FragColor = objectColor;
    } else if(GLOW){
        FragColor = vec4(objectColor.rgb * 100000, objectColor.a);
    }else {
        // If it's an object, apply the lighting effect
        float fade = smoothstep(0.0, 10.0, lightIntensity*10);
        FragColor = vec4(objectColor.rgb * fade, objectColor.a);
    }})glsl";

bool running = true;
bool pause = true;
float lastX = 400.0, lastY = 300.0;// 上一帧的鼠标位置
float deltaTime = 0.0;// 上一帧到当前帧的时间间隔
float lastFrame = 0.0;// 上一帧的时间戳

const double G = 6.6743e-11; // m^3 kg^-1 s^-2
const float c = 299792458.0;
float initMass = float(pow(10, 22));
float sizeRatio = 30000.0f;

struct Camera {
    // 这里使用欧拉角定义观察方向，便于通过 yaw / pitch 直接推导前向向量。
    glm::vec3 cameraPos {0.0f, 1000.0f, 5000.0f};
    float yaw = -90.0f;
    float pitch = -8.0f;
    float fov = 45.0f;

    glm::vec3 front() const {
        // 把球面角转换成单位方向向量，供 lookAt 和 shader 使用。
        glm::vec3 dir;
        dir.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        dir.y = std::sin(glm::radians(pitch));
        dir.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        return glm::normalize(dir);
    }

    glm::vec3 up() const {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::mat4 viewMatrix() const {
        // 视图矩阵只描述“相机怎么看世界”，不包含投影参数。
        return glm::lookAt(cameraPos, cameraPos + front(), up());
    }
};
Camera gCamera;

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

struct Framebuffer;

//函数声明合计
GLFWwindow* StartGLU();
GLuint CreateShaderProgram(const char* vertexSource, const char* fragmentSource);
void CreateVBOVAO(GLuint& VAO, GLuint& VBO, const float* vertices, size_t vertexCount);
void UpdateCam(GLuint shaderProgram);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
glm::vec3 sphericalToCartesian(float r, float theta, float phi);
void DrawGrid(GLuint shaderProgram, GLuint gridVAO, size_t vertexCount);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
std::vector<glm::vec4> generateStars(int);
bool readTextFile(const std::filesystem::path& filePath, std::string& outText);
std::vector<std::filesystem::path> shaderSearchPaths(const std::filesystem::path& exeDir, const std::string& relativePath);
std::filesystem::path findFirstExistingPath(const std::vector<std::filesystem::path>& paths);
GLuint createProgramFromFiles(const std::filesystem::path& exeDir, const std::string& vertexRelativePath, const std::string& fragmentRelativePath);
void configureOpenGLWindowHints();
std::string prepareShaderSource(const char* source);
void logOpenGLContextInfo();
bool createColorFramebuffer(Framebuffer& fb, int width, int height);

// 全局变量声明
GLuint shaderProgram;
GLint projectionLoc;
// 飞船模式全局变量
bool spaceshipMode = false;
glm::vec3 shipPos = glm::vec3(0.0f, 0.0f, 0.0f);
glm::vec3 shipVel = glm::vec3(0.0f, 0.0f, 0.0f);
const float shipMaxSpeed = 10000.0f; // 飞船最大速度上限
const float shipRadius = 100.0f;     // 飞船碰撞体积

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

AppState gApp;
GLFWwindow* gWindow = nullptr;

// 窗口大小变化回调函数
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    gApp.width = std::max(1, width);
    gApp.height = std::max(1, height);
    gApp.framebufferResized = true;

    glViewport(0, 0, gApp.width, gApp.height);
    float aspectRatio = static_cast<float>(gApp.width) / static_cast<float>(gApp.height);
    glm::mat4 projection = glm::perspective(glm::radians(gCamera.fov), aspectRatio, 0.1f, 750000.0f);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
}

std::vector<Object> objs = {};// 所有物体


//创建并实时修改网格
std::vector<float> CreateGridVertices(float size, int divisions, const std::vector<Object>& objs);
std::vector<float> UpdateGridVertices(std::vector<float> vertices, const std::vector<Object>& objs);

GLuint gridVAO, gridVBO;
Octree* octree = nullptr;

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

void setControlMode(AppState::ControlMode mode);

struct Framebuffer {
    // fbo 负责组织附件；colorTex 作为后处理输入；depthTex 供后处理判断前景遮挡。
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthTex = 0;
};

int main(int argc, char** argv) {
    GLFWwindow* window = StartGLU();//初始化GLFW和GLAD库，相当于init
    if (!window) {
        return EXIT_FAILURE;
    }
    gWindow = window;

    logOpenGLContextInfo();

    shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
    if (!shaderProgram) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    //渲染部分初始化
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");//位置
    GLint objectColorLoc = glGetUniformLocation(shaderProgram, "objectColor");
    glUseProgram(shaderProgram);
    
    //回调
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    
    glfwMakeContextCurrent(window);
    // 开启垂直同步，避免这个 demo 以极高帧率空转。
    glfwSwapInterval(1);

    //获取当前窗口大小
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    gApp.width = std::max(1, width);
    gApp.height = std::max(1, height);
    //计算宽高比
    float aspectRatio = static_cast<float>(gApp.width) / static_cast<float>(gApp.height);

    //创建投影矩阵，传给着色器
    glm::mat4 projection = glm::perspective(glm::radians(gCamera.fov), aspectRatio, 0.1f, 750000.0f);
    projectionLoc = glGetUniformLocation(shaderProgram, "projection");
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

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
    
    bool createSceneFramebuffer(Framebuffer& fb, int width, int height);

    Framebuffer sceneFB;
    Framebuffer starFB;
    if (!createSceneFramebuffer(sceneFB, gApp.width, gApp.height) ||
        !createColorFramebuffer(starFB, gApp.width, gApp.height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    RenderSettings settings;

    // 预分配100个天体的容量
    objs.reserve(100);
    
    // 天体创建（从外部文件初始化）
    InitCelestialBodies(objs);

    // 初始化飞船位置
    if (!objs.empty()) {
        // 出生在第一个天体（超新星）稍微靠上的位置
        shipPos = objs[0].position + glm::vec3(0.0f, objs[0].radius + 5000.0f, 0.0f);
        shipVel = objs[0].velocity;
    }
    
    // 初始化八叉树
    glm::vec3 minBound = glm::vec3(-20000, -20000, -20000);
    glm::vec3 maxBound = glm::vec3(20000, 20000, 20000);
    octree = new Octree(minBound, maxBound);
    
    std::vector<float> gridVertices = CreateGridVertices(40000.0f, 50, objs);//网格大小
    CreateVBOVAO(gridVAO, gridVBO, gridVertices.data(), gridVertices.size());
    
    // 主循环
    while (!glfwWindowShouldClose(window) && running == true) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        
        // 时间统一从 GLFW 获取，shader 动画和 CPU 侧状态用同一时钟。
        const float timeNow = currentFrame;

        glfwPollEvents();
        //processInput(window);

        // 窗口尺寸变化后，离屏缓冲也要按新尺寸重建。
        if (gApp.framebufferResized) {
            createSceneFramebuffer(sceneFB, gApp.width, gApp.height);
            createColorFramebuffer(starFB, gApp.width, gApp.height);
            gApp.framebufferResized = false;
        }

        // 设置ImGui的DeltaTime
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;
        
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

        // 飞船模式物理与控制
        if (spaceshipMode) {
            // 1. 引擎推进器
            float thrust = 200.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) shipVel += gCamera.front() * thrust;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) shipVel -= gCamera.front() * thrust;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) shipVel -= glm::normalize(glm::cross(gCamera.front(), gCamera.up())) * thrust;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) shipVel += glm::normalize(glm::cross(gCamera.front(), gCamera.up())) * thrust;
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) shipVel += gCamera.up() * thrust;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) shipVel -= gCamera.up() * thrust;

            //惯性阻尼器 / 刹车：按 X 键缓慢减速直至悬停
            if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
                shipVel *= (1.0f - 2.0f * deltaTime); // 每秒削减部分速度
                if (glm::length(shipVel) < 0.1f) shipVel = glm::vec3(0.0f);
            }

            // 限制最大速度
            float currentMaxSpeed = 8000.0f; 
            if (glm::length(shipVel) > currentMaxSpeed) {
                shipVel = glm::normalize(shipVel) * currentMaxSpeed;
            }

            // 2. 引力影响和位置更新 (非暂停状态下)
            if (!pause) {
                glm::vec3 totalForce(0.0f);
                for (const auto& obj : objs) {
                    if (obj.Initalizing) continue;
                    glm::vec3 dir = obj.position - shipPos;
                    float dist = glm::length(dir);
                    if (dist > 0.1f) {
                        // 万有引力计算
                        float gAccel = (G * obj.mass) / (dist * dist * 1000000.0f);
                        totalForce += glm::normalize(dir) * gAccel;
                    }
                }
                
                // 叠加引力产生的加速度
                shipVel += totalForce / 96.0f;
                // 更新位置
                shipPos += shipVel / 94.0f;

                // 3. 与天体的碰撞检测
                for (auto& obj : objs) {
                    if (obj.Initalizing) continue;
                    glm::vec3 dir = obj.position - shipPos;
                    float dist = glm::length(dir);
                    float minDist = obj.radius + shipRadius;
                    
                    if (dist < minDist) {
                        // 发生碰撞，计算法线
                        glm::vec3 normal = glm::normalize(dir);
                        
                        // 防止穿模，将飞船推出天体表面
                        float overlap = minDist - dist;
                        shipPos -= normal * overlap;
                        
                        // 弹性碰撞：速度反弹
                        glm::vec3 relVel = shipVel - obj.velocity;
                        float velAlongNormal = glm::dot(relVel, normal);
                        if (velAlongNormal > 0) { // 如果飞船正在撞向天体
                            float restitution = 0.3f; // 飞船的反弹系数 (0~1)
                            shipVel += normal * -(1.0f + restitution) * velAlongNormal;
                        }
                    }
                }
            }
            
            // 锁定第一人称视角到飞船
            gCamera.cameraPos = shipPos;
        }
        
        //调整最后一个天体的质量和大小
        UpdateCam(shaderProgram);
        if (!objs.empty() && objs.back().Initalizing) {
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                // 按下右键时，每秒质量增加100%
                objs.back().mass *= 1.0 + 1.0 * deltaTime;
                
                // 根据质量和密度计算新的半径
                objs.back().radius = pow(
                    (3 * objs.back().mass / objs.back().density) / 
                    (4 * 3.14159265359f), 
                    1.0f/3.0f
                ) / sizeRatio;
                
                // 更新网格顶点数据
                objs.back().UpdateVertices();
            }
        }

        // 使用八叉树计算引力和碰撞
        if (!pause) {
            octree->build(objs);
            octree->calculateForces(objs);
            octree->checkCollisions(objs);
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
        glDepthMask(GL_TRUE);
        // 点大小由顶点着色器写入 gl_PointSize，因此必须启用这个状态。
        glEnable(GL_PROGRAM_POINT_SIZE);
        // 前景层直接写入自己的 RGBA，交给最终合成处理透明度。
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 渲染网格
        glUseProgram(shaderProgram);
        glUniform4f(objectColorLoc, 1.0f, 1.0f, 1.0f, 0.25f);
        glUniform1i(glGetUniformLocation(shaderProgram, "isGrid"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram, "GLOW"), 0);
        gridVertices = UpdateGridVertices(gridVertices, objs);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_DYNAMIC_DRAW);
        DrawGrid(shaderProgram, gridVAO, gridVertices.size());

        // 绘制三角形和球体
        for(auto& obj : objs) {
            // 确保球体颜色的alpha通道为1.0
            glUniform4f(objectColorLoc, obj.color.r, obj.color.g, obj.color.b, 1.0f);
            
            if(obj.Initalizing){
                obj.radius = pow(((3 * obj.mass/obj.density)/(4 * 3.14159265359)), (1.0f/3.0f)) / 30000;
                obj.UpdateVertices();
            }

            // 更新位置
            if(!pause){
                obj.UpdatePos();
            }
            
            glm::mat4 model = glm::mat4(1.0f);//创建单位矩阵
            model = glm::translate(model, obj.position); //传矩阵至当前位置
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));//传递给着色器
            //渲染物体
            glUniform1i(glGetUniformLocation(shaderProgram, "isGrid"), 0);
            if(obj.glow){
                glUniform1i(glGetUniformLocation(shaderProgram, "GLOW"), 1);//1发光
            } else {
                glUniform1i(glGetUniformLocation(shaderProgram, "GLOW"), 0);//0不发光
            }
            
            glBindVertexArray(obj.VAO);
            glDrawArrays(GL_TRIANGLES, 0, obj.vertexCount / 3);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, starFB.fbo);
        glViewport(0, 0, gApp.width, gApp.height);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

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
        glUniform1i(glGetUniformLocation(postProgram, "uForeground"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, starFB.colorTex);
        glUniform1i(glGetUniformLocation(postProgram, "uStars"), 1);
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

        //交换双缓存，将当前帧渲染到屏幕上
        glfwSwapBuffers(window);
    }
    //销毁资源，相当于destroy
    for (auto& obj : objs) {
        glDeleteVertexArrays(1, &obj.VAO);
        glDeleteBuffers(1, &obj.VBO);
    }

    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);

    // 清理八叉树
    if (octree) {
        delete octree;
        octree = nullptr;
    }

    glDeleteFramebuffers(1, &sceneFB.fbo);
    glDeleteTextures(1, &sceneFB.colorTex);
    glDeleteTextures(1, &sceneFB.depthTex);
    glDeleteFramebuffers(1, &starFB.fbo);
    glDeleteTextures(1, &starFB.colorTex);

    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &starVBO);
    glDeleteVertexArrays(1, &starVAO);
    glDeleteProgram(starProgram);
    glDeleteProgram(postProgram);
    glDeleteProgram(shaderProgram);

    // 按初始化的逆序释放资源，避免依赖对象提前失效。
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
// 初始化GLFW和GLAD库
GLFWwindow* StartGLU() {
    // 初始化GLFW
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW, panic" << std::endl;
        return nullptr;
    }

    configureOpenGLWindowHints();

    //建窗口
    GLFWwindow* window = glfwCreateWindow(kInitialWidth, kInitialHeight, "3D_TEST", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    // 初始化GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD." << std::endl;
        glfwTerminate();
        return nullptr;
    }

    glEnable(GL_DEPTH_TEST);// 启用深度测试
    glViewport(0, 0, kInitialWidth, kInitialHeight);// 设置视口
    glEnable(GL_BLEND);// 启用混合
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);// 设置混合函数

    return window;
}

GLuint CreateShaderProgram(const char* vertexSource, const char* fragmentSource) {
    const std::string preparedVertexSource = prepareShaderSource(vertexSource);
    const std::string preparedFragmentSource = prepareShaderSource(fragmentSource);
    const char* vertexSourcePtr = preparedVertexSource.c_str();
    const char* fragmentSourcePtr = preparedFragmentSource.c_str();

    //顶点着色
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSourcePtr, nullptr);
    glCompileShader(vertexShader);

    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        return 0;
    }

    //片元着色
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSourcePtr, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    //着色程序
    GLuint shaderProgram = glCreateProgram();//创建着色程序
    glAttachShader(shaderProgram, vertexShader);//绑定顶点着色器
    glAttachShader(shaderProgram, fragmentShader);//绑定片元着色器
    glLinkProgram(shaderProgram);//链接着色程序
    // 检查链接状态
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
        glDeleteProgram(shaderProgram);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

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

    return CreateShaderProgram(vsSource.c_str(), fsSource.c_str());
}

bool createSceneFramebuffer(Framebuffer& fb, int width, int height) {
    if (fb.fbo) {
        // 重建前先释放旧附件；窗口缩放时会走到这里。
        glDeleteFramebuffers(1, &fb.fbo);
        glDeleteTextures(1, &fb.colorTex);
        glDeleteTextures(1, &fb.depthTex);
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

    glGenTextures(1, &fb.depthTex);
    glBindTexture(GL_TEXTURE_2D, fb.depthTex);
    // 深度改成纹理，后处理阶段才能识别哪里有天体或网格挡在前面。
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb.depthTex, 0);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        std::cerr << "Framebuffer is incomplete." << std::endl;
    }
    return complete;
}

bool createColorFramebuffer(Framebuffer& fb, int width, int height) {
    if (fb.fbo) {
        glDeleteFramebuffers(1, &fb.fbo);
        glDeleteTextures(1, &fb.colorTex);
        fb = {};
    }

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    glGenTextures(1, &fb.colorTex);
    glBindTexture(GL_TEXTURE_2D, fb.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.colorTex, 0);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        std::cerr << "Color framebuffer is incomplete." << std::endl;
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
    gApp.mode = mode;

    if (!gWindow) {
        return;
    }

    if (mode == AppState::ControlMode::ImGuiUI) {
        glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(gWindow, &xpos, &ypos);
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
    }
}

void toggleControlMode() {
    const AppState::ControlMode nextMode = (gApp.mode == AppState::ControlMode::SceneOnly)
        ? AppState::ControlMode::ImGuiUI
        : AppState::ControlMode::SceneOnly;
    setControlMode(nextMode);
}


void CreateVBOVAO(GLuint& VAO, GLuint& VBO, const float* vertices, size_t vertexCount) {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(float), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void UpdateCam(GLuint shaderProgram) {
    glUseProgram(shaderProgram);
    glm::mat4 view = gCamera.viewMatrix();
    GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}
//键盘事件响应
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    float cameraSpeed = 10000.0f * deltaTime;// 相机移动速度
    bool shiftPressed = (mods & GLFW_MOD_SHIFT) != 0;// 是否按下了Shift键
    // 只有当objs不为空时才获取最后一个物体的引用
    Object* lastObj = nullptr;
    if (!objs.empty()) {
        lastObj = &objs[objs.size() - 1];
    }
    // F键切换飞船模式
    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        spaceshipMode = !spaceshipMode;
        if (spaceshipMode) {
            // 切入飞船时，将飞船传送到当前相机位置
            std::cout << "Spaceship Mode: ON" << std::endl;
        } else {
            std::cout << "Spaceship Mode: OFF" << std::endl;
        }
    }

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        running = false;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    // 相机移动
    if (!spaceshipMode) {
        if (glfwGetKey(window, GLFW_KEY_W)==GLFW_PRESS) gCamera.cameraPos += cameraSpeed * gCamera.front();
        if (glfwGetKey(window, GLFW_KEY_S)==GLFW_PRESS) gCamera.cameraPos -= cameraSpeed * gCamera.front();
        if (glfwGetKey(window, GLFW_KEY_A)==GLFW_PRESS) gCamera.cameraPos -= cameraSpeed * glm::normalize(glm::cross(gCamera.front(), gCamera.up()));
        if (glfwGetKey(window, GLFW_KEY_D)==GLFW_PRESS) gCamera.cameraPos += cameraSpeed * glm::normalize(glm::cross(gCamera.front(), gCamera.up()));
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) gCamera.cameraPos += cameraSpeed * gCamera.up();
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) gCamera.cameraPos -= cameraSpeed * gCamera.up();
    }

    if(key == GLFW_KEY_K && action == GLFW_PRESS){
        pause = !pause;//切换状态
        std::cout << (pause ? "Simulation paused" : "Simulation resumed") << std::endl;
    }
    
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS){
        glfwTerminate();
        glfwWindowShouldClose(window);
        running = false;
    }

    const bool zDown = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    // 只在按下瞬间切一次，避免长按 Z 时在两种模式之间来回抖动。
    if (zDown && !gApp.modeTogglePressed) {
        toggleControlMode();
    }
    gApp.modeTogglePressed = zDown;

    //新天体位置调整
    if(lastObj && lastObj->Initalizing){
        const float MOVE_FACTOR = 0.2f;
        float moveDistance = lastObj->radius * MOVE_FACTOR;
        switch(key) {
            case GLFW_KEY_UP:
                if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                    if (shiftPressed) {
                        // Shift+Up: Z+
                        lastObj->position[2] += moveDistance;
                    } else {
                        // Up: Y+
                        lastObj->position[1] += moveDistance;
                    }
                }
                break;
            case GLFW_KEY_DOWN:
                if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                    if (shiftPressed) {
                        // Shift+Down: Z-
                        lastObj->position[2] -= moveDistance;
                    } else {
                        // Down: Y-
                        lastObj->position[1] -= moveDistance;
                    }
                }
                break;
            case GLFW_KEY_RIGHT:
                if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                    // Right: X+
                    lastObj->position[0] += moveDistance;
                }
                break;
            case GLFW_KEY_LEFT:
                if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                    // Left: X-
                    lastObj->position[0] -= moveDistance;
                }
                break;
        }
    };
    
};
//鼠标移动事件响应
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (gApp.mode == AppState::ControlMode::ImGuiUI) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        return;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; 
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    gCamera.yaw += xoffset;
    gCamera.pitch += yoffset;

    if(gCamera.pitch > 89.0f) gCamera.pitch = 89.0f;
    if(gCamera.pitch < -89.0f) gCamera.pitch = -89.0f;

}
// 鼠标按钮事件响应
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods){
    if (button == GLFW_MOUSE_BUTTON_RIGHT){
        if (action == GLFW_PRESS){
            objs.emplace_back(glm::vec3(500.0, 0.0, 0.0), glm::vec3(0.0f, 0.0f, 0.0f), initMass);
            // 只有当objs不为空时才访问最后一个元素
            if (!objs.empty()) {
                objs[objs.size()-1].Initalizing = true;
            }
        };
        if (action == GLFW_RELEASE){
            // 只有当objs不为空时才访问最后一个元素
            if (!objs.empty()) {
                objs[objs.size()-1].Initalizing = false;
                objs[objs.size()-1].Launched = true;
            }
        };
    };
    if (!objs.empty() && button == GLFW_MOUSE_BUTTON_LEFT && objs[objs.size()-1].Initalizing) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            objs[objs.size()-1].mass *= 1.2;
        }
        std::cout<<"MASS: "<<objs[objs.size()-1].mass<<std::endl;
    }
};
//鼠标滚轮响应
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset){
    if (gApp.mode == AppState::ControlMode::ImGuiUI) return;
    if (spaceshipMode) return; // 飞船模式下禁用鼠标滚轮瞬间移动
    float cameraSpeed = 250000.0f * deltaTime;
    if(yoffset>0){
        gCamera.cameraPos += cameraSpeed * gCamera.front();
    } else if(yoffset<0){
        gCamera.cameraPos -= cameraSpeed * gCamera.front();
    }
}
//坐标系转换
glm::vec3 sphericalToCartesian(float r, float theta, float phi){
    float x = r * sin(theta) * cos(phi);
    float y = r * cos(theta);
    float z = r * sin(theta) * sin(phi);
    return glm::vec3(x, y, z);
};
void DrawGrid(GLuint shaderProgram, GLuint gridVAO, size_t vertexCount) {
    glUseProgram(shaderProgram);
    glm::mat4 model = glm::mat4(1.0f); //网格对应的单位矩阵
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    glBindVertexArray(gridVAO);
    glPointSize(5.0f);//设置点大小
    glDrawArrays(GL_LINES, 0, vertexCount / 3);
    glBindVertexArray(0);
}
//创建网格结构，y固定
std::vector<float> CreateGridVertices(float size, int divisions, const std::vector<Object>& objs) {
    std::vector<float> vertices;
    float step = size / divisions;//每个网格单元的大小
    float halfSize = size / 2.0f; //网格半尺寸

    //x轴 水平方向，右为正向
    for (int yStep = 3; yStep <= 3; ++yStep) {
        float y = -halfSize*0.3f + yStep * step;
        for (int zStep = 0; zStep <= divisions; ++zStep) {
            float z = -halfSize + zStep * step;
            for (int xStep = 0; xStep < divisions; ++xStep) {
                float xStart = -halfSize + xStep * step;
                float xEnd = xStart + step;
                vertices.push_back(xStart); vertices.push_back(y); vertices.push_back(z);
                vertices.push_back(xEnd);   vertices.push_back(y); vertices.push_back(z);
            }
        }
    }
    //z轴 深度方向，后为正向
    for (int xStep = 0; xStep <= divisions; ++xStep) {
        float x = -halfSize + xStep * step;
        for (int yStep = 3; yStep <= 3; ++yStep) {
            float y = -halfSize*0.3f + yStep * step;
            for (int zStep = 0; zStep < divisions; ++zStep) {
                float zStart = -halfSize + zStep * step;
                float zEnd = zStart + step;
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(zStart);
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(zEnd);
            }
        }
    }
    return vertices;
}
std::vector<float> UpdateGridVertices(std::vector<float> vertices, const std::vector<Object>& objs){
    
    //质心计算
    float totalMass = 0.0f;
    float comY = 0.0f;
    for (const auto& obj : objs) {
        if (obj.Initalizing) continue;
        comY += obj.mass * obj.position.y;
        totalMass += obj.mass;
    }
    if (totalMass > 0) comY /= totalMass;
    //原始网格边界计算
    float originalMaxY = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < vertices.size(); i += 3) {
        originalMaxY = std::max(originalMaxY, vertices[i+1]);
    }

    float verticalShift = comY - originalMaxY;
    //std::cout<<"vertical shift: "<<verticalShift<<" |         comY: "<<comY<<"|            originalmaxy: "<<originalMaxY<<std::endl;

    //引力模拟计算
    for (int i = 0; i < vertices.size(); i += 3) {

        //广义相对论：质量弯曲空间
        glm::vec3 vertexPos(vertices[i], vertices[i+1], vertices[i+2]);
        glm::vec3 totalDisplacement(0.0f);
        for (const auto& obj : objs) {
            //f (obj.Initalizing) continue;
            glm::vec3 toObject = obj.GetPos() - vertexPos;//天体到网格点的向量
            float distance = glm::length(toObject);
            float distance_m = distance * 1000.0f;
            float rs = (2*G*obj.mass)/(c*c);//史瓦西半径

            float dz = 2 * sqrt(rs * (distance_m - rs));//弯曲公式
            totalDisplacement.y += dz * 2.0f;
        }
        vertices[i+1] = totalDisplacement.y + -abs(verticalShift);
    }

    return vertices;
}

