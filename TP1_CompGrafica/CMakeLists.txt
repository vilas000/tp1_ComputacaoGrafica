cmake_minimum_required(VERSION 3.10)
project(ProjetoGrafica C CXX)
cmake_policy(SET CMP0072 NEW)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependências ---
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)

# --- MÉTODO ROBUSTO PARA GLFW (via pkg-config) ---
# 1. Encontra a ferramenta pkg-config (padrão do Linux)
find_package(PkgConfig REQUIRED)

# 2. Pede ao pkg-config para encontrar o 'glfw3'
#    Isto vai preencher as variáveis GLFW3_INCLUDE_DIRS e GLFW3_LIBRARIES
pkg_check_modules(GLFW3 REQUIRED glfw3)
# -----------------------------------------------


# --- Nosso Projeto ---

# Cria o executável
add_executable(meu_app
    main.cpp
    src/glad.c
)

# Adiciona os 'includes' necessários
target_include_directories(meu_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # Para o GLAD
    ${GLFW3_INCLUDE_DIRS}                # Para o GLFW (via pkg-config)
)

# Linka (conecta) as bibliotecas
target_link_libraries(meu_app PRIVATE
    OpenGL::GL
    glm::glm
    ${GLFW3_LIBRARIES}                   # Para o GLFW (via pkg-config)
)
