# 🐚 MiniShell - Práctica de SSOO

![Language](https://img.shields.io/badge/Language-C-00599C?style=flat-square&logo=c&logoColor=white)
![OS](https://img.shields.io/badge/OS-Linux-FCC624?style=flat-square&logo=linux&logoColor=black)
![Course](https://img.shields.io/badge/Asignatura-Sistemas_Operativos-lightgrey?style=flat-square)

> Intérprete de comandos desarrollado en C para la asignatura de Sistemas Operativos.

Este proyecto implementa una shell básica capaz de ejecutar mandatos externos, gestionar procesos en primer y segundo plano, y manipular la entrada/salida mediante redirecciones y tuberías.

## 👥 Autores

| Nombre | Rol |
| :--- | :--- |
| **Héctor Julián Alijas** | Desarrollador |
| **Daniel Martín Muñoz** | Desarrollador |

---

## 🚀 Funcionalidades Implementadas

### 1. ⚙️ Gestión de Procesos
*   **Mandatos externos**: Soporte total para ejecutables del sistema (e.g., `ls`, `grep`, `sleep`).
*   **Background (`&`)**: Ejecución asíncrona de tareas sin bloquear la terminal.
*   **Job Control**:
    *   Gestión de hasta **20 trabajos** simultáneos.
    *   Monitorización de estado (`EJECUTANDO`, `FINALIZADO`).
    *   Limpieza automática de procesos *zombie*.

### 2. 🔧 Mandatos Internos (Built-ins)

| Comando | Descripción | Uso |
| :--- | :--- | :--- |
| `cd` | Cambia el directorio actual (por defecto a `HOME`). | `cd [dir]` |
| `jobs` | Lista los trabajos activos y su estado. | `jobs` |
| `fg` | Trae un proceso de background al primer plano. | `fg [id]` |
| `exit` | Cierra la shell (opcionalmente con código de retorno). | `exit [n]` |

### 3. 🔀 Redirecciones y Tuberías
*   **Entrada (`<`)**: `cmd < fichero`
*   **Salida (`>`)**: `cmd > fichero`
*   **Error (`>&`)**: Soporte para redirigir `stderr`.
*   **Tuberías (`|`)**: Conexión de múltiples comandos (e.g., `ls | grep .c | wc -l`). Soporta *N* comandos encadenados.

### 4. 🚦 Gestión de Señales
*   **SIGINT (`Ctrl+C`)**:
    *   *En el prompt*: Se ignora (imprime nueva línea).
    *   *En ejecución*: Se envía al proceso en primer plano.
*   **SIGQUIT (`Ctrl+\`)**:
    *   Envía terminación con volcado de memoria (*core dump*) si hay un proceso activo.

### 5. 📝 Parsing
*   **Limpieza de comillas**: Elimina comillas simples o dobles innecesarias (`"archivo.txt"` → `archivo.txt`).
*   **Tokenización**: Utiliza la librería externa `parser.h` para el análisis léxico.

---

## 🛠️ Compilación

El proyecto depende de la librería de parsing (`parser.h` / `libparser.a` o `parser.c`).

# Compilar usando make
```bash
make
```

# O compilación manual
```bash
gcc -Wall -Wextra -o minishell minishell.c parser.c
```

## 🖥️ Uso
Una vez iniciada, la shell muestra el prompt:

```bash
msh> 
```

Ejemplos Prácticos:

Trabajos en segundo plano y control
```bash
msh> sleep 20 &
 12345[1]

msh> jobs
+ Running    sleep 20[1]

msh> fg 1
sleep 20
# (Espera a que termine el proceso en primer plano)
```

Tuberías y redirecciones complejas
```bash
msh> ls -l | grep "minishell" > salida.txt
Manejo de errores
bash
msh> cd directorio_falso
cd: directorio_falso: No such file or directory
```

## 📂 Estructura del Código
El flujo principal del programa se organiza de la siguiente manera:

main: Bucle infinito que lee con fgets, parsea con tokenize y decide si ejecutar un built-in o lanzar procesos hijos con fork.

Job Control (init_jobs, add_job, check_jobs): Gestiona la tabla de procesos y realiza el reaping de zombies usando waitpid con WNOHANG.

Señales (manejador_ctrl_c, _quit): Capturan interrupciones y las reenvían a la variable global fg_pid.

Built-ins: Funciones modulares para cada comando interno.

## ⚙️ Limitaciones y Constantes
⚠️ Nota: No se implementó memoria dinámica para las estructuras de control por complejidad y diseño académico.

Longitud máxima de línea: 1024 caracteres.

Máximo de trabajos (Jobs): 20.

Máximo de procesos por trabajo: 32 (permite tuberías extensas).