#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <cfloat>

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
glm::vec3 gradiente(float t) {
    t = glm::clamp(t, 0.0f, 1.0f);
    return {
        1.5f * (1 - t) * t,
        1.0f - std::abs(t - 0.5f) * 2.0f,
        t
    };
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

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    Arvore2D A = carregarVTK(arquivo);
    if (A.pontos.empty()) return -1;

    std::vector<float> vboData;

    for (const auto& s : A.segmentos) {
        glm::vec3 A0 = A.pontos[s.a].pos;
        glm::vec3 B0 = A.pontos[s.b].pos;

        glm::vec2 d = glm::normalize(glm::vec2(B0 - A0));
        glm::vec2 n(-d.y, d.x);

        // Espessura real (usada apenas para cálculo de cor já feita antes)
        float espReal = glm::mix(ESPESSURA_MIN, ESPESSURA_MAX, s.t);
        // Usar uma espessura visual constante para todas as linhas;
        // a espessura "real" será representada apenas pela cor (s.cor).
        float espVisual = (ESPESSURA_MIN + ESPESSURA_MAX) * 0.5f;
        glm::vec2 off = n * espVisual;

        glm::vec3 v1(A0.x + off.x, A0.y + off.y, 0);
        glm::vec3 v2(A0.x - off.x, A0.y - off.y, 0);
        glm::vec3 v3(B0.x + off.x, B0.y + off.y, 0);
        glm::vec3 v4(B0.x - off.x, B0.y - off.y, 0);

        auto push = [&](glm::vec3 p) {
            vboData.insert(vboData.end(), {p.x, p.y, p.z,
                                           s.cor.r, s.cor.g, s.cor.b});
        };

        push(v1); push(v2); push(v4);
        push(v1); push(v4); push(v3);
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
