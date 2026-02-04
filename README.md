MiniShell - Práctica de SSOO
Intérprete de comandos desarrollado en C para la asignatura de Sistemas Operativos.

Este proyecto implementa una shell básica capaz de ejecutar mandatos externos, gestionar procesos en primer y segundo plano, y manipular la entrada/salida mediante redirecciones y tuberías.

👥 Autores
Héctor Julián Alijas

Daniel Martín Muñoz

🚀 Funcionalidades Implementadas
1. Gestión de Procesos
Ejecución de mandatos externos: Soporte para cualquier ejecutable del sistema (e.g., ls, grep, sleep).

Background (&): Ejecución de tareas en segundo plano sin bloquear la terminal.

Job Control:

Gestión de hasta 20 trabajos simultáneos.

Monitorización de estado (EJECUTANDO, FINALIZADO).

Limpieza automática de procesos "zombie".

2. Mandatos Internos (Built-ins)
El código incluye la implementación nativa de:

cd [dir]: Cambia el directorio actual. Si no se especifica argumento, va a HOME. Gestiona errores de rutas.

jobs: Lista los trabajos activos y su estado (Running o Done).

fg [id]: Trae un trabajo de segundo plano al primer plano.

Sin argumentos: Trae el último trabajo ejecutado.

Con ID: Trae el trabajo específico (e.g., fg 1).

exit [n]: Cierra la shell, opcionalmente con un código de retorno.

3. Redirecciones y Tuberías
Redirección de entrada (<): cmd < fichero

Redirección de salida (>): cmd > fichero

Redirección de error: Soporte específico para redirigir stderr.

Tuberías (Pipes |): Conexión de múltiples comandos (e.g., ls | grep .c | wc -l). Soporta N comandos encadenados.

4. Gestión de Señales
SIGINT (Ctrl+C):

En el prompt: Se ignora (imprime nueva línea).

En ejecución: Se envía al proceso en primer plano (fg).

SIGQUIT (Ctrl+):

Similar a SIGINT, envía la señal de terminación con volcado de memoria (core dump) si hay proceso en primer plano.

5. Parsing
Limpieza de comillas: Elimina comillas simples o dobles innecesarias en los argumentos ("archivo.txt" -> archivo.txt).

El intérprete utiliza una librería externa parser.h (función tokenize) para el análisis léxico.

🛠️ Compilación
El proyecto depende de la librería de parsing (parser.h / libparser.a o parser.c). Asegúrate de tener los objetos necesarios compilados.

bash
# Compilar usando make (si dispones del Makefile)
make

# O compilación manual (ejemplo)
gcc -Wall -Wextra -o minishell minishell.c parser.c
🖥️ Uso
Una vez iniciada, la shell muestra el prompt:

bash
msh> 
Ejemplos
Trabajos en segundo plano y control:

bash
msh> sleep 20 &
[1] 12345
msh> jobs
[1]+ Running    sleep 20
msh> fg 1
sleep 20
# (Espera a que termine)
Tuberías y redirecciones:

bash
msh> ls -l | grep "minishell" > salida.txt
Manejo de errores:

bash
msh> cd directorio_falso
cd: directorio_falso: No such file or directory

📂 Estructura del Código
main: Bucle principal. Lee (fgets), parsea (tokenize), gestiona built-ins y lanza hijos (fork).

init_jobs / add_job / check_jobs: Lógica para la tabla de procesos y limpieza de zombies con waitpid(-1, &status, WNOHANG).

manejador_ctrl_c / _quit: Captura de señales y reenvío a la variable global fg_pid.

Built-ins: Funciones dedicadas builtin_cd, builtin_jobs, builtin_fg, builtin_exit.

⚙️ Limitaciones y Constantes
Longitud máxima de línea: 1024 caracteres.

Máximo de trabajos (Jobs): 20.

Máximo de procesos por trabajo: 32 (para tuberías largas).

(No se implementó memoria dinámica por complejidad).