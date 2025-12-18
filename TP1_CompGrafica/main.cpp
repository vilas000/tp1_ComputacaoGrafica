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
struct Ponto {
    glm::vec3 pos;
};

struct Segmento {
    int a, b;
    float raio;
    float t;          // raio normalizado [0,1]
    glm::vec3 cor;
};

struct Arvore2D {
    std::vector<Ponto> pontos;
    std::vector<Segmento> segmentos;
};

/* =========================================================
   VARIÁVEIS GLOBAIS (CÂMERA)
   ========================================================= */
glm::vec2 cameraPos(0.0f);
float zoom = 1.0f;
float rotacao = 0.0f;

/* =========================================================
   SHADERS
   ========================================================= */
const char* vsSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
uniform mat4 MVP;
void main() {
    gl_Position = MVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* fsSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

/* =========================================================
   CALLBACK
   ========================================================= */
void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

/* =========================================================
   GRADIENTE DE COR
   ========================================================= */
// Converte HSV (h em [0,1]) para RGB
static glm::vec3 hsv2rgb(float h, float s, float v) {
    h = fmod(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    float c = v * s;
    float x = c * (1.0f - std::fabs(fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 1.0f/6.0f) { r = c; g = x; b = 0; }
    else if (h < 2.0f/6.0f) { r = x; g = c; b = 0; }
    else if (h < 3.0f/6.0f) { r = 0; g = c; b = x; }
    else if (h < 4.0f/6.0f) { r = 0; g = x; b = c; }
    else if (h < 5.0f/6.0f) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return glm::vec3(r + m, g + m, b + m);
}

glm::vec3 gradiente(float t) {
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
static std::string buildVTKPath(int nDim, int tamanhoArvore, int step) {
    if (nDim == 2) {
        char caminhoBuilder[128] = "";
        char aux[16];
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
    // fallback
    return std::string("../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_256/tree2D_Nterm0256_step0224.vtk");
}

// Detecta o maior step disponível na pasta correspondente
static int detectMaxStep(int nDim, int tamanhoArvore) {
    if (nDim != 2) return 0;
    char folder[128] = "";
    char aux[16];
    strcat(folder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_");
    sprintf(aux, "%03d", tamanhoArvore);
    strcat(folder, aux);

    std::regex re(R"(tree2D_Nterm\d+_step(\d+)\.vtk)");
    int maxStep = 0;
    try {
        for (const auto &entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            std::smatch m;
            std::string name = entry.path().filename().string();
            if (std::regex_search(name, m, re)) {
                int s = std::stoi(m[1].str());
                if (s > maxStep) maxStep = s;
            }
        }
    } catch (...) {
        return 0;
    }
    return maxStep;
}

// Key callback: j = previous step, k = next step
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    auto stepDelta = [](int tamanho) {
        if (tamanho == 64) return 8;
        if (tamanho == 128) return 16;
        return 32; // default for 256 and others
    };

    int delta = stepDelta(g_tamanhoArvore);

    if (key == GLFW_KEY_J) {
        int next = g_currentStep - delta;
        if (next < delta) next = delta; // menor step válido
        if (g_maxStep > 0 && next > g_maxStep) next = g_maxStep;
        g_requestedStep = next;
        g_reloadRequested = true;
    } else if (key == GLFW_KEY_K) {
        int next = g_currentStep + delta;
        if (g_maxStep > 0 && next > g_maxStep) next = g_maxStep;
        g_requestedStep = next;
        g_reloadRequested = true;
    }
}

/* =========================================================
   PARSER VTK (POINTS + LINES + SCALARS)
   ========================================================= */
Arvore2D carregarVTK(const std::string& path) {
    Arvore2D A;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Erro ao abrir " << path << std::endl;
        return A;
    }

    std::string word;
    int cellDataCount = 0;

    while (f >> word) {
        if (word == "POINTS") {
            int n; std::string type;
            f >> n >> type;
            A.pontos.resize(n);
            for (int i = 0; i < n; ++i)
                f >> A.pontos[i].pos.x >> A.pontos[i].pos.y >> A.pontos[i].pos.z;
        }
        else if (word == "LINES") {
            int n, total;
            f >> n >> total;
            for (int i = 0; i < n; ++i) {
                int k; f >> k;
                int prev; f >> prev;
                for (int j = 1; j < k; ++j) {
                    int cur; f >> cur;
                    A.segmentos.push_back({prev, cur, 0.0f, 0.0f, {}});
                    prev = cur;
                }
            }
        }
        else if (word == "CELL_DATA") {
            f >> cellDataCount;
        }
        else if (word == "SCALARS" || word == "scalars") {
            std::string name, type, next;
            f >> name >> type >> next;
            if (next != "LOOKUP_TABLE") {
                std::string dummy;
                f >> dummy;
            }
            std::string table;
            f >> table;

            int n = std::min((int)A.segmentos.size(), cellDataCount);
            float rMin = FLT_MAX, rMax = -FLT_MAX;

            for (int i = 0; i < n; ++i) {
                f >> A.segmentos[i].raio;
                rMin = std::min(rMin, A.segmentos[i].raio);
                rMax = std::max(rMax, A.segmentos[i].raio);
            }

            for (auto& s : A.segmentos) {
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
GLuint criarShader() {
    auto compile = [](GLenum type, const char* src) {
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
void processInput(GLFWwindow* w) {
    if (glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(w, true);

    float pan = 0.02f / zoom;

    if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS)    cameraPos.y -= pan;
    if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS)  cameraPos.y += pan;
    if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS)  cameraPos.x += pan;
    if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) cameraPos.x -= pan;

    if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) zoom *= 1.02f;
    if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) zoom *= 0.98f;

    if (glfwGetKey(w, GLFW_KEY_R) == GLFW_PRESS) rotacao += 1.0f;
    if (glfwGetKey(w, GLFW_KEY_T) == GLFW_PRESS) rotacao -= 1.0f;
}

/* =========================================================
   MAIN
   ========================================================= */
int main(int argc, char** argv) {
    std::string arquivo;
    if (argc > 1) {
        int tamanhoArvore = atoi(argv[2]);
        int step = atoi(argv[3]);
        char caminhoBuilder[90] = "";
        char aux[10];
 
        switch (atoi(argv[1]))
        {
        case 2:
            std::cout << "Tentando carregar árvore 2D de " << tamanhoArvore << " termos, no step " << step << std::endl;
            strcat(caminhoBuilder, "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_");
            if (tamanhoArvore != 64 && tamanhoArvore != 128 && tamanhoArvore != 256) {
                std::cout << "Tamanho de árvore inválido, tente algum desses valores: [64, 128, 256]" << std::endl;
                return 1;
            }
            sprintf(aux,"%03d",tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "/tree2D_Nterm");
            sprintf(aux,"%04d",tamanhoArvore);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, "_step");
            sprintf(aux,"%04d", step);
            strcat(caminhoBuilder, aux);
            strcat(caminhoBuilder, ".vtk");
 
            arquivo = caminhoBuilder;
            break;
 
        case 3:
            std::cout << "3D ainda não implementado" << std::endl;
            std::cout << "Carregando arquivo padrao..." << std::endl;
            arquivo = "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_256/tree2D_Nterm0256_step0224.vtk";
            break;
 
        default:
            std::cout << "Opção inválida de dimensões, tente '2' para 2D ou '3' para 3D" << std::endl;
            return 1;
            break;
        }
    }
    else {
        std::cout << "Uso: ./meu_app <nDimensoes> <Nterm> <step>" << std::endl;
        std::cout << "Carregando arquivo padrao..." << std::endl;
        // Caminho padrão (fallback)
        arquivo = "../TP_CCO_Pacote_Dados/TP_CCO_Pacote_Dados/TP1_2D/Nterm_256/tree2D_Nterm0256_step0224.vtk"; // Ajuste se necessário
    }

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1000, 800, "CCO Viewer", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);
    // register key callback for j/k stepping
    glfwSetKeyCallback(win, key_callback);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    Arvore2D A = carregarVTK(arquivo);
    if (A.pontos.empty()) return -1;

    // inicializa variáveis globais para navegação por step se argumentos forem fornecidos na execução
    if (argc > 1) {
        g_nDim = atoi(argv[1]);
        g_tamanhoArvore = atoi(argv[2]);
        g_currentStep = atoi(argv[3]);
    }
    // Detecta o step máximo disponível para não exceder na navegacao
    g_maxStep = detectMaxStep(g_nDim, g_tamanhoArvore);

    std::vector<float> vboData;

    // Normalizar/centralizar modelo para evitar espessuras visuais desproporcionais
    glm::vec2 minP(FLT_MAX), maxP(-FLT_MAX);
    for (const auto &p : A.pontos) {
        minP.x = std::min(minP.x, p.pos.x);
        minP.y = std::min(minP.y, p.pos.y);
        maxP.x = std::max(maxP.x, p.pos.x);
        maxP.y = std::max(maxP.y, p.pos.y);
    }
    glm::vec2 center = (minP + maxP) * 0.5f;
    float rangeX = maxP.x - minP.x;
    float rangeY = maxP.y - minP.y;
    float scale = std::max(rangeX, rangeY);
    if (scale == 0.0f) scale = 1.0f;

    // Precompute normalized vertex positions
    int nPts = (int)A.pontos.size();
    std::vector<glm::vec3> P(nPts);
    for (int i = 0; i < nPts; ++i) {
        P[i] = A.pontos[i].pos;
        P[i].x = (P[i].x - center.x) / scale;
        P[i].y = (P[i].y - center.y) / scale;
    }

    // Calcular t por vértice como média dos segmentos incidentes (suaviza transições)
    std::vector<float> pontoTSum(nPts, 0.0f);
    std::vector<int> pontoTCount(nPts, 0);
    for (const auto &s : A.segmentos) {
        pontoTSum[s.a] += s.t;
        pontoTSum[s.b] += s.t;
        pontoTCount[s.a]++;
        pontoTCount[s.b]++;
    }
    std::vector<float> pontoT(nPts, 0.5f);
    for (int i = 0; i < nPts; ++i) {
        if (pontoTCount[i] > 0) pontoT[i] = pontoTSum[i] / pontoTCount[i];
        else pontoT[i] = 0.5f;
    }

    // Espessura por vértice (em coordenadas normalizadas)
    std::vector<float> pontoEsp(nPts, 0.0f);
    for (int i = 0; i < nPts; ++i)
        pontoEsp[i] = glm::mix(ESPESSURA_MIN, ESPESSURA_MAX, pontoT[i]);

    // Calcular normal média por vértice (somatório de direções dos segmentos incidentes)
    std::vector<glm::vec2> dirSum(nPts, glm::vec2(0.0f));
    std::vector<int> deg(nPts, 0);
    for (const auto &s : A.segmentos) {
        glm::vec2 a = glm::vec2(P[s.a]);
        glm::vec2 b = glm::vec2(P[s.b]);
        glm::vec2 d = glm::normalize(b - a);
        dirSum[s.a] += d;
        dirSum[s.b] -= d;
        deg[s.a]++;
        deg[s.b]++;
    }

    std::vector<glm::vec2> normals(nPts, glm::vec2(1.0f, 0.0f));
    for (int i = 0; i < nPts; ++i) {
        if (deg[i] > 0) {
            glm::vec2 s = dirSum[i];
            float len = glm::length(s);
            glm::vec2 d;
            if (len > 1e-6f) d = s / len;
            else {
                // fallback: encontrar qualquer direção de segmento incidente
                bool found = false;
                for (const auto &seg : A.segmentos) {
                    if (seg.a == i) { glm::vec2 other = glm::vec2(P[seg.b]); d = glm::normalize(other - glm::vec2(P[i])); found = true; break; }
                    if (seg.b == i) { glm::vec2 other = glm::vec2(P[seg.a]); d = glm::normalize(other - glm::vec2(P[i])); found = true; break; }
                }
                if (!found) d = glm::vec2(1.0f, 0.0f);
            }
            normals[i] = glm::vec2(-d.y, d.x);
        } else {
            normals[i] = glm::vec2(1.0f, 0.0f);
        }
    }

    // Cores por vértice, seguindo o raio variável
    std::vector<glm::vec3> corPt(nPts);
    for (int i = 0; i < nPts; ++i) corPt[i] = gradiente(pontoT[i]);

    // Construir triângulos usando offsets por vértice (garante conectividade)
    auto push = [&](const glm::vec3 &pos, const glm::vec3 &col) {
        vboData.insert(vboData.end(), {pos.x, pos.y, pos.z, col.r, col.g, col.b});
    };

    // Construir triângulos usando offsets por vértice (suaviza transições de espessura)
    for (const auto &s : A.segmentos) {
        glm::vec3 A0 = P[s.a];
        glm::vec3 B0 = P[s.b];

        // Use segment normal so the width along the segment is consistent
        glm::vec2 segDir = glm::normalize(glm::vec2(B0 - A0));
        glm::vec2 segN = glm::vec2(-segDir.y, segDir.x);

        glm::vec2 offA = segN * pontoEsp[s.a];
        glm::vec2 offB = segN * pontoEsp[s.b];

        glm::vec3 v1(A0.x + offA.x, A0.y + offA.y, 0);
        glm::vec3 v2(A0.x - offA.x, A0.y - offA.y, 0);
        glm::vec3 v3(B0.x + offB.x, B0.y + offB.y, 0);
        glm::vec3 v4(B0.x - offB.x, B0.y - offB.y, 0);

        // Cor por vértice (suaviza transições de cor)
        glm::vec3 cA = corPt[s.a];
        glm::vec3 cB = corPt[s.b];

        push(v1, cA); push(v2, cA); push(v4, cB);
        push(v1, cA); push(v4, cB); push(v3, cB);
    }

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vboData.size() * sizeof(float),
                 vboData.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    GLuint prog = criarShader();
    GLint locMVP = glGetUniformLocation(prog, "MVP");

    while (!glfwWindowShouldClose(win)) {
        processInput(win);

        // se uma recarga foi solicitada pela callback de teclado
        if (g_reloadRequested) {
            g_reloadRequested = false;
            int newStep = g_requestedStep;
            std::string novoArquivo = buildVTKPath(g_nDim, g_tamanhoArvore, newStep);
            Arvore2D An = carregarVTK(novoArquivo);
                if (!An.pontos.empty()) {
                A = std::move(An);
                g_currentStep = newStep;
                    std::cout << "Exibindo step: " << g_currentStep << std::endl;


                // reconstruir dados do VBO (duplicado da lógica de construção acima)
                std::vector<float> newVBO;

                // refaz a normalização/centralização
                glm::vec2 minP(FLT_MAX), maxP(-FLT_MAX);
                for (const auto &p : A.pontos) {
                    minP.x = std::min(minP.x, p.pos.x);
                    minP.y = std::min(minP.y, p.pos.y);
                    maxP.x = std::max(maxP.x, p.pos.x);
                    maxP.y = std::max(maxP.y, p.pos.y);
                }
                glm::vec2 center = (minP + maxP) * 0.5f;
                float rangeX = maxP.x - minP.x;
                float rangeY = maxP.y - minP.y;
                float scale = std::max(rangeX, rangeY);
                if (scale == 0.0f) scale = 1.0f;

                int nPts = (int)A.pontos.size();
                std::vector<glm::vec3> P(nPts);
                for (int i = 0; i < nPts; ++i) {
                    P[i] = A.pontos[i].pos;
                    P[i].x = (P[i].x - center.x) / scale;
                    P[i].y = (P[i].y - center.y) / scale;
                }

                // Calcular t por vértice como média dos segmentos incidentes (suaviza transições)
                std::vector<float> pontoTSum(nPts, 0.0f);
                std::vector<int> pontoTCount(nPts, 0);
                for (const auto &s : A.segmentos) {
                    pontoTSum[s.a] += s.t;
                    pontoTSum[s.b] += s.t;
                    pontoTCount[s.a]++;
                    pontoTCount[s.b]++;
                }
                std::vector<float> pontoT(nPts, 0.5f);
                for (int i = 0; i < nPts; ++i) {
                    if (pontoTCount[i] > 0) pontoT[i] = pontoTSum[i] / pontoTCount[i];
                    else pontoT[i] = 0.5f;
                }

                std::vector<float> pontoEsp(nPts, 0.0f);
                for (int i = 0; i < nPts; ++i)
                    pontoEsp[i] = glm::mix(ESPESSURA_MIN, ESPESSURA_MAX, pontoT[i]);

                std::vector<glm::vec2> dirSum(nPts, glm::vec2(0.0f));
                std::vector<int> deg(nPts, 0);
                for (const auto &s : A.segmentos) {
                    glm::vec2 a = glm::vec2(P[s.a]);
                    glm::vec2 b = glm::vec2(P[s.b]);
                    glm::vec2 d = glm::normalize(b - a);
                    dirSum[s.a] += d;
                    dirSum[s.b] -= d;
                    deg[s.a]++;
                    deg[s.b]++;
                }

                std::vector<glm::vec2> normals(nPts, glm::vec2(1.0f, 0.0f));
                for (int i = 0; i < nPts; ++i) {
                    if (deg[i] > 0) {
                        glm::vec2 s = dirSum[i];
                        float len = glm::length(s);
                        glm::vec2 d;
                        if (len > 1e-6f) d = s / len;
                        else {
                            bool found = false;
                            for (const auto &seg : A.segmentos) {
                                if (seg.a == i) { glm::vec2 other = glm::vec2(P[seg.b]); d = glm::normalize(other - glm::vec2(P[i])); found = true; break; }
                                if (seg.b == i) { glm::vec2 other = glm::vec2(P[seg.a]); d = glm::normalize(other - glm::vec2(P[i])); found = true; break; }
                            }
                            if (!found) d = glm::vec2(1.0f, 0.0f);
                        }
                        normals[i] = glm::vec2(-d.y, d.x);
                    } else {
                        normals[i] = glm::vec2(1.0f, 0.0f);
                    }
                }

                std::vector<glm::vec3> corPt(nPts);
                for (int i = 0; i < nPts; ++i) corPt[i] = gradiente(pontoT[i]);

                auto pushNew = [&](const glm::vec3 &pos, const glm::vec3 &col) {
                    newVBO.insert(newVBO.end(), {pos.x, pos.y, pos.z, col.r, col.g, col.b});
                };

                for (const auto &s : A.segmentos) {
                    glm::vec3 A0 = P[s.a];
                    glm::vec3 B0 = P[s.b];
                    glm::vec2 segDir = glm::normalize(glm::vec2(B0 - A0));
                    glm::vec2 segN = glm::vec2(-segDir.y, segDir.x);

                    glm::vec2 offA = segN * pontoEsp[s.a];
                    glm::vec2 offB = segN * pontoEsp[s.b];

                    glm::vec3 v1(A0.x + offA.x, A0.y + offA.y, 0);
                    glm::vec3 v2(A0.x - offA.x, A0.y - offA.y, 0);
                    glm::vec3 v3(B0.x + offB.x, B0.y + offB.y, 0);
                    glm::vec3 v4(B0.x - offB.x, B0.y - offB.y, 0);

                    glm::vec3 cA = corPt[s.a];
                    glm::vec3 cB = corPt[s.b];

                    pushNew(v1, cA); pushNew(v2, cA); pushNew(v4, cB);
                    pushNew(v1, cA); pushNew(v4, cB); pushNew(v3, cB);
                }

                // atualizar VBO OpenGL
                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                glBufferData(GL_ARRAY_BUFFER, newVBO.size() * sizeof(float), newVBO.data(), GL_STATIC_DRAW);
                vboData.swap(newVBO);
            } else {
                std::cerr << "Falha ao carregar step " << newStep << std::endl;
            }
        }

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        float asp = (float)w / h;

        glm::mat4 proj = glm::ortho(-asp, asp, -1.f, 1.f);
        glm::mat4 view = glm::translate(glm::mat4(1.0f),
                                        glm::vec3(-cameraPos, 0));
        view = glm::rotate(view, glm::radians(rotacao), glm::vec3(0,0,1));
        view = glm::scale(view, glm::vec3(zoom));

        glm::mat4 MVP = proj * view;

        glClearColor(0.95f, 0.95f, 0.95f, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vboData.size() / 6);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
