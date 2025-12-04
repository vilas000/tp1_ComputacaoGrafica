#include <glad/glad.h> 
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm> 
#include <cstdlib> // Para rand()
#include <ctime>   // Para time()

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- ESTRUTURAS ---
struct Ponto { glm::vec3 posicao; };
struct Segmento { int indicePontoA; int indicePontoB; float raio; glm::vec3 cor; }; // Adicionamos COR aqui
struct Arvore2D { std::vector<Ponto> vertices; std::vector<Segmento> segmentos; };

// --- VARIÁVEIS GLOBAIS ---
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 0.0f);
float zoomLevel = 1.0f;
float anguloRotacao = 0.0f;

// Controle de Crescimento
int segmentosVisiveis = 0;
int totalSegmentos = 0;
float ultimoTempoCrescimento = 0.0f;
float delayCrescimento = 0.05f;

// --- SHADERS ATUALIZADOS PARA RECEBER COR ---
const char* vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n" // Recebe a cor do buffer
    "out vec3 ourColor;\n"                     // Envia para o Fragment Shader
    "uniform mat4 transform;\n" 
    "void main(){\n"
    "   gl_Position = transform * vec4(aPos, 1.0);\n"
    "   ourColor = aColor;\n"                  // Passa a cor adiante
    "}\0";

const char* fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec3 ourColor;\n"                      // Recebe do Vertex Shader
    "void main(){ FragColor = vec4(ourColor, 1.0f); }\n\0";

// --- CALLBACKS ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow *window) {
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float velPan = 0.02f / zoomLevel; 
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)    cameraPos.y -= velPan;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)  cameraPos.y += velPan;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  cameraPos.x += velPan;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) cameraPos.x -= velPan;
    
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) zoomLevel *= 1.02f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) zoomLevel *= 0.98f;
    
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) anguloRotacao += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) anguloRotacao -= 1.0f;

    float tempoAtual = glfwGetTime();
    float delayAtual = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 0.001f : delayCrescimento;

    if (tempoAtual - ultimoTempoCrescimento > delayAtual) {
        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) { segmentosVisiveis++; ultimoTempoCrescimento = tempoAtual; }
        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) { segmentosVisiveis--; ultimoTempoCrescimento = tempoAtual; }
        segmentosVisiveis = std::max(0, std::min(segmentosVisiveis, totalSegmentos));
    }
}

// --- PARSER ---
Arvore2D carregarVTK(const std::string& caminho) {
    Arvore2D arvore;
    std::ifstream ficheiro(caminho);
    std::string linha;
    if (!ficheiro.is_open()) {
        std::cerr << "ERRO: Nao consegui abrir " << caminho << std::endl;
        return arvore;
    }

    // Semente aleatória para cores
    srand(time(NULL));

    std::string estadoAtual = "HEADER";
    int indiceRaioAtual = 0;
    while (std::getline(ficheiro, linha)) {
        if (linha.empty()) continue;
        std::stringstream ss(linha); std::string palavra; ss >> palavra;
        
        if (palavra == "POINTS") { estadoAtual = "POINTS"; int n; ss >> n; arvore.vertices.reserve(n); continue; }
        else if (palavra == "LINES") { estadoAtual = "LINES"; int n; ss >> n; arvore.segmentos.reserve(n); continue; }
        else if (palavra == "CELL_DATA") { estadoAtual = "CELL_DATA"; continue; }
        else if (palavra == "scalars" || palavra == "LOOKUP_TABLE") continue; 

        if (estadoAtual == "POINTS") {
            std::stringstream ss_l(linha); Ponto p; ss_l >> p.posicao.x >> p.posicao.y >> p.posicao.z;
            arvore.vertices.push_back(p);
        } else if (estadoAtual == "LINES") {
            std::stringstream ss_l(linha); int n; Segmento s; ss_l >> n >> s.indicePontoA >> s.indicePontoB;
            if (n==2) { 
                s.raio=0.0f; 
                
                // GERA COR ALEATÓRIA VIBRANTE PARA CADA SEGMENTO
                // Evitamos cores muito escuras garantindo um mínimo de 0.2
                float r = (rand() % 100) / 100.0f;
                float g = (rand() % 100) / 100.0f;
                float b = (rand() % 100) / 100.0f;
                
                // Opção: Se quiser distinguir vizinhos, random total é o melhor.
                s.cor = glm::vec3(r, g, b);

                arvore.segmentos.push_back(s); 
            }
        } else if (estadoAtual == "CELL_DATA") {
            std::stringstream ss_l(linha); float r; ss_l >> r;
            if (indiceRaioAtual < arvore.segmentos.size()) arvore.segmentos[indiceRaioAtual++].raio = r;
        }
    }
    return arvore;
}

unsigned int setupShaders() {
    unsigned int v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vertexShaderSource, NULL); glCompileShader(v);
    unsigned int f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fragmentShaderSource, NULL); glCompileShader(f);
    unsigned int p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f); return p;
}

// --- MAIN COM ARGUMENTOS (argc, argv) ---
int main(int argc, char* argv[]) {
    // Verificar se o usuário passou um arquivo
    std::string caminhoArquivo;
    if (argc > 1) {
        caminhoArquivo = argv[1]; // Pega o argumento do terminal
    } else {
        std::cout << "Uso: ./meu_app <caminho_para_arquivo.vtk>" << std::endl;
        std::cout << "Carregando arquivo padrao..." << std::endl;
        // Caminho padrão (fallback)
        caminhoArquivo = "/home/vilas000/Downloads/tree.vtk"; // Ajuste se necessário
    }

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Visualizador CCO", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    std::cout << "Tentando carregar: " << caminhoArquivo << std::endl;
    Arvore2D minhaArvore = carregarVTK(caminhoArquivo);
    
    if (minhaArvore.vertices.empty()) { 
        std::cerr << "Falha ao carregar a arvore! Verifique o caminho." << std::endl;
        glfwTerminate(); return -1; 
    }

    totalSegmentos = minhaArvore.segmentos.size();
    segmentosVisiveis = totalSegmentos; 
    std::string tituloBase = "TP1 [" + caminhoArquivo + "]";
    glfwSetWindowTitle(window, tituloBase.c_str());

    // 2. PREPARAR BUFFERS COM COR (Position + Color)
    // ----------------------------------------------
    std::vector<float> dadosGPU; // Agora contém X,Y,Z, R,G,B, X,Y,Z, R,G,B...
    for (const auto& s : minhaArvore.segmentos) {
        const auto& pA = minhaArvore.vertices[s.indicePontoA];
        const auto& pB = minhaArvore.vertices[s.indicePontoB];
        
        // Vértice A (Posição + Cor do Segmento)
        dadosGPU.push_back(pA.posicao.x); dadosGPU.push_back(pA.posicao.y); dadosGPU.push_back(pA.posicao.z);
        dadosGPU.push_back(s.cor.r);      dadosGPU.push_back(s.cor.g);      dadosGPU.push_back(s.cor.b);

        // Vértice B (Posição + Cor do Segmento)
        dadosGPU.push_back(pB.posicao.x); dadosGPU.push_back(pB.posicao.y); dadosGPU.push_back(pB.posicao.z);
        dadosGPU.push_back(s.cor.r);      dadosGPU.push_back(s.cor.g);      dadosGPU.push_back(s.cor.b);
    }

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, dadosGPU.size()*sizeof(float), dadosGPU.data(), GL_STATIC_DRAW);

    // Agora o "stride" é 6 floats (3 pos + 3 cor)
    // Atributo 0: Posição (começa no offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Atributo 1: Cor (começa no offset 3 floats)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);

    unsigned int prog = setupShaders();
    unsigned int loc = glGetUniformLocation(prog, "transform");

    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT); 

        glUseProgram(prog);
        glBindVertexArray(VAO);

        int w, h; glfwGetFramebufferSize(window, &w, &h);
        float asp = (float)w/h;
        glm::mat4 proj = glm::ortho(-asp, asp, -1.0f, 1.0f, -1.0f, 1.0f);
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::scale(model, glm::vec3(zoomLevel));
        model = glm::translate(model, -cameraPos);      
        model = glm::rotate(model, glm::radians(anguloRotacao), glm::vec3(0,0,1));
        
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(proj * model));
        glDrawArrays(GL_LINES, 0, segmentosVisiveis * 2);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO); glDeleteBuffers(1, &VBO); glDeleteProgram(prog);
    glfwTerminate();
    return 0;
}