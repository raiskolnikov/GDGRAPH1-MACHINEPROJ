#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using namespace glm;

// global booleans
bool nightvision = false;   // whether night vision is active
bool topvision = false; // whether top-down view is active

// abstract base class for lights
class Light {
public:
    vec3 color;
    float intensity;

    // construct new light object
    Light(vec3 color = vec3(1.f), float intensity = 1.f)
        : color(color), intensity(intensity) {
    }

	// apply light parameters to shader
    virtual void apply(GLuint shaderProg) = 0;

    // destructor
    virtual ~Light() = default;
};

// directional light
class DirectionLight : public Light {
public:
    vec3 direction;

    // simulate moonlight
    DirectionLight(vec3 dir = normalize(vec3(0.f, -1.f, 1.f)),
        vec3 col = vec3(0.6f, 0.6f, 0.9f),
        float intensity = 1.5f)
        : Light(col, intensity), direction(normalize(dir)) {
    }

	// apply directional light parameters to shader
    void apply(GLuint shaderProg) override {
        glUniform3fv(glGetUniformLocation(shaderProg, "dirLight.direction"),
            1, value_ptr(direction));
        glUniform3fv(glGetUniformLocation(shaderProg, "dirLight.color"),
            1, value_ptr(color));
        glUniform1f(glGetUniformLocation(shaderProg, "dirLight.intensity"),
            intensity);
    }
};

// point light
class PointLight : public Light {
public:
    vec3 position;
    float constant;
    float linear;
    float quadratic;

    // light intensity levels
    enum Level { LOW = 0, MEDIUM, HIGH };
    Level level = MEDIUM;

    // intensity values for each level
    static constexpr float levels[3] = { 0.3f, 1.5f, 3.0f };

    // white point light
    PointLight(vec3 pos = vec3(0.f), vec3 col = vec3(1.f, 0.95f, 0.8f))
        : Light(col, levels[MEDIUM]), position(pos) {
        constant = 1.f;
        linear = 0.14f;
        quadratic = 0.07f;
    }

	// cycle through intensity levels (Low, Medium, High)
    void cycleIntensity() {
        level = static_cast<Level>((level + 1) % 3);
        intensity = levels[level];
        cout << "PointLight intensity: "
            << (level == LOW ? "LOW" : level == MEDIUM ? "MEDIUM" : "HIGH")
            << endl;
    }

	// apply point light parameters to shader
    void apply(GLuint shaderProg) override {
        glUniform3fv(glGetUniformLocation(shaderProg, "pointLight.position"),
            1, value_ptr(position));
        glUniform3fv(glGetUniformLocation(shaderProg, "pointLight.color"),
            1, value_ptr(color));
        glUniform1f(glGetUniformLocation(shaderProg, "pointLight.intensity"),
            intensity);
        glUniform1f(glGetUniformLocation(shaderProg, "pointLight.constant"),
            constant);
        glUniform1f(glGetUniformLocation(shaderProg, "pointLight.linear"),
            linear);
        glUniform1f(glGetUniformLocation(shaderProg, "pointLight.quadratic"),
            quadratic);
    }
};

constexpr float PointLight::levels[3];

// tank class
class Tank {
public:
    vec3 position = vec3(0.f, 0.f, 0.f);
    float yaw = 0.f;

private:
    GLuint shaderProg = 0;
    GLuint VAO = 0;
    GLuint VBO = 0;

    vector<float> interleavedData;  // vertex data (pos, normal, uv, tangent)

	// submesh struct for each material group in tank model
    struct SubMesh {
        int startIndex;
        int count;
        GLuint texDiffuse = 0;
        GLuint texEmissive = 0;
        GLuint texNormal = 0;
    };
    vector<SubMesh> subMeshes;

    // return shader after compiling
    GLuint compileShader(GLenum type, const char* src) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, NULL);
        glCompileShader(id);

        GLint ok;
        char log[512];
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(id, 512, NULL, log);
            cout << (type == GL_VERTEX_SHADER ? "TANK VERT: " : "TANK FRAG: ")
                << log << endl;
        }
        return id;
    }

    // read file from string path and return content as string
    string readFile(const string& path) {
        fstream f(path);
        stringstream buf;
        buf << f.rdbuf();
        return buf.str();
    }

    // load 2d texture from file
    GLuint loadTexture(const string& path) {
        if (path.empty()) return 0;

        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        // set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // load image data
        int w, h, c;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 0);

        if (data) {
            GLenum fmt = (c == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
            stbi_image_free(data);
            cout << "Tank texture loaded: " << path << endl;
        }
        else {
            cout << "Tank texture FAILED: " << path << endl;
            id = 0;
        }

        return id;
    }

	// compute tangent for normal vector mapping
    void computeTangents() {
        for (auto& sm : subMeshes) {
            int base = sm.startIndex * 11;

            // process each triangle
            for (int i = 0; i < sm.count; i += 3) {
                int i0 = base + (i + 0) * 11;
                int i1 = base + (i + 1) * 11;
                int i2 = base + (i + 2) * 11;

                // extract positions
                vec3 p0(interleavedData[i0 + 0], interleavedData[i0 + 1], interleavedData[i0 + 2]);
                vec3 p1(interleavedData[i1 + 0], interleavedData[i1 + 1], interleavedData[i1 + 2]);
                vec3 p2(interleavedData[i2 + 0], interleavedData[i2 + 1], interleavedData[i2 + 2]);

                // extract UVs
                vec2 uv0(interleavedData[i0 + 6], interleavedData[i0 + 7]);
                vec2 uv1(interleavedData[i1 + 6], interleavedData[i1 + 7]);
                vec2 uv2(interleavedData[i2 + 6], interleavedData[i2 + 7]);

                // calculate edges
                vec3 edge1 = p1 - p0;
                vec3 edge2 = p2 - p0;
                vec2 dUV1 = uv1 - uv0;
                vec2 dUV2 = uv2 - uv0;

                // compute tangent
                float det = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
                vec3 tangent(0.f);
                if (abs(det) > 1e-6f) {
                    float f = 1.f / det;
                    tangent = normalize(f * (dUV2.y * edge1 - dUV1.y * edge2));
                }

                // assign tangent to all three vertices
                for (int v = 0; v < 3; v++) {
                    int iv = base + (i + v) * 11;
                    interleavedData[iv + 8] = tangent.x;
                    interleavedData[iv + 9] = tangent.y;
                    interleavedData[iv + 10] = tangent.z;
                }
            }
        }
    }

public:
	// load tank from obj and mtl files, build interleaved vertex data
    void load(const string& objPath, const string& mtlDir) {
        tinyobj::attrib_t attrib;
        vector<tinyobj::shape_t> shapes;
        vector<tinyobj::material_t> materials;
        string warn, err;

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials,
            &warn, &err, objPath.c_str(),
            mtlDir.c_str(), true);

        cout << "Tank OBJ load: " << ok << " (" << objPath << ")" << endl;
        if (!warn.empty()) cout << "  warn: " << warn << endl;
        if (!err.empty())  cout << "  err:  " << err << endl;
        if (!ok || shapes.empty()) return;

        // group faces by material ID
        map<int, vector<tinyobj::index_t>> groups;
        for (auto& shape : shapes) {
            size_t offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                int matID = shape.mesh.material_ids[f];
                int fv = shape.mesh.num_face_vertices[f];
                for (int v = 0; v < fv; v++) {
                    groups[matID].push_back(shape.mesh.indices[offset + v]);
                }
                offset += fv;
            }
        }

        // build interleaved vertex data for each material group
        for (auto& [matID, indices] : groups) {
            SubMesh sm;
            sm.startIndex = (int)(interleavedData.size() / 11);

            for (auto& idx : indices) {
                // position
                interleavedData.push_back(attrib.vertices[3 * idx.vertex_index + 0]);
                interleavedData.push_back(attrib.vertices[3 * idx.vertex_index + 1]);
                interleavedData.push_back(attrib.vertices[3 * idx.vertex_index + 2]);

                // normal
                if (idx.normal_index >= 0 && !attrib.normals.empty()) {
                    interleavedData.push_back(attrib.normals[3 * idx.normal_index + 0]);
                    interleavedData.push_back(attrib.normals[3 * idx.normal_index + 1]);
                    interleavedData.push_back(attrib.normals[3 * idx.normal_index + 2]);
                }
                else {
                    interleavedData.push_back(0.f);
                    interleavedData.push_back(1.f);
                    interleavedData.push_back(0.f);
                }

                // texture coordinates
                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty() &&
                    (2 * idx.texcoord_index + 1) < (int)attrib.texcoords.size()) {
                    interleavedData.push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
                    interleavedData.push_back(attrib.texcoords[2 * idx.texcoord_index + 1]);
                }
                else {
                    interleavedData.push_back(0.f);
                    interleavedData.push_back(0.f);
                }

                // tangent (computed later)
                interleavedData.push_back(0.f);
                interleavedData.push_back(0.f);
                interleavedData.push_back(0.f);
            }

            sm.count = (int)(interleavedData.size() / 11) - sm.startIndex;
            subMeshes.push_back(sm);
        }

        computeTangents();
    }

    // setup buffer and shader program
    void setup() {
        if (interleavedData.empty()) {
            cout << "Tank: no geometry." << endl;
            return;
        }

        // load and compile shaders
        string vertS = readFile("Shaders/tank.vert");
        string fragS = readFile("Shaders/tank.frag");

        GLuint v = compileShader(GL_VERTEX_SHADER, vertS.c_str());
        GLuint f = compileShader(GL_FRAGMENT_SHADER, fragS.c_str());

        // link shader program
        shaderProg = glCreateProgram();
        glAttachShader(shaderProg, v);
        glAttachShader(shaderProg, f);
        glLinkProgram(shaderProg);

        GLint ok;
        char log[512];
        glGetProgramiv(shaderProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            glGetProgramInfoLog(shaderProg, 512, NULL, log);
            cout << "TANK LINK: " << log << endl;
        }

        glDeleteShader(v);
        glDeleteShader(f);

        // set up vertex buffers
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * interleavedData.size(),
            interleavedData.data(), GL_STATIC_DRAW);

        GLsizei stride = 11 * sizeof(float);

        // position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);

        // normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // UV attribute
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
            (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        // tangent attribute
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

	// load textures for tank and assign to submeshes
    void loadTextures(const string& diffusePath, const string& emissivePath,
        const string& normalPath) {
        GLuint tDiff = loadTexture(diffusePath);
        GLuint tEmit = loadTexture(emissivePath);
        GLuint tNorm = loadTexture(normalPath);

        for (auto& sm : subMeshes) {
            sm.texDiffuse = tDiff;
            sm.texEmissive = tEmit;
            sm.texNormal = tNorm;
        }
    }

    // initialize tank
    void init() {
        load("3D/Tank/source/tank.obj", "3D/Tank/source/");
        setup();
        loadTextures(
            "3D/Tank/textures/vh_megatron_film_03.png",
            "3D/Tank/textures/vh_megatron_film_03_e.png",
            "3D/Tank/textures/vh_megatron_film_03_n.png"
        );
    }

    // render tank with lighting
    void draw(mat4 view, mat4 projection, vec3 camPos,
        DirectionLight& dirLight, PointLight& pointLight) {
        if (!shaderProg) return;

        // update point light position to be in front of tank
        pointLight.position = position + forward() * 2.f + vec3(0.f, 0.8f, 0.f);

        glUseProgram(shaderProg);

        // set transformation matrices
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "model"),
            1, GL_FALSE, value_ptr(getModel()));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "view"),
            1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "projection"),
            1, GL_FALSE, value_ptr(projection));

        // set lighting parameters
        glUniform3f(glGetUniformLocation(shaderProg, "ambientColor"),
            0.15f, 0.15f, 0.2f);
        glUniform3f(glGetUniformLocation(shaderProg, "viewPos"),
            camPos.x, camPos.y, camPos.z);

        // apply lights
        dirLight.apply(shaderProg);
        pointLight.apply(shaderProg);

        // draw each submesh
        glBindVertexArray(VAO);
        for (auto& sm : subMeshes) {
            if (sm.count == 0) continue;

            // bind textures
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sm.texDiffuse);
            glUniform1i(glGetUniformLocation(shaderProg, "tex0"), 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, sm.texEmissive);
            glUniform1i(glGetUniformLocation(shaderProg, "tex1"), 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, sm.texNormal);
            glUniform1i(glGetUniformLocation(shaderProg, "tex2"), 2);

            glDrawArrays(GL_TRIANGLES, sm.startIndex, sm.count);
        }
    }

	// get model transformtion matrix
    mat4 getModel() const {
        mat4 model = mat4(1.f);
        model = translate(model, position);
        model = rotate(model, radians(90.f), vec3(0.f, 1.f, 0.f));
        model = rotate(model, radians(yaw), vec3(0.f, 1.f, 0.f));
        model = scale(model, vec3(0.05f));
        return model;
    }

	// get tank forward normalized direction vector
    vec3 forward() const {
        return normalize(vec3(sin(radians(yaw)), 0.f, -cos(radians(yaw))));
    }

    // get tank right normalized direction vector
    vec3 right() const {
        return normalize(cross(forward(), vec3(0.f, 1.f, 0.f)));
    }

	// get first person camera taarget position
    vec3 firstPersonEye() const {
        return position + vec3(0.f, 1.2f, 0.f);
    }

    // get third-person camera eye position
    vec3 thirdPersonEye(float distance = 1.5f, float height = 1.5f) const {
        return position - forward() * distance + vec3(0.f, height, 0.f);
    }

    // get third-person camera target position
    vec3 thirdPersonTarget() const {
        return position + vec3(0.f, 1.f, 0.f);
    }
};

// forward declaration for camera-tank interaction
class Tank;

// abstract base camera class
class myCamera {
public:
    vec3 position = vec3(0.f);            // camera position in world space
    vec3 front = vec3(0.f, 0.f, -1.f);   // camera forward direction
    vec3 up = vec3(0.f, 1.f, 0.f);       // camera up direction

    float yaw = -90.f;    // camera yaw angle
    float pitch = 0.f;    // camera pitch angle
    float fov = 60.f;     // field of view

    bool allowMouse = true;  // whether mouse input affects this camera

    // virtual update function
    virtual void update(GLFWwindow* window, float dt, Tank& tank) = 0;

    // return view matrix
    virtual mat4 getView() {
        return lookAt(position, position + front, up);
    }

    // return projection matrix
    virtual mat4 getProjection(float aspect) {
        return perspective(radians(fov), aspect, 0.1f, 100.f);
    }

    // process mouse movment
    void processMouse(float xOffset, float yOffset) {
        if (!allowMouse) return;

        float sensitivity = 0.1f;
        xOffset *= sensitivity;
        yOffset *= sensitivity;

        yaw += xOffset;
        pitch += yOffset;

        // clamp pitch to prevent gimbal lock
        if (pitch > 89.f)  pitch = 89.f;
        if (pitch < -89.f) pitch = -89.f;

        // Recalculate front vector
        front = normalize(vec3(
            cos(radians(yaw)) * cos(radians(pitch)),
            sin(radians(pitch)),
            sin(radians(yaw)) * cos(radians(pitch))
        ));
    }
};

// camera that orbits around tank
class ThirdPersonCamera : public myCamera {
public:
    float distance = 1.5f;

    ThirdPersonCamera() {
        allowMouse = true;
    }

     // WASD input to move tank and mouse to update camera position
    void update(GLFWwindow* window, float dt, Tank& tank) override {
        topvision = false;

        float speed = 5.f * dt;

        // tank movement
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            tank.position += tank.forward() * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            tank.position -= tank.forward() * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            tank.position -= tank.right() * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            tank.position += tank.right() * speed;

        // limit pitch for third-person view
        if (pitch > -5.f) pitch = -5.f;

        // calculate camera offset
        vec3 offset;
        offset.x = distance * cos(radians(pitch)) * cos(radians(yaw));
        offset.y = distance * sin(radians(pitch));
        offset.z = distance * cos(radians(pitch)) * sin(radians(yaw));

        position = tank.position - offset;
        front = normalize(tank.position - position);
    }
};

// for night vision mode
class FirstPersonCamera : public myCamera {
public:
    FirstPersonCamera() {
        allowMouse = false;
        fov = 60.f;
    }

    // Q and E keys control zoom
    void update(GLFWwindow* window, float dt, Tank& tank) override {
        position = tank.firstPersonEye();
        front = tank.forward();

        // zoom control
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            fov -= 50.f * dt;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            fov += 50.f * dt;

        // clamp FOV
        if (fov < 20.f) fov = 20.f;
        if (fov > 90.f) fov = 90.f;
    }
};

// orthographic camera top-down view
class TopCamera : public myCamera {
public:
    float size = 10.f;
    vec3 targetPos = vec3(0.f);

    TopCamera() {
        allowMouse = false;
    }

    // camera looks straight down at tank from above 
    void update(GLFWwindow*, float, Tank& tank) override {
        topvision = true;
        targetPos = tank.position;
        position = tank.position + vec3(0.f, 10.f, 0.f);
        front = vec3(0.f, -1.f, 0.f);
        up = vec3(0.f, 0.f, -1.f);
    }

    mat4 getView() override {
        return lookAt(position, targetPos, up);
    }

    mat4 getProjection(float aspect) override {
        return ortho(-size * aspect, size * aspect, -size, size, 0.1f, 200.f);
    }
};

// pointer to currently active camera
myCamera* activeCam = nullptr;

// GLFW mouse callback to control camera orientation
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    static bool firstMouse = true;
    static float lastX = 400, lastY = 400;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xOffset = xpos - lastX;
    float yOffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    if (activeCam)
        activeCam->processMouse(xOffset, yOffset);
}

// base class for eva units
class Model {
public:
    GLuint shaderProg;
    GLuint VAO, VBO;

    vector<tinyobj::material_t> materials;
    tinyobj::attrib_t attributes;
    vector<float> interleavedData; // vertex data (position, normal, uv)

    // submeshes are material group within model
    struct SubMesh {
        int startIndex;
        int count;
        GLuint tex;
        string texPath;
    };
    vector<SubMesh> subMeshes;

	// load model from obj and mtl files, build interleaved vertex data
    void load(const string& objPath, const string& mtlDir) {
        vector<tinyobj::shape_t> shapes;
        string warning, error;

        bool success = tinyobj::LoadObj(
            &attributes, &shapes, &materials,
            &warning, &error,
            objPath.c_str(), mtlDir.c_str(), true
        );

        cout << "OBJ load success: " << success << " (" << objPath << ")" << endl;
        if (!warning.empty()) cout << "  warning: " << warning << endl;
        if (!error.empty())   cout << "  error: " << error << endl;
        if (shapes.empty()) {
            cout << "ERROR: No shapes loaded." << endl;
            return;
        }

        // group faces by material
        map<int, vector<tinyobj::index_t>> groups;
        for (size_t s = 0; s < shapes.size(); s++) {
            size_t indexOffset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
                int matID = shapes[s].mesh.material_ids[f];
                int fv = shapes[s].mesh.num_face_vertices[f];
                for (int v = 0; v < fv; v++) {
                    groups[matID].push_back(shapes[s].mesh.indices[indexOffset + v]);
                }
                indexOffset += fv;
            }
        }

        // build interleaved vertex data
        for (auto& [matID, indices] : groups) {
            SubMesh sm;
            sm.startIndex = (int)(interleavedData.size() / 8);
            sm.tex = 0;
            sm.texPath = "";

            // Get texture path from material
            if (matID >= 0 && matID < (int)materials.size()) {
                string texName = materials[matID].diffuse_texname;
                if (!texName.empty())
                    sm.texPath = texName;
            }

            for (auto& idx : indices) {
                // position
                interleavedData.push_back(attributes.vertices[3 * idx.vertex_index + 0]);
                interleavedData.push_back(attributes.vertices[3 * idx.vertex_index + 1]);
                interleavedData.push_back(attributes.vertices[3 * idx.vertex_index + 2]);

                // normal
                if (idx.normal_index >= 0 && !attributes.normals.empty()) {
                    interleavedData.push_back(attributes.normals[3 * idx.normal_index + 0]);
                    interleavedData.push_back(attributes.normals[3 * idx.normal_index + 1]);
                    interleavedData.push_back(attributes.normals[3 * idx.normal_index + 2]);
                }
                else {
                    interleavedData.push_back(0.f);
                    interleavedData.push_back(1.f);
                    interleavedData.push_back(0.f);
                }

                // texture coordinates
                if (idx.texcoord_index >= 0 && !attributes.texcoords.empty() &&
                    (2 * idx.texcoord_index + 1) < (int)attributes.texcoords.size()) {
                    interleavedData.push_back(attributes.texcoords[2 * idx.texcoord_index + 0]);
                    interleavedData.push_back(attributes.texcoords[2 * idx.texcoord_index + 1]);
                }
                else {
                    interleavedData.push_back(0.f);
                    interleavedData.push_back(0.f);
                }
            }

            sm.count = (int)(interleavedData.size() / 8) - sm.startIndex;
            subMeshes.push_back(sm);
        }
    }

	// setup buffers and shader program
    void setup() {
        if (interleavedData.empty()) {
            cout << "ERROR: No interleaved data." << endl;
            return;
        }

        // load shaders
        fstream vertSrc("Shaders/units.vert");
        stringstream vertBuff;
        vertBuff << vertSrc.rdbuf();
        string vertS = vertBuff.str();
        const char* vert = vertS.c_str();

        fstream fragSrc("Shaders/units.frag");
        stringstream fragBuff;
        fragBuff << fragSrc.rdbuf();
        string fragS = fragBuff.str();
        const char* frag = fragS.c_str();

        // compile vertex shader
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &vert, NULL);
        glCompileShader(vertShader);
        {
            GLint ok;
            char log[512];
            glGetShaderiv(vertShader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                glGetShaderInfoLog(vertShader, 512, NULL, log);
                cout << "VERT: " << log << endl;
            }
        }

        // compile fragment shader
        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragShader, 1, &frag, NULL);
        glCompileShader(fragShader);
        {
            GLint ok;
            char log[512];
            glGetShaderiv(fragShader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                glGetShaderInfoLog(fragShader, 512, NULL, log);
                cout << "FRAG: " << log << endl;
            }
        }

        // link program
        shaderProg = glCreateProgram();
        glAttachShader(shaderProg, vertShader);
        glAttachShader(shaderProg, fragShader);
        glLinkProgram(shaderProg);
        {
            GLint ok;
            char log[512];
            glGetProgramiv(shaderProg, GL_LINK_STATUS, &ok);
            if (!ok) {
                glGetProgramInfoLog(shaderProg, 512, NULL, log);
                cout << "LINK: " << log << endl;
            }
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        // set up vertex buffers
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * interleavedData.size(),
            interleavedData.data(), GL_STATIC_DRAW);

        GLsizei stride = 8 * sizeof(float);

        // Position
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);

        // Normal
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // UV
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
            (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // load textures for model
    void texture(vector<string> paths) {
        auto loadTex = [](const string& path, GLuint& texID) {
            if (path.empty()) {
                texID = 0;
                return;
            }

            glGenTextures(1, &texID);
            glBindTexture(GL_TEXTURE_2D, texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            int w, h, c;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 0);

            if (data) {
                GLenum fmt = (c == 4) ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
                glGenerateMipmap(GL_TEXTURE_2D);
                stbi_image_free(data);
                cout << "Loaded texture: " << path << endl;
            }
            else {
                cout << "Failed to load texture: " << path << endl;
                texID = 0;
            }
            };

        // load individual textures per submesh
        if (paths.size() == 1) {
            GLuint sharedTex = 0;
            loadTex(paths[0], sharedTex);
            for (auto& sm : subMeshes)
                sm.tex = sharedTex;
        }
        else {
            for (size_t i = 0; i < subMeshes.size() && i < paths.size(); i++)
                loadTex(paths[i], subMeshes[i].tex);
        }
    }

    // draw model with lighting
    void draw(mat4 view, mat4 projection, mat4 model,
        DirectionLight& dirLight, PointLight& pointLight) {
        glUseProgram(shaderProg);

        // set matrices
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "model"),
            1, GL_FALSE, value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "view"),
            1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "projection"),
            1, GL_FALSE, value_ptr(projection));

        // set ambient light
        glUniform3f(glGetUniformLocation(shaderProg, "ambientColor"),
            0.15f, 0.15f, 0.2f);

        // apply lights
        dirLight.apply(shaderProg);
        pointLight.apply(shaderProg);

        // draw submeshes
        glBindVertexArray(VAO);
        for (auto& sm : subMeshes) {
            if (sm.tex == 0 || sm.count == 0) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sm.tex);
            glUniform1i(glGetUniformLocation(shaderProg, "tex0"), 0);
            glDrawArrays(GL_TRIANGLES, sm.startIndex, sm.count);
        }
    }

    // return model transformation matrix
    virtual mat4 getModel() = 0;

    // draw using model matrix and with light
    void draw(mat4 view, mat4 projection, DirectionLight& dirLight,
        PointLight& pointLight) {
        draw(view, projection, getModel(), dirLight, pointLight);
    }
};

// unit models

// Eva Unit-00 (yellow)
class Eva00 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-00/source/Unit00.obj", "3D/Mechas/Eva-00/source/");
        setup();
        texture({ "3D/Mechas/Eva-00/textures/e00_201.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(1.5f, 0.f, -5.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// Eva Unit-01 (purple)
class Eva01 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-01/source/Unit01.obj", "3D/Mechas/Eva-01/source/");
        setup();
        texture({ "3D/Mechas/Eva-01/textures/e01_201.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(4.5f, 0.f, -4.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// Eva Unit-02 (red)
class Eva02 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-02/source/Unit02.obj", "3D/Mechas/Eva-02/source/");
        setup();
        texture({ "3D/Mechas/Eva-02/textures/e02_201.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(-4.5f, 0.f, -4.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// Eva Unit-03 (black)
class Eva03 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-03/source/Unit03.obj", "3D/Mechas/Eva-03/source/");
        setup();
        texture({ "3D/Mechas/Eva-03/textures/e03_201.png",
                  "3D/Mechas/Eva-03/textures/w03_001.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(-7.0f, 0.f, -3.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// Eva Unit-04 (white)
class Eva04 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-04/source/Unit04.obj", "3D/Mechas/Eva-04/source/");
        setup();
        texture({ "3D/Mechas/Eva-04/textures/e04_201.png",
                  "3D/Mechas/Eva-04/textures/w04_001.png",
                  "3D/Mechas/Eva-04/textures/w04_201.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(-1.5f, 0.f, -5.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// i didnt use unit 05 bc it looks so diff from the other mechs

// Eva Unit-06 (blue)
class Eva06 : public Model {
public:
    void init() {
        load("3D/Mechas/Eva-06/source/Unit06.obj", "3D/Mechas/Eva-06/source/");
        setup();
        texture({ "3D/Mechas/Eva-06/textures/e06_201.png" });
    }

    mat4 getModel() override {
        mat4 model = mat4(1.0f);
        model = translate(model, vec3(7.f, 0.f, -3.f));
        model = scale(model, vec3(0.75f));
        return model;
    }
};

// plane class for ground
class Plane {
    GLuint shaderProg = 0;
    GLuint VAO = 0, VBO = 0;

    string readFile(const string& path) {
        fstream f(path);
        if (!f.is_open()) {
            cout << "PLANE: Could not open: " << path << endl;
            return "";
        }
        stringstream buf;
        buf << f.rdbuf();
        return buf.str();
    }

    GLuint compileShader(GLenum type, const char* src) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, NULL);
        glCompileShader(id);

        GLint ok;
        char log[512];
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(id, 512, NULL, log);
            cout << "PLANE SHADER: " << log << endl;
        }
        return id;
    }

public:
    // initialize plane geometry and shader
    void init() {
        // create large quad
        float s = 500.f;
        float verts[] = {
            -s, 0.f, -s,  s, 0.f, -s,  s, 0.f,  s,
            -s, 0.f, -s,  s, 0.f,  s, -s, 0.f,  s,
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        // load shaders
        string vertS = readFile("Shaders/plane.vert");
        string fragS = readFile("Shaders/plane.frag");
        if (vertS.empty() || fragS.empty()) return;

        GLuint v = compileShader(GL_VERTEX_SHADER, vertS.c_str());
        GLuint f = compileShader(GL_FRAGMENT_SHADER, fragS.c_str());

        shaderProg = glCreateProgram();
        glAttachShader(shaderProg, v);
        glAttachShader(shaderProg, f);
        glLinkProgram(shaderProg);

        GLint ok;
        char log[512];
        glGetProgramiv(shaderProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            glGetProgramInfoLog(shaderProg, 512, NULL, log);
            cout << "PLANE LINK: " << log << endl;
        }

        glDeleteShader(v);
        glDeleteShader(f);
    }

    // draw ground plane w grid effect
    void draw(mat4 view, mat4 projection, vec3 camPos, bool nightVision) {
        if (!shaderProg) return;

        glUseProgram(shaderProg);

        mat4 model = mat4(1.f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "model"),
            1, GL_FALSE, value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "view"),
            1, GL_FALSE, value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProg, "projection"),
            1, GL_FALSE, value_ptr(projection));
        glUniform3fv(glGetUniformLocation(shaderProg, "camPos"),
            1, value_ptr(camPos));
        glUniform1i(glGetUniformLocation(shaderProg, "nightVision"),
            nightVision ? 1 : 0);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};

// skybox
class Skybox {
public:
    GLuint skyboxShaderProg;
    GLuint skyboxVAO, skyboxVBO, skyboxEBO;
    GLuint skyboxTex;

    /// Paths to skybox face textures
    string facesSkybox[6]{
        "3D/Skybox/left.png",
        "3D/Skybox/right.png",
        "3D/Skybox/up.png",
        "3D/Skybox/down.png",
        "3D/Skybox/back.png",
        "3D/Skybox/front.png"
    };

    // Cube vertices
    float skyboxVertices[24]{
        -1.f, -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,  1.f, -1.f, -1.f,  1.f, -1.f,
        -1.f, -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };

    // Cube indices
    unsigned int skyboxIndices[36]{
        0, 1, 2,  2, 3, 0,  4, 5, 6,  6, 7, 4,  0, 4, 7,  7, 3, 0,
        1, 5, 6,  6, 2, 1,  0, 1, 5,  5, 4, 0,  3, 2, 6,  6, 7, 3
    };

	// load and compile skybox shaders
    void load() {
        fstream skyboxVertSrc("Shaders/skybox.vert");
        stringstream skyboxVertBuff;
        skyboxVertBuff << skyboxVertSrc.rdbuf();
        string skyboxVertS = skyboxVertBuff.str();
        const char* sky_v = skyboxVertS.c_str();

        fstream skyboxFragSrc("Shaders/skybox.frag");
        stringstream skyboxFragBuff;
        skyboxFragBuff << skyboxFragSrc.rdbuf();
        string sky_fragS = skyboxFragBuff.str();
        const char* sky_f = sky_fragS.c_str();

        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &sky_v, NULL);
        glCompileShader(v);
        {
            GLint ok;
            char log[512];
            glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                glGetShaderInfoLog(v, 512, NULL, log);
                cout << "SKYBOX VERT: " << log << endl;
            }
        }

        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &sky_f, NULL);
        glCompileShader(f);
        {
            GLint ok;
            char log[512];
            glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                glGetShaderInfoLog(f, 512, NULL, log);
                cout << "SKYBOX FRAG: " << log << endl;
            }
        }

        skyboxShaderProg = glCreateProgram();
        glAttachShader(skyboxShaderProg, v);
        glAttachShader(skyboxShaderProg, f);
        glLinkProgram(skyboxShaderProg);
        {
            GLint ok;
            char log[512];
            glGetProgramiv(skyboxShaderProg, GL_LINK_STATUS, &ok);
            if (!ok) {
                glGetProgramInfoLog(skyboxShaderProg, 512, NULL, log);
                cout << "SKYBOX LINK: " << log << endl;
            }
        }

        glDeleteShader(v);
        glDeleteShader(f);
    }

	// Set up cube geometry and buffers
    void setup() {
        glGenVertexArrays(1, &skyboxVAO);
        glGenBuffers(1, &skyboxVBO);
        glGenBuffers(1, &skyboxEBO);
        glBindVertexArray(skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices),
            &skyboxVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyboxEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(skyboxIndices),
            &skyboxIndices, GL_STATIC_DRAW);
    }

    // load cubemap from face images
    void texture() {
        glGenTextures(1, &skyboxTex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        for (unsigned int i = 0; i < 6; i++) {
            int w, h, c;
            stbi_set_flip_vertically_on_load(false);
            unsigned char* data = stbi_load(facesSkybox[i].c_str(), &w, &h, &c, 0);

            if (data) {
                GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format,
                    w, h, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
            else {
                cout << "Failed to load skybox face: " << facesSkybox[i] << endl;
            }
        }
    }

	// draw skybox with depth function change and view matrix
    void draw(mat4 view, mat4 projection) {
        glDepthFunc(GL_LEQUAL);
        glUseProgram(skyboxShaderProg);

        mat4 sky_view = mat4(mat3(view));

        glUniformMatrix4fv(glGetUniformLocation(skyboxShaderProg, "view"),
            1, GL_FALSE, value_ptr(sky_view));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShaderProg, "projection"),
            1, GL_FALSE, value_ptr(projection));
        glUniform1i(glGetUniformLocation(skyboxShaderProg, "skybox"), 0);

        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        glDepthFunc(GL_LESS);
    }
};

// for night vision overlay - globalized bc i was having trouble passing them arnd cleanly lol
GLuint overlayShader = 0;
GLuint quadVAO = 0;
GLuint quadVBO;
GLuint nightTex;

// setup night vision overlay shader
void setupOverlayShader() {
    auto readFile = [](const string& path) -> string {
        fstream f(path);
        if (!f.is_open()) {
            cout << "NIGHTVISION: Could not open shader file: " << path << endl;
            return "";
        }
        stringstream buf;
        buf << f.rdbuf();
        string s = buf.str();
        cout << "NIGHTVISION: Loaded shader (" << s.size() << " bytes): "
            << path << endl;
        return s;
        };

    string vertS = readFile("Shaders/nightvision.vert");
    string fragS = readFile("Shaders/nightvision.frag");
    if (vertS.empty() || fragS.empty()) {
        cout << "NIGHTVISION: Shader source empty!" << endl;
        return;
    }

    const char* vert = vertS.c_str();
    const char* frag = fragS.c_str();

    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vert, NULL);
    glCompileShader(v);
    {
        GLint ok;
        char log[512];
        glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(v, 512, NULL, log);
            cout << "NIGHTVISION VERT ERROR: " << log << endl;
        }
        else {
            cout << "NIGHTVISION VERT: OK" << endl;
        }
    }

    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &frag, NULL);
    glCompileShader(f);
    {
        GLint ok;
        char log[512];
        glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(f, 512, NULL, log);
            cout << "NIGHTVISION FRAG ERROR: " << log << endl;
        }
        else {
            cout << "NIGHTVISION FRAG: OK" << endl;
        }
    }

    overlayShader = glCreateProgram();
    glAttachShader(overlayShader, v);
    glAttachShader(overlayShader, f);
    glLinkProgram(overlayShader);
    {
        GLint ok;
        char log[512];
        glGetProgramiv(overlayShader, GL_LINK_STATUS, &ok);
        if (!ok) {
            glGetProgramInfoLog(overlayShader, 512, NULL, log);
            cout << "NIGHTVISION LINK ERROR: " << log << endl;
        }
        else {
            cout << "NIGHTVISION LINK: OK" << endl;
        }
    }

    glDeleteShader(v);
    glDeleteShader(f);
}


// set up fullscreen quad for overlay rendering 
void setupQuad() {
    float quadVertices[] = {
        -1.f,  1.f,  0.f, 1.f,
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
        -1.f,  1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

// load night vision overlay texture
void loadNightVision() {
    glGenTextures(1, &nightTex);
    glBindTexture(GL_TEXTURE_2D, nightTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    int w, h, c;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load("3D/night-vision.jpg", &w, &h, &c, 0);

    if (data) {
        GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format,
            GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        cout << "NIGHTVISION: Texture loaded OK (" << w << "x" << h << ")" << endl;
    }
    else {
        cout << "NIGHTVISION: Texture FAILED: " << stbi_failure_reason() << endl;
        // fallback to solid green if fialed to load
        unsigned char green[3] = { 0, 200, 0 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB,
            GL_UNSIGNED_BYTE, green);
    }
}

// main func
int main()
{
    // initialize GLFW
    if (!glfwInit()) {
        cout << "Failed to initialize GLFW" << endl;
        return -1;
    }

    // create window
    GLFWwindow* window = glfwCreateWindow(800, 800,
        "Hiraya Buan - Final Project",
        NULL, NULL);
    if (!window) {
        cout << "Failed to create GLFW window" << endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    gladLoadGL();
    glEnable(GL_DEPTH_TEST);

    // set up input
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);

    // initialize cameras
    FirstPersonCamera fpCam;
    ThirdPersonCamera tpCam;
    TopCamera topCam;
    activeCam = &tpCam;

    // initialize lights
    DirectionLight dirLight;   // moonlight
    PointLight pointLight;     // tank headlight

    // Initialize skybox
    Skybox skybox; skybox.load(); skybox.setup(); skybox.texture();

    // initialize Evangelion units
    Eva00 eva00; eva00.init();
    Eva01 eva01; eva01.init();
    Eva02 eva02; eva02.init();
    Eva03 eva03; eva03.init();
    Eva04 eva04; eva04.init();
    Eva06 eva06; eva06.init();

    // initialize tank
    Tank tank; tank.init();

    // initialize ground plane
    Plane plane; plane.init();

    // initialize night vision overlay
    setupOverlayShader();
    setupQuad();
    loadNightVision();

    // delta time tracking
    float lastFrame = 0.f;

    // render loop
    while (!glfwWindowShouldClose(window))
    {
		// calculate delta time
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Clear buffers
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

		// input handling for camera switching
        static bool key1 = false, key2 = false, keyF = false;

        // Camera switching (Key 1: FP <-> TP)
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && !key1) {
            key1 = true;
            activeCam = (activeCam == &tpCam) ? (myCamera*)&fpCam : &tpCam;
			cout << "Switched to " << ((activeCam == &fpCam) ? "First-Person" : "Third-Person") << " Camera" << endl;
        }
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_RELEASE)
            key1 = false;

        // Camera switching (Key 2: Top <-> TP)
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS && !key2) {
            key2 = true;
            activeCam = (activeCam != &topCam) ? (myCamera*)&topCam : &tpCam;
			cout << "Switched to " << ((activeCam == &topCam) ? "Top" : "Third-Person") << " Camera" << endl;
        }
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_RELEASE)
            key2 = false;

        // input handling for point light intensity cycling
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !keyF) {
            keyF = true;
            pointLight.cycleIntensity();
        }
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_RELEASE)
            keyF = false;

        // Night vision is active in first-person mode
        nightvision = (activeCam == &fpCam);

        // update camera and calculate matrices
        activeCam->update(window, deltaTime, tank);
        mat4 view = activeCam->getView();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        float aspect = (float)w / (float)h;
        mat4 projection = activeCam->getProjection(aspect);

        // draw scene        
        tank.draw(view, projection, activeCam->position, dirLight, pointLight);
        plane.draw(view, projection, activeCam->position, nightvision);
        skybox.draw(view, projection);

        if (nightvision || topvision) {
            eva00.draw(view, projection, dirLight, pointLight);
            eva01.draw(view, projection, dirLight, pointLight);
            eva02.draw(view, projection, dirLight, pointLight);
            eva03.draw(view, projection, dirLight, pointLight);
            eva04.draw(view, projection, dirLight, pointLight);
            eva06.draw(view, projection, dirLight, pointLight);
        }

        if (nightvision) {
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glUseProgram(overlayShader);
            glBindVertexArray(quadVAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, nightTex);
            glUniform1i(glGetUniformLocation(overlayShader, "overlayTex"), 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}