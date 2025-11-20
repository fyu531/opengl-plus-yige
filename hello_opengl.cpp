// main.cpp
// Modern OpenGL demo: GLAD + GLFW + GLM + stb + tinyobjloader
// Features: PBR (metallic-roughness) shading, model loading (.obj), textures, camera controls,
// HDR framebuffer, bloom (bright-pass + gaussian blur ping-pong), screen combine.
// Requires: glad (glad.c compiled into project), glfw3, glm, stb_image.h, tiny_obj_loader.h

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;

// -------------------- utility: read file --------------------
static string readFile(const string& path) {
    ifstream in(path);
    if (!in) {
        cerr << "Failed to open: " << path << "\n";
        return string();
    }
    stringstream ss; ss << in.rdbuf();
    return ss.str();
}

// -------------------- shader compile helpers --------------------
static GLuint compileShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[10240]; glGetShaderInfoLog(s, 10239, nullptr, buf);
        cerr << "Shader compile error: " << buf << "\n";
    }
    return s;
}
static GLuint createProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(vsSrc, GL_VERTEX_SHADER);
    GLuint fs = compileShader(fsSrc, GL_FRAGMENT_SHADER);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[10240]; glGetProgramInfoLog(p, 10239, nullptr, buf);
        cerr << "Program link error: " << buf << "\n";
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// -------------------- load texture (stb_image) --------------------
static GLuint loadTexture(const string& path, bool srgb = false, bool flip = true) {
    if (flip) stbi_set_flip_vertically_on_load(1);
    int w, h, c;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 0);
    if (!data) {
        cerr << "Failed to load texture: " << path << "\n";
        return 0;
    }
    GLenum internal = (c == 4) ? (srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8) : (srgb ? GL_SRGB8 : GL_RGB8);
    GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
    return tex;
}

// -------------------- simple camera --------------------
struct Camera {
    glm::vec3 pos = { 0.0f, 1.5f, 10.0f };
    glm::vec3 front = { 0.0f, 0.0f, -1.0f };
    glm::vec3 up = { 0.0f, 1.0f, 0.0f };
    float yaw = -90.0f, pitch = -10.0f;
    float fov = 45.0f;
    float lastX = 400, lastY = 300;
    bool firstMouse = true;
    bool mouseDown = false;
    void processMouseMove(float xpos, float ypos) {
        if (!mouseDown) { lastX = xpos; lastY = ypos; return; }
        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;
        lastX = xpos; lastY = ypos;
        float sensitivity = 0.18f;
        xoffset *= sensitivity; yoffset *= sensitivity;
        yaw += xoffset; pitch += yoffset;
        if (pitch > 89.0f) pitch = 89.0f; if (pitch < -89.0f) pitch = -89.0f;
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(dir);
    }
    glm::mat4 viewMatrix() { return glm::lookAt(pos, pos + front, up); }
};

// -------------------- model loading (tinyobj) --------------------
struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei count = 0;
};
static bool loadObjToMesh(const string& path, Mesh& mesh) {
    tinyobj::attrib_t attrib;
    vector<tinyobj::shape_t> shapes;
    vector<tinyobj::material_t> materials;
    string warn, err;
    string base = path.substr(0, path.find_last_of("/\\") + 1);
    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), base.c_str());
    if (!warn.empty()) cerr << "WARN: " << warn << "\n";
    if (!err.empty()) cerr << "ERR: " << err << "\n";
    if (!ok) return false;
    vector<float> data;
    vector<unsigned int> idx;
    unsigned int index = 0;
    for (auto& shape : shapes) {
        for (auto& f : shape.mesh.indices) {
            int vi = f.vertex_index;
            int ni = f.normal_index;
            int ti = f.texcoord_index;
            // position
            data.push_back(attrib.vertices[3 * vi + 0]);
            data.push_back(attrib.vertices[3 * vi + 1]);
            data.push_back(attrib.vertices[3 * vi + 2]);
            // normal
            if (ni >= 0) {
                data.push_back(attrib.normals[3 * ni + 0]);
                data.push_back(attrib.normals[3 * ni + 1]);
                data.push_back(attrib.normals[3 * ni + 2]);
            }
            else {
                data.push_back(0); data.push_back(1); data.push_back(0);
            }
            // uv
            if (ti >= 0) {
                data.push_back(attrib.texcoords[2 * ti + 0]);
                data.push_back(attrib.texcoords[2 * ti + 1]);
            }
            else { data.push_back(0.0f); data.push_back(0.0f); }
            idx.push_back(index++);
        }
    }
    // upload to GL
    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &mesh.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    // format: pos(3), norm(3), uv(2) => stride = 8 floats
    GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    mesh.count = (GLsizei)idx.size();
    return true;
}

// -------------------- screen quad for postprocess --------------------
static GLuint quadVAO = 0;
static void initQuad() {
    float quadVerts[] = {
        // positions   // uv
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    glGenVertexArrays(1, &quadVAO);
    GLuint qVBO; glGenBuffers(1, &qVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, qVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

// -------------------- Shaders (load from files) --------------------
string loadShaderText(const string& name) {
    string path = "shaders/" + name;
    string s = readFile(path);
    if (s.empty()) cerr << "Warning: shader empty or not found: " << path << "\n";
    return s;
}

// -------------------- main --------------------
int SCR_W = 2560, SCR_H = 1440;

// 新增：行走参数配置（可自定义）
const float walkSpeed = 2.0f;    // 行走速度（单位：米/秒）
const float walkRange = 4.0f;    // 来回行走的最大距离（总范围是 ±walkRange，即总长度 8 米）
const float walkAxis = 0.0f;     // 行走高度（Y轴，避免模型贴地或悬空）

Camera camera;
float lastFrame = 0.0f;
bool keys[1024];

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, 1);
    if (key >= 0 && key < 1024) {
        if (action == GLFW_PRESS) keys[key] = true;
        else if (action == GLFW_RELEASE) keys[key] = false;
    }
}
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        camera.mouseDown = (action == GLFW_PRESS);
        camera.firstMouse = true;
    }
}
void cursor_callback(GLFWwindow* window, double xpos, double ypos) {
    camera.processMouseMove((float)xpos, (float)ypos);
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.fov -= (float)yoffset;
    if (camera.fov < 15.0f) camera.fov = 15.0f;
    if (camera.fov > 90.0f) camera.fov = 90.0f;
}

void processKeyboard(float dt) {
    float speed = 4.0f;
    if (keys[GLFW_KEY_W]) camera.pos += camera.front * speed * dt;
    if (keys[GLFW_KEY_S]) camera.pos -= camera.front * speed * dt;
    glm::vec3 right = glm::normalize(glm::cross(camera.front, camera.up));
    if (keys[GLFW_KEY_A]) camera.pos -= right * speed * dt;
    if (keys[GLFW_KEY_D]) camera.pos += right * speed * dt;
    if (keys[GLFW_KEY_Q]) camera.pos -= camera.up * speed * dt;
    if (keys[GLFW_KEY_E]) camera.pos += camera.up * speed * dt;
}

// -------------------- build HDR framebuffer, ping-pong for blur --------------------
struct BloomFBO {
    GLuint hdrFBO = 0;
    GLuint colorBuffers[2]; // 0: normal HDR, 1: bright color
    GLuint pingpongFBO[2];
    GLuint pingpongColorbuffers[2];
    int width, height;
    void init(int w, int h) {
        width = w; height = h;
        // HDR framebuffer
        glGenFramebuffers(1, &hdrFBO); glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glGenTextures(2, colorBuffers);
        for (unsigned int i = 0;i < 2;i++) {
            glBindTexture(GL_TEXTURE_2D, colorBuffers[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorBuffers[i], 0);
        }
        GLuint rbo; glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
        unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, attachments);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) cerr << "HDR Framebuffer not complete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // ping-pong
        glGenFramebuffers(2, pingpongFBO);
        glGenTextures(2, pingpongColorbuffers);
        for (unsigned int i = 0;i < 2;i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
            glBindTexture(GL_TEXTURE_2D, pingpongColorbuffers[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffers[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) cerr << "Pingpong FBO not complete!\n";
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
} bloomFBO;

// -------------------- main program entry --------------------
int main() {
    // GLFW init
    if (!glfwInit()) { cerr << "glfw init failed\n"; return -1; }
    // OpenGL 3.3 core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_W, SCR_H, "PBR + Bloom + Model + Camera (GLAD+GLFW)", NULL, NULL);
    if (!window) { cerr << "create window failed\n"; glfwTerminate(); return -1; }

    glfwSetWindowAspectRatio(window,16 ,9);

    glfwMakeContextCurrent(window);
    // load glad
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { cerr << "Failed to initialize GLAD\n"; return -1; }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // callbacks
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // load shaders from files
    string pbr_vs = loadShaderText("pbr.vs");
    string pbr_fs = loadShaderText("pbr.fs");
    string quad_vs = loadShaderText("quad.vs");
    string bright_fs = loadShaderText("bright_extract.fs");
    string blur_fs = loadShaderText("gaussian_blur.fs");
    string combine_fs = loadShaderText("bloom_combine.fs");

    GLuint pbrProg = createProgram(pbr_vs.c_str(), pbr_fs.c_str());
    GLuint brightProg = createProgram(quad_vs.c_str(), bright_fs.c_str());
    GLuint blurProg = createProgram(quad_vs.c_str(), blur_fs.c_str());
    GLuint combineProg = createProgram(quad_vs.c_str(), combine_fs.c_str());

    // load model (replace with your model path)
    Mesh mesh;
    bool ok = loadObjToMesh("resources/model.obj", mesh);
    if (!ok) { cerr << "Failed to load model.obj\n"; /* still continue to show something */ }

    // default textures (if missing, create 1x1 white)
    GLuint texAlbedo = loadTexture("resources/albedo.png");
    if (!texAlbedo) {
        unsigned char white[3] = { 255,255,255 };
        glGenTextures(1, &texAlbedo); glBindTexture(GL_TEXTURE_2D, texAlbedo);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    GLuint texNormal = loadTexture("resources/normal.png");
    GLuint texMetallic = loadTexture("resources/metallic.png");
    GLuint texRoughness = loadTexture("resources/roughness.png");
    GLuint texAO = loadTexture("resources/ao.png");

    // screen quad
    initQuad();

    // build bloom FBOs
    bloomFBO.init(SCR_W, SCR_H);

    // default uniforms binding
    glUseProgram(pbrProg);
    glUniform1i(glGetUniformLocation(pbrProg, "albedoMap"), 0);
    glUniform1i(glGetUniformLocation(pbrProg, "normalMap"), 1);
    glUniform1i(glGetUniformLocation(pbrProg, "metallicMap"), 2);
    glUniform1i(glGetUniformLocation(pbrProg, "roughnessMap"), 3);
    glUniform1i(glGetUniformLocation(pbrProg, "aoMap"), 4);

    glUseProgram(brightProg); glUniform1i(glGetUniformLocation(brightProg, "scene"), 0);
    glUseProgram(blurProg); glUniform1i(glGetUniformLocation(blurProg, "image"), 0);
    glUseProgram(combineProg); glUniform1i(glGetUniformLocation(combineProg, "scene"), 0);
    glUseProgram(combineProg); glUniform1i(glGetUniformLocation(combineProg, "bloomBlur"), 1);

    // time loop
    float time = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float delta = currentFrame - lastFrame;
        lastFrame = currentFrame;
        processKeyboard(delta);
        time = currentFrame;

        // 1. render scene into floating point framebuffer (HDR)
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO.hdrFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // set viewport
        glViewport(0, 0, SCR_W, SCR_H);

        // use pbr shader
        glUseProgram(pbrProg);
        // set matrices
        glm::mat4 proj = glm::perspective(glm::radians(camera.fov), (float)SCR_W / (float)SCR_H, 0.1f, 100.0f);
        glm::mat4 view = camera.viewMatrix();
        glm::mat4 model_m = glm::mat4(1.0f);

        float ScaleFactor = 3.0f;
        model_m = glm::scale(model_m, glm::vec3(ScaleFactor));


        float walkPos = cos(currentFrame * walkSpeed) * walkRange;
        // 沿 X 轴来回走（也可改成 Z 轴：glm::vec3(0.0f, walkAxis, walkPos)）
        model_m = glm::translate(model_m, glm::vec3(walkPos, walkAxis, 0.0f));


        model_m = glm::rotate(model_m, currentFrame * glm::radians(60.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // uniforms
        glUniformMatrix4fv(glGetUniformLocation(pbrProg, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(pbrProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(pbrProg, "model"), 1, GL_FALSE, glm::value_ptr(model_m));
        glUniform3fv(glGetUniformLocation(pbrProg, "camPos"), 1, glm::value_ptr(camera.pos));
        glUniform1f(glGetUniformLocation(pbrProg, "time"), time);

        // lights (two moving lights)
        glm::vec3 lightPosA = glm::vec3(5.0f * cos(time * 0.6f), 4.0f + sin(time * 0.7f), 5.0f * sin(time * 0.6f));
        glm::vec3 lightColA = glm::vec3(1.0f, 0.9f, 0.7f);
        glm::vec3 lightPosB = glm::vec3(-6.0f * cos(time * 0.4f), 3.4f + 0.3f * sin(time * 0.9f), -6.0f * sin(time * 0.4f));
        glm::vec3 lightColB = glm::vec3(0.4f, 0.7f, 1.0f);
        glUniform3fv(glGetUniformLocation(pbrProg, "lightPosA"), 1, glm::value_ptr(lightPosA));
        glUniform3fv(glGetUniformLocation(pbrProg, "lightColorA"), 1, glm::value_ptr(lightColA));
        glUniform3fv(glGetUniformLocation(pbrProg, "lightPosB"), 1, glm::value_ptr(lightPosB));
        glUniform3fv(glGetUniformLocation(pbrProg, "lightColorB"), 1, glm::value_ptr(lightColB));

        // bind textures
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texAlbedo);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texNormal ? texNormal : texAlbedo);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texMetallic ? texMetallic : texAlbedo);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, texRoughness ? texRoughness : texAlbedo);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, texAO ? texAO : texAlbedo);

        if (mesh.vao) {
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
        else {
            // fallback: draw a simple cube using immediate mode fallback (not ideal)
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. extract bright parts to pingpong buffers (brightProg reads colorBuffers[1])
        glUseProgram(brightProg);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO.pingpongFBO[0]); // render once into pingpong0 first
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomFBO.colorBuffers[1]);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 3. blur bright image (ping-pong)
        bool horizontal = true;
        int blurPasses = 15;
        glUseProgram(blurProg);
        for (int i = 0;i < blurPasses;i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO.pingpongFBO[horizontal]);
            glUniform1i(glGetUniformLocation(blurProg, "horizontal"), horizontal ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            if (i == 0) glBindTexture(GL_TEXTURE_2D, bloomFBO.pingpongColorbuffers[0]); // first is bright result
            else glBindTexture(GL_TEXTURE_2D, bloomFBO.pingpongColorbuffers[!horizontal]);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            horizontal = !horizontal;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 4. final composition
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(combineProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomFBO.colorBuffers[0]); // original HDR scene color
        glActiveTexture(GL_TEXTURE1);
        // final blurred texture is in pingpongColorbuffers[!horizontal]
        glBindTexture(GL_TEXTURE_2D, bloomFBO.pingpongColorbuffers[!horizontal]);
        glUniform1f(glGetUniformLocation(combineProg, "exposure"), 8.0f);
        glUniform1f(glGetUniformLocation(combineProg, "bloomIntensity"), 8.2f);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // swap
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
