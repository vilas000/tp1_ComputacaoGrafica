#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <regex>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

/* =========================================================
   CONFIGURAÇÕES VISUAIS
   ========================================================= */
const float ESPESSURA_MIN = 0.0004f;
const float ESPESSURA_MAX = 0.0040f;

/* =========================================================
   ESTRUTURAS
   ========================================================= */
struct Ponto
{
    glm::vec3 pos;
};

struct Segmento
{
    int a, b;
    float raio;
    float t; // raio normalizado [0,1]
    glm::vec3 cor;
};

struct Arvore3D
{
    std::vector<Ponto> pontos;
    std::vector<Segmento> segmentos;
};

struct Vertice3D
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 cor;
};

/* =========================================================
   VARIÁVEIS GLOBAIS (CÂMERA ORBITAL E ILUMINAÇÃO)
   ========================================================= */
struct CameraOrbital
{
    float distance;
    float azimuth;   // ângulo horizontal em graus
    float elevation; // ângulo vertical em graus
    glm::vec3 target;
};

CameraOrbital camera = {3.0f, 45.0f, 30.0f, glm::vec3(0.0f)};

// Controle de mouse
bool g_mousePressed = false;
double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;

// Parâmetros de iluminação
int g_shadingMode = 2; // 0=Flat, 1=Gouraud, 2=Phong
glm::vec3 g_lightColor(1.0f, 1.0f, 1.0f);
float g_ambientStrength = 0.2f;
float g_specularStrength = 0.5f;
float g_shininess = 32.0f;

// Seleção de segmentos
int g_segmentoSelecionado = -1;           // -1 = nenhum selecionado
glm::vec3 g_corSelecao(1.0f, 1.0f, 0.0f); // Amarelo para destacar

// Árvore atual (para ray casting)
Arvore3D *g_arvoreAtual = nullptr;
std::vector<Vertice3D> *g_geometriaAtual = nullptr;

// Conversão de coordenadas esféricas para cartesianas
glm::vec3 sphericalToCartesian(float distance, float azimuth, float elevation)
{
    float azimuthRad = glm::radians(azimuth);
    float elevationRad = glm::radians(elevation);

    float x = distance * cos(elevationRad) * cos(azimuthRad);
    float y = distance * sin(elevationRad);
    float z = distance * cos(elevationRad) * sin(azimuthRad);

    return glm::vec3(x, y, z);
}

/* =========================================================
   SHADERS COM ILUMINAÇÃO
   ========================================================= */
const char *vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

out vec3 vFragPos;
out vec3 vNormal;
out vec3 vColor;
out vec3 vLightColor; // Para Gouraud

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform int shadingMode; // 0=Flat, 1=Gouraud, 2=Phong
uniform float ambientStrength;
uniform float specularStrength;
uniform float shininess;

vec3 computeLighting(vec3 fragPos, vec3 normal, vec3 baseColor) {
    // Ambient
    vec3 ambient = ambientStrength * lightColor;
    
    // Diffuse
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor;
    
    return (ambient + diffuse + specular) * baseColor;
}

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    
    vFragPos = vec3(worldPos);
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vColor = aColor;
    
    // Gouraud: computar iluminação no vertex shader
    if (shadingMode == 1) {
        vLightColor = computeLighting(vFragPos, normalize(vNormal), vColor);
    }
}
)";

const char *fsSrc = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec3 vColor;
in vec3 vLightColor;

out vec4 FragColor;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform int shadingMode; // 0=Flat, 1=Gouraud, 2=Phong
uniform float ambientStrength;
uniform float specularStrength;
uniform float shininess;

vec3 computeLighting(vec3 fragPos, vec3 normal, vec3 baseColor) {
    // Ambient
    vec3 ambient = ambientStrength * lightColor;
    
    // Diffuse
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor;
    
    return (ambient + diffuse + specular) * baseColor;
}

void main() {
    vec3 finalColor;
    
    if (shadingMode == 0) {
        // Flat: usar normal não interpolada (aproximação sem geometry shader)
        vec3 norm = normalize(vNormal);
        finalColor = computeLighting(vFragPos, norm, vColor);
    } else if (shadingMode == 1) {
        // Gouraud: usar iluminação interpolada do vertex shader
        finalColor = vLightColor;
    } else {
        // Phong: computar iluminação por fragmento
        vec3 norm = normalize(vNormal);
        finalColor = computeLighting(vFragPos, norm, vColor);
    }
    
    FragColor = vec4(finalColor, 1.0);
}
)";

/* =========================================================
   CALLBACK
   ========================================================= */
void framebuffer_size_callback(GLFWwindow *, int w, int h)
{
    glViewport(0, 0, w, h);
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            g_mousePressed = true;
            glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
        }
        else if (action == GLFW_RELEASE)
        {
            g_mousePressed = false;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        // Seleção de segmento com botão direito
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        float asp = (float)w / h;

        glm::mat4 proj = glm::perspective(glm::radians(45.0f), asp, 0.01f, 100.0f);
        glm::vec3 camPos = camera.target + sphericalToCartesian(camera.distance, camera.azimuth, camera.elevation);
        glm::mat4 view = glm::lookAt(camPos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));

        extern int findClickedSegment(double, double, int, int, const glm::mat4 &, const glm::mat4 &);
        int segIdx = findClickedSegment(mouseX, mouseY, w, h, view, proj);

        if (segIdx >= 0 && g_arvoreAtual)
        {
            g_segmentoSelecionado = segIdx;
            const auto &seg = g_arvoreAtual->segmentos[segIdx];

            // Imprimir propriedades
            std::cout << "\n=== SEGMENTO SELECIONADO ===" << std::endl;
            std::cout << "Índice: " << segIdx << std::endl;
            std::cout << "Vértices: " << seg.a << " -> " << seg.b << std::endl;
            std::cout << "Raio: " << seg.raio << std::endl;
            std::cout << "Raio normalizado (t): " << seg.t << std::endl;

            glm::vec3 p1 = g_arvoreAtual->pontos[seg.a].pos;
            glm::vec3 p2 = g_arvoreAtual->pontos[seg.b].pos;
            float comprimento = glm::length(p2 - p1);

            std::cout << "Comprimento: " << comprimento << std::endl;
            std::cout << "P1: (" << p1.x << ", " << p1.y << ", " << p1.z << ")" << std::endl;
            std::cout << "P2: (" << p2.x << ", " << p2.y << ", " << p2.z << ")" << std::endl;
            std::cout << "Cor: (" << seg.cor.r << ", " << seg.cor.g << ", " << seg.cor.b << ")" << std::endl;
            std::cout << "===========================\n"
                      << std::endl;
        }
        else
        {
            g_segmentoSelecionado = -1;
            std::cout << "Nenhum segmento selecionado" << std::endl;
        }
    }
}

void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (g_mousePressed)
    {
        double dx = xpos - g_lastMouseX;
        double dy = ypos - g_lastMouseY;

        // Sensibilidade: 0.5 graus por pixel
        camera.azimuth += dx * 0.5f;
        camera.elevation -= dy * 0.5f;

        // Limitar elevation para evitar gimbal lock
        camera.elevation = glm::clamp(camera.elevation, -89.0f, 89.0f);

        g_lastMouseX = xpos;
        g_lastMouseY = ypos;
    }
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    camera.distance *= (yoffset > 0) ? 0.9f : 1.1f;
    camera.distance = glm::clamp(camera.distance, 0.5f, 20.0f);
}

/* =========================================================
   RAY CASTING PARA SELEÇÃO DE SEGMENTOS
   ========================================================= */

// Estrutura de Ray
struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;
};

// Gera ray a partir de coordenadas de tela
Ray getRayFromScreen(double mouseX, double mouseY, int screenWidth, int screenHeight,
                     const glm::mat4 &view, const glm::mat4 &projection)
{
    // Normalizar coordenadas de tela para NDC [-1, 1]
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;

    // Ray em clip space
    glm::vec4 rayClip(x, y, -1.0f, 1.0f);

    // Ray em eye space
    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    // Ray em world space
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    rayWorld = glm::normalize(rayWorld);

    // Posição da câmera em world space
    glm::vec3 camPos = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

    Ray ray;
    ray.origin = camPos;
    ray.direction = rayWorld;
    return ray;
}

// Testa interseção ray-cilindro (simplificado: ray-segmento com threshold)
bool rayCylinderIntersection(const Ray &ray, const glm::vec3 &p1, const glm::vec3 &p2,
                             float radius, float &t, bool debug = false)
{
    // Algoritmo simplificado: distância do ray ao segmento
    glm::vec3 segDir = p2 - p1;
    float segLength = glm::length(segDir);
    if (segLength < 1e-6f)
        return false;
    segDir /= segLength;

    glm::vec3 rayToSeg = p1 - ray.origin;

    // Parâmetros para calcular distância mínima
    float a = glm::dot(ray.direction, ray.direction);
    float b = glm::dot(ray.direction, segDir);
    float c = glm::dot(segDir, segDir);
    float d = glm::dot(ray.direction, rayToSeg);
    float e = glm::dot(segDir, rayToSeg);

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-6f)
        return false;

    // FIX: Correção do sinal para s e tSeg
    float s = (c * d - b * e) / denom;    // Estava: (b * e - c * d) / denom
    float tSeg = (b * d - a * e) / denom; // Estava: (a * e - b * d) / denom
    {
        t = s;
        return true;
    }

    return false;
}

// Encontra segmento clicado
int findClickedSegment(double mouseX, double mouseY, int screenWidth, int screenHeight,
                       const glm::mat4 &view, const glm::mat4 &projection)
{
    if (!g_arvoreAtual || !g_geometriaAtual)
        return -1;

    Ray ray = getRayFromScreen(mouseX, mouseY, screenWidth, screenHeight, view, projection);

    float closestT = FLT_MAX;
    int closestSegment = -1;

    // Normalizar pontos (mesmo processo do buildGeometry)
    glm::vec3 minP(FLT_MAX), maxP(-FLT_MAX);
    for (const auto &p : g_arvoreAtual->pontos)
    {
        minP = glm::min(minP, p.pos);
        maxP = glm::max(maxP, p.pos);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 range = maxP - minP;
    float scale = glm::max(glm::max(range.x, range.y), range.z);
    if (scale == 0.0f)
        scale = 1.0f;

    int nPts = (int)g_arvoreAtual->pontos.size();
    std::vector<glm::vec3> P(nPts);
    for (int i = 0; i < nPts; ++i)
    {
        P[i] = (g_arvoreAtual->pontos[i].pos - center) / scale;
    }

    // Calcular raios
    std::vector<float> pontoTSum(nPts, 0.0f);
    std::vector<int> pontoTCount(nPts, 0);
    for (const auto &s : g_arvoreAtual->segmentos)
    {
        pontoTSum[s.a] += s.t;
        pontoTSum[s.b] += s.t;
        pontoTCount[s.a]++;
        pontoTCount[s.b]++;
    }
    std::vector<float> pontoRaio(nPts, 0.0f);
    for (int i = 0; i < nPts; ++i)
    {
        float tVal = (pontoTCount[i] > 0) ? pontoTSum[i] / pontoTCount[i] : 0.5f;
        pontoRaio[i] = glm::mix(ESPESSURA_MIN, ESPESSURA_MAX, tVal);
    }

    // Testar cada segmento
    for (size_t i = 0; i < g_arvoreAtual->segmentos.size(); ++i)
    {
        const auto &seg = g_arvoreAtual->segmentos[i];
        float avgRadius = (pontoRaio[seg.a] + pontoRaio[seg.b]) * 0.5f;

        float tVal;
        if (rayCylinderIntersection(ray, P[seg.a], P[seg.b], avgRadius, tVal))
        {
            if (tVal < closestT)
            {
                closestT = tVal;
                closestSegment = i;
            }
        }
    }

    return closestSegment;
}

/* =========================================================
   GRADIENTE DE COR
   ========================================================= */
// Converte HSV (h em [0,1]) para RGB
static glm::vec3 hsv2rgb(float h, float s, float v)
{
    h = fmod(h, 1.0f);
    if (h < 0.0f)
        h += 1.0f;
    float c = v * s;
    float x = c * (1.0f - std::fabs(fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 1.0f / 6.0f)
    {
        r = c;
        g = x;
        b = 0;
    }
    else if (h < 2.0f / 6.0f)
    {
        r = x;
        g = c;
        b = 0;
    }
    else if (h < 3.0f / 6.0f)
    {
        r = 0;
        g = c;
        b = x;
    }
    else if (h < 4.0f / 6.0f)
    {
        r = 0;
        g = x;
        b = c;
    }
    else if (h < 5.0f / 6.0f)
    {
        r = x;
        g = 0;
        b = c;
    }
    else
    {
        r = c;
        g = 0;
        b = x;
    }
    return glm::vec3(r + m, g + m, b + m);
}

glm::vec3 gradiente(float t)
{
    t = glm::clamp(t, 0.0f, 1.0f);
    // Queremos: t=1.0 -> vermelho (hue=0.0), t=0.0 -> violeta/azul (~270deg -> hue=0.75)
    float hueViolet = 0.75f; // ~270deg
    float hueRed = 0.0f;     // 0deg
    float hue = glm::mix(hueViolet, hueRed, t);
    return hsv2rgb(hue, 1.0f, 1.0f);
}

// Globals to control step reloading
static int g_currentStep = 0;
static int g_nDim = 2;
static int g_tamanhoArvore = 256;
static bool g_reloadRequested = false;
static int g_requestedStep = 0;
static int g_maxStep = 0;

// Constrói o caminho do arquivo VTK baseado em dimensão, tamanho e step
static std::string buildVTKPath(int nDim, int tamanhoArvore, int step)
{
    char caminhoBuilder[128] = "";
    char aux[16];

    if (nDim == 2)
    {
        strcat(caminhoBuilder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_");
        sprintf(aux, "%03d", tamanhoArvore);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, "/tree2D_Nterm");
        sprintf(aux, "%04d", tamanhoArvore);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, "_step");
        sprintf(aux, "%04d", step);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, ".vtk");
        return std::string(caminhoBuilder);
    }
    else if (nDim == 3)
    {
        strcat(caminhoBuilder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP2_3D/Nterm_");
        sprintf(aux, "%03d", tamanhoArvore);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, "/tree3D_Nterm");
        sprintf(aux, "%04d", tamanhoArvore);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, "_step");
        sprintf(aux, "%04d", step);
        strcat(caminhoBuilder, aux);
        strcat(caminhoBuilder, ".vtk");
        return std::string(caminhoBuilder);
    }
    // fallback
    return std::string("../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP2_3D/Nterm_128/tree3D_Nterm0128_step0064.vtk");
}

// Detecta o maior step disponível na pasta correspondente
static int detectMaxStep(int nDim, int tamanhoArvore)
{
    char folder[128] = "";
    char aux[16];
    std::regex re;

    if (nDim == 2)
    {
        strcat(folder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_");
        sprintf(aux, "%03d", tamanhoArvore);
        strcat(folder, aux);
        re = std::regex(R"(tree2D_Nterm\d+_step(\d+)\.vtk)");
    }
    else if (nDim == 3)
    {
        strcat(folder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP2_3D/Nterm_");
        sprintf(aux, "%03d", tamanhoArvore);
        strcat(folder, aux);
        re = std::regex(R"(tree3D_Nterm\d+_step(\d+)\.vtk)");
    }
    else
    {
        return 0;
    }

    int maxStep = 0;
    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(folder))
        {
            if (!entry.is_regular_file())
                continue;
            std::smatch m;
            std::string name = entry.path().filename().string();
            if (std::regex_search(name, m, re))
            {
                int s = std::stoi(m[1].str());
                if (s > maxStep)
                    maxStep = s;
            }
        }
    }
    catch (...)
    {
        return 0;
    }
    return maxStep;
}

// Key callback: j = previous step, k = next step
static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS)
        return;
    auto stepDelta = [](int tamanho)
    {
        if (tamanho == 64)
            return 8;
        if (tamanho == 128)
            return 16;
        return 32; // default for 256 and others
    };

    int delta = stepDelta(g_tamanhoArvore);

    if (key == GLFW_KEY_J)
    {
        int next = g_currentStep - delta;
        if (next < delta)
            next = delta; // menor step válido
        if (g_maxStep > 0 && next > g_maxStep)
            next = g_maxStep;
        g_requestedStep = next;
        g_reloadRequested = true;
    }
    else if (key == GLFW_KEY_K)
    {
        int next = g_currentStep + delta;
        if (g_maxStep > 0 && next > g_maxStep)
            next = g_maxStep;
        g_requestedStep = next;
        g_reloadRequested = true;
    }
}

/* =========================================================
   GERAÇÃO DE GEOMETRIA DE CILINDRO
   ========================================================= */

// Gera geometria para um cilindro entre dois pontos com tampas
void gerarCilindro(const glm::vec3 &p1, const glm::vec3 &p2,
                   float raio1, float raio2,
                   const glm::vec3 &cor1, const glm::vec3 &cor2,
                   std::vector<Vertice3D> &vertices,
                   int numLados = 12)
{

    glm::vec3 axis = p2 - p1;
    float height = glm::length(axis);
    if (height < 1e-6f)
        return; // segmento degenerado

    axis = glm::normalize(axis);

    // Encontrar dois vetores perpendiculares ao eixo
    glm::vec3 up = (std::abs(axis.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, axis));
    glm::vec3 forward = glm::cross(axis, right);

    // Gerar círculos nas extremidades
    std::vector<glm::vec3> circle1(numLados), circle2(numLados);
    std::vector<glm::vec3> normals1(numLados), normals2(numLados);

    for (int i = 0; i < numLados; ++i)
    {
        float angle = 2.0f * M_PI * i / numLados;
        float c = std::cos(angle);
        float s = std::sin(angle);

        glm::vec3 radialDir = c * right + s * forward;

        circle1[i] = p1 + radialDir * raio1;
        circle2[i] = p2 + radialDir * raio2;

        normals1[i] = glm::normalize(radialDir);
        normals2[i] = glm::normalize(radialDir);
    }

    // === LATERAIS DO CILINDRO (triangle strip) ===
    for (int i = 0; i < numLados; ++i)
    {
        int next = (i + 1) % numLados;

        // Triângulo 1
        vertices.push_back({circle1[i], normals1[i], cor1});
        vertices.push_back({circle2[i], normals2[i], cor2});
        vertices.push_back({circle1[next], normals1[next], cor1});

        // Triângulo 2
        vertices.push_back({circle1[next], normals1[next], cor1});
        vertices.push_back({circle2[i], normals2[i], cor2});
        vertices.push_back({circle2[next], normals2[next], cor2});
    }

    // === TAMPA INFERIOR (p1) ===
    glm::vec3 normal1 = -axis; // aponta para fora
    for (int i = 1; i < numLados - 1; ++i)
    {
        vertices.push_back({p1, normal1, cor1});
        vertices.push_back({circle1[i], normal1, cor1});
        vertices.push_back({circle1[i + 1], normal1, cor1});
    }

    // === TAMPA SUPERIOR (p2) ===
    glm::vec3 normal2 = axis; // aponta para fora
    for (int i = 1; i < numLados - 1; ++i)
    {
        vertices.push_back({p2, normal2, cor2});
        vertices.push_back({circle2[i + 1], normal2, cor2});
        vertices.push_back({circle2[i], normal2, cor2});
    }
}

/* =========================================================
   PARSER VTK (POINTS + LINES + SCALARS)
   ========================================================= */
Arvore3D carregarVTK(const std::string &path)
{
    Arvore3D A;
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "Erro ao abrir " << path << std::endl;
        return A;
    }

    std::string word;
    int cellDataCount = 0;

    while (f >> word)
    {
        if (word == "POINTS")
        {
            int n;
            std::string type;
            f >> n >> type;
            A.pontos.resize(n);
            for (int i = 0; i < n; ++i)
                f >> A.pontos[i].pos.x >> A.pontos[i].pos.y >> A.pontos[i].pos.z;
        }
        else if (word == "LINES")
        {
            int n, total;
            f >> n >> total;
            for (int i = 0; i < n; ++i)
            {
                int k;
                f >> k;
                int prev;
                f >> prev;
                for (int j = 1; j < k; ++j)
                {
                    int cur;
                    f >> cur;
                    A.segmentos.push_back({prev, cur, 0.0f, 0.0f, {}});
                    prev = cur;
                }
            }
        }
        else if (word == "CELL_DATA")
        {
            f >> cellDataCount;
        }
        else if (word == "SCALARS" || word == "scalars")
        {
            std::string name, type, next;
            f >> name >> type >> next;
            if (next != "LOOKUP_TABLE")
            {
                std::string dummy;
                f >> dummy;
            }
            std::string table;
            f >> table;

            int n = std::min((int)A.segmentos.size(), cellDataCount);
            float rMin = FLT_MAX, rMax = -FLT_MAX;

            for (int i = 0; i < n; ++i)
            {
                f >> A.segmentos[i].raio;
                rMin = std::min(rMin, A.segmentos[i].raio);
                rMax = std::max(rMax, A.segmentos[i].raio);
            }

            for (auto &s : A.segmentos)
            {
                float t = (rMax > rMin)
                              ? (s.raio - rMin) / (rMax - rMin)
                              : 0.5f;
                s.t = t;
                s.cor = gradiente(t);
            }
        }
    }
    return A;
}

/* =========================================================
   SHADER SETUP
   ========================================================= */
GLuint criarShader()
{
    auto compile = [](GLenum type, const char *src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

/* =========================================================
   INPUT
   ========================================================= */
static float lastModeSwitch = 0.0f;

void processInput(GLFWwindow *w)
{
    if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(w, true);

    float pan = 0.05f;

    // Pan do target da câmera com as setas
    glm::vec3 right = glm::normalize(glm::cross(
        sphericalToCartesian(1.0f, camera.azimuth, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 forward = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), right));

    if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS)
        camera.target += forward * pan;
    if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS)
        camera.target -= forward * pan;
    if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS)
        camera.target -= right * pan;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS)
        camera.target += right * pan;

    // Zoom alternativo (Q/E além do scroll)
    if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS)
    {
        camera.distance *= 0.98f;
        camera.distance = glm::clamp(camera.distance, 0.5f, 20.0f);
    }
    if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS)
    {
        camera.distance *= 1.02f;
        camera.distance = glm::clamp(camera.distance, 0.5f, 20.0f);
    }

    // Rotação manual (R/T)
    if (glfwGetKey(w, GLFW_KEY_R) == GLFW_PRESS)
        camera.azimuth += 1.0f;
    if (glfwGetKey(w, GLFW_KEY_T) == GLFW_PRESS)
        camera.azimuth -= 1.0f;

    // Alternar modos de iluminação (com debounce)
    float currentTime = glfwGetTime();
    if (currentTime - lastModeSwitch > 0.2f)
    {
        if (glfwGetKey(w, GLFW_KEY_1) == GLFW_PRESS)
        {
            g_shadingMode = 0; // Flat
            lastModeSwitch = currentTime;
            std::cout << "Modo alterado para: FLAT" << std::endl;
            const char *modes[] = {"Flat", "Gouraud", "Phong"};
            std::string title = std::string("CCO Viewer - ") + modes[g_shadingMode];
            glfwSetWindowTitle(w, title.c_str());
        }
        if (glfwGetKey(w, GLFW_KEY_2) == GLFW_PRESS)
        {
            g_shadingMode = 1; // Gouraud
            lastModeSwitch = currentTime;
            std::cout << "Modo alterado para: GOURAUD" << std::endl;
            const char *modes[] = {"Flat", "Gouraud", "Phong"};
            std::string title = std::string("CCO Viewer - ") + modes[g_shadingMode];
            glfwSetWindowTitle(w, title.c_str());
        }
        if (glfwGetKey(w, GLFW_KEY_3) == GLFW_PRESS)
        {
            g_shadingMode = 2; // Phong
            lastModeSwitch = currentTime;
            std::cout << "Modo alterado para: PHONG" << std::endl;
            const char *modes[] = {"Flat", "Gouraud", "Phong"};
            std::string title = std::string("CCO Viewer - ") + modes[g_shadingMode];
            glfwSetWindowTitle(w, title.c_str());
        }
    }
}

/* =========================================================
   MAIN
   ========================================================= */
int main(int argc, char **argv)
{
    std::string arquivo;
    if (argc > 1)
    {
        int tamanhoArvore = atoi(argv[2]);
        int step = atoi(argv[3]);
        char caminhoBuilder[90] = "";
        char aux[10];

        switch (atoi(argv[1]))
        {
        case 2:
            std::cout << "Tentando carregar árvore 2D de " << tamanhoArvore << " termos, no step " << step << std::endl;
            strcat(caminhoBuilder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_");
            if (tamanhoArvore != 64 && tamanhoArvore != 128 && tamanhoArvore != 256)
            {
                std::cout << "Tamanho de árvore inválido, tente algum desses valores: [64, 128, 256]" << std::endl;
                return 1;
            }
            sprintf(aux, "%03d", tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "/tree2D_Nterm");
            sprintf(aux, "%04d", tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "_step");
            sprintf(aux, "%04d", step);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, ".vtk");

            arquivo = caminhoBuilder;
            break;

        case 3:
            std::cout << "Tentando carregar árvore 3D de " << tamanhoArvore << " termos, no step " << step << std::endl;
            strcat(caminhoBuilder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP2_3D/Nterm_");
            if (tamanhoArvore != 128 && tamanhoArvore != 256 && tamanhoArvore != 512)
            {
                std::cout << "Tamanho de árvore inválido, tente algum desses valores: [128, 256, 512]" << std::endl;
                return 1;
            }
            sprintf(aux, "%03d", tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "/tree3D_Nterm");
            sprintf(aux, "%04d", tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "_step");
            sprintf(aux, "%04d", step);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, ".vtk");

            arquivo = caminhoBuilder;
            break;

        default:
            std::cout << "Opção inválida de dimensões, tente '2' para 2D ou '3' para 3D" << std::endl;
            return 1;
            break;
        }
    }
    else
    {
        std::cout << "Uso: ./meu_app <nDimensoes> <Nterm> <step>" << std::endl;
        std::cout << "Carregando arquivo 3D padrao..." << std::endl;
        // Caminho padrão (fallback) - 3D
        arquivo = "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP2_3D/Nterm_128/tree3D_Nterm0128_step0064.vtk";
    }

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *win = glfwCreateWindow(1000, 800, "CCO Viewer - Phong", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);
    // register key callback for j/k stepping
    glfwSetKeyCallback(win, key_callback);
    // register mouse callbacks for orbital camera
    glfwSetMouseButtonCallback(win, mouse_button_callback);
    glfwSetCursorPosCallback(win, cursor_pos_callback);
    glfwSetScrollCallback(win, scroll_callback);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // Configurações 3D
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    Arvore3D A = carregarVTK(arquivo);
    if (A.pontos.empty())
        return -1;

    // Configurar ponteiros globais para ray casting
    g_arvoreAtual = &A;

    // inicializa variáveis globais para navegação por step se argumentos forem fornecidos na execução
    if (argc > 1)
    {
        g_nDim = atoi(argv[1]);
        g_tamanhoArvore = atoi(argv[2]);
        g_currentStep = atoi(argv[3]);
    }
    // Detecta o step máximo disponível para não exceder na navegacao
    g_maxStep = detectMaxStep(g_nDim, g_tamanhoArvore);

    // ==== GERAÇÃO DE GEOMETRIA 3D COM CILINDROS ====

    // Função auxiliar para construir geometria
    auto buildGeometry = [](const Arvore3D &arvore) -> std::vector<Vertice3D>
    {
        std::vector<Vertice3D> vertices;

        // Normalizar/centralizar modelo em 3D
        glm::vec3 minP(FLT_MAX), maxP(-FLT_MAX);
        for (const auto &p : arvore.pontos)
        {
            minP = glm::min(minP, p.pos);
            maxP = glm::max(maxP, p.pos);
        }
        glm::vec3 center = (minP + maxP) * 0.5f;
        glm::vec3 range = maxP - minP;
        float scale = glm::max(glm::max(range.x, range.y), range.z);
        if (scale == 0.0f)
            scale = 1.0f;

        // Normalizar posições
        int nPts = (int)arvore.pontos.size();
        std::vector<glm::vec3> P(nPts);
        for (int i = 0; i < nPts; ++i)
        {
            P[i] = (arvore.pontos[i].pos - center) / scale;
        }

        // Calcular t e raios por vértice (média dos segmentos incidentes)
        std::vector<float> pontoTSum(nPts, 0.0f);
        std::vector<int> pontoTCount(nPts, 0);
        for (const auto &s : arvore.segmentos)
        {
            pontoTSum[s.a] += s.t;
            pontoTSum[s.b] += s.t;
            pontoTCount[s.a]++;
            pontoTCount[s.b]++;
        }
        std::vector<float> pontoT(nPts, 0.5f);
        std::vector<float> pontoRaio(nPts, 0.0f);
        for (int i = 0; i < nPts; ++i)
        {
            if (pontoTCount[i] > 0)
                pontoT[i] = pontoTSum[i] / pontoTCount[i];
            else
                pontoT[i] = 0.5f;
            pontoRaio[i] = glm::mix(ESPESSURA_MIN, ESPESSURA_MAX, pontoT[i]);
        }

        // Cores por vértice
        std::vector<glm::vec3> corPt(nPts);
        for (int i = 0; i < nPts; ++i)
            corPt[i] = gradiente(pontoT[i]);

        // Gerar cilindros para cada segmento
        for (const auto &s : arvore.segmentos)
        {
            gerarCilindro(P[s.a], P[s.b],
                          pontoRaio[s.a], pontoRaio[s.b],
                          corPt[s.a], corPt[s.b],
                          vertices, 12);
        }

        return vertices;
    };

    std::vector<Vertice3D> geometria = buildGeometry(A);

    // Configurar ponteiro global para ray casting
    g_geometriaAtual = &geometria;

    // Converter para VBO flat (9 floats por vértice)
    std::vector<float> vboData;
    vboData.reserve(geometria.size() * 9);
    for (const auto &v : geometria)
    {
        vboData.push_back(v.pos.x);
        vboData.push_back(v.pos.y);
        vboData.push_back(v.pos.z);
        vboData.push_back(v.normal.x);
        vboData.push_back(v.normal.y);
        vboData.push_back(v.normal.z);
        vboData.push_back(v.cor.r);
        vboData.push_back(v.cor.g);
        vboData.push_back(v.cor.b);
    }

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float),
                 vboData.data(), GL_STATIC_DRAW);

    // Layout: pos(3) + normal(3) + color(3) = 9 floats, stride = 36 bytes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    GLuint prog = criarShader();
    GLint locModel = glGetUniformLocation(prog, "model");
    GLint locView = glGetUniformLocation(prog, "view");
    GLint locProj = glGetUniformLocation(prog, "projection");
    GLint locLightPos = glGetUniformLocation(prog, "lightPos");
    GLint locLightColor = glGetUniformLocation(prog, "lightColor");
    GLint locViewPos = glGetUniformLocation(prog, "viewPos");
    GLint locShadingMode = glGetUniformLocation(prog, "shadingMode");
    GLint locAmbient = glGetUniformLocation(prog, "ambientStrength");
    GLint locSpecular = glGetUniformLocation(prog, "specularStrength");
    GLint locShininess = glGetUniformLocation(prog, "shininess");

    while (!glfwWindowShouldClose(win))
    {
        processInput(win);

        // se uma recarga foi solicitada pela callback de teclado
        if (g_reloadRequested)
        {
            g_reloadRequested = false;
            int newStep = g_requestedStep;
            std::string novoArquivo = buildVTKPath(g_nDim, g_tamanhoArvore, newStep);
            Arvore3D An = carregarVTK(novoArquivo);
            if (!An.pontos.empty())
            {
                A = std::move(An);
                g_currentStep = newStep;
                std::cout << "Exibindo step: " << g_currentStep << std::endl;

                // Reconstruir geometria
                std::vector<Vertice3D> novaGeometria = buildGeometry(A);

                // Converter para VBO flat
                std::vector<float> newVBO;
                newVBO.reserve(novaGeometria.size() * 9);
                for (const auto &v : novaGeometria)
                {
                    newVBO.insert(newVBO.end(), {v.pos.x, v.pos.y, v.pos.z,
                                                 v.normal.x, v.normal.y, v.normal.z,
                                                 v.cor.r, v.cor.g, v.cor.b});
                }

                // Atualizar VBO OpenGL
                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                glBufferData(GL_ARRAY_BUFFER, newVBO.size() * sizeof(float), newVBO.data(), GL_STATIC_DRAW);
                vboData = std::move(newVBO);
                geometria = std::move(novaGeometria);

                // Atualizar ponteiro global para ray casting
                g_geometriaAtual = &geometria;
            }
            else
            {
                std::cerr << "Falha ao carregar step " << newStep << std::endl;
            }
        }

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        float asp = (float)w / h;

        // Projeção perspectiva
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), asp, 0.01f, 100.0f);

        // Câmera orbital
        glm::vec3 camPos = camera.target + sphericalToCartesian(camera.distance, camera.azimuth, camera.elevation);
        glm::mat4 view = glm::lookAt(camPos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));

        // Matriz model (identidade por enquanto)
        glm::mat4 model = glm::mat4(1.0f);

        // Luz acompanha a câmera
        glm::vec3 lightPos = camPos;

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(prog);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(locLightPos, 1, glm::value_ptr(lightPos));
        glUniform3fv(locLightColor, 1, glm::value_ptr(g_lightColor));
        glUniform3fv(locViewPos, 1, glm::value_ptr(camPos));
        glUniform1i(locShadingMode, g_shadingMode);
        glUniform1f(locAmbient, g_ambientStrength);
        glUniform1f(locSpecular, g_specularStrength);
        glUniform1f(locShininess, g_shininess);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, geometria.size());

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
