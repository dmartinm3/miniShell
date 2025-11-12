CHECKLIST Minishell
Marca cada casilla al validar la prueba y ejecuta el comando sugerido para comprobar el comportamiento.​

Prompt e interacción
 Prompt exacto “msh ” y reimpresión tras cada ejecución en foreground.​

Comando: iniciar minishell y pulsar Enter varias veces.​

 Línea vacía o solo espacios no ejecuta nada y vuelve al prompt.​

Comando: introducir solo espacios y Enter.​

 Parser reconoce ncommands, args, redirecciones y background.​

Comando: echo a < in.txt | tr a A 2> err.txt & ​

Mandatos simples
 Ejecuta mandato con argumentos en foreground y espera.​

Comando: /bin/echo hola mundo​

 Mandato inexistente → error por stderr, shell sigue operativa.​

Comando: foobarbaz​

Redirecciones
 Entrada solo en el primer mandato del pipeline.​

Comando: printf "x\ny\n" > in.txt; grep y < in.txt​

 Salida solo en el último mandato; crea/trunca fichero.​

Comando: ls > out.txt; wc -l out.txt​

 Error solo en el último mandato; dup2 a STDERR.​

Comando: ls no_existe 2> err.txt; test -s err.txt && echo ok​

 Error al abrir fichero se informa como “Fichero: Error. …”.​

Comando: cat < no_such_file​

Pipelines
 Pipeline de 2 mandatos, cierre correcto de extremos de pipe.​

Comando: ls -1 | wc -l ​

 Pipeline de N≥3 mandatos encadenados.​

Comando: printf "a\nb\na\n" | grep a | sort | uniq -c ​

 Sin deadlocks: cierre de fds en padre e hijos.​

Comando: yes | head -n 5 | wc -l (debe imprimir 5) ​

Background y jobs
 ‘&’ no bloquea, muestra PID/identificador del job.​

Comando: sleep 2 &​

 Tabla de trabajos mantiene procesos activos.​

Comando: sleep 30 &; sleep 300 &; jobs​

 Limpieza de terminados/erróneos en jobs.​

Comando: sleep 1 &; sleep 2; jobs​

Señales
 Minishell y background inmunes a Ctrl-C/Ctrl-; foreground por defecto.​

Comando: sleep 30 y pulsar Ctrl-C; luego sleep 30 & y pulsar Ctrl-C.​

 Reenvío de señales al foreground sin matar la shell.​

Comando: cat y pulsar Ctrl-\ para finalizar.​

Comandos internos
 cd sin argumento → HOME e imprime cwd absoluto.​

Comando: cd​

 cd con ruta relativa y absoluta.​

Comando: mkdir -p /tmp/msh_test; cd /tmp/msh_test​

 cd con demasiados argumentos → uso/error y no cambia.​

Comando: cd a b​

 jobs lista activos y limpia terminados.​

Comando: sleep 5 &; jobs; sleep 6; jobs​

 fg sin argumentos trae el último job.​

Comando: sleep 5 &; fg​

 fg <id> trae el job indicado.​

Comando: sleep 30 &; sleep 300 &; jobs; fg 0 (ajusta al índice que muestre tu jobs)​

Gestión de errores
 Línea sin mandatos vuelve al prompt sin fallar.​

Comando: introducir “|” o solo espacios y Enter. ​

 Errores en fork/pipe/dup2/exec informan y limpian.​

Comando: prueba avanzada: ulimit -u 1; /bin/echo x; luego restaurar ulimit.​

 Mensaje “Mandato: Error. No se encuentra el mandato <cmd>”.​

Comando: nonexistentcmd​

Gestión de recursos
 Restaurar stdin/stdout/stderr tras cada línea.​

Comando: ls > out.txt; echo OK (debe ir a pantalla)​

 Cierre de descriptores de pipe correcto.​

Comando: yes | head -n 1 (debe terminar rápido) ​

 Sin fugas de memoria perceptibles en bucles.​

Comando: for i in $(seq 1 200); do echo x | cat; done ​

Combinadas
 In en 1º + out en último + background.​

Comando: echo "x\nx\ny" > in.txt; cat < in.txt | grep x | wc -l > out.txt & ​

 Error en entrada + pipeline no cuelga.​

Comando: grep a < no_such | wc -l ​

 Error de salida en último mandato informado.​

Comando: ls | wc -l > /root/out.txt (sin permisos) ​

Objetivos/evaluación del enunciado
 Mandato simple con 0+ args.​

Comando: /bin/echo ok​

 Simple con redirecciones in/out.​

Comando: tr a A < in.txt > out.txt​

 Dos mandatos con pipe + redirecciones válidas.​

Comando: grep a < in.txt | wc -l > out.txt ​

 N>2 mandatos con pipes/redirecciones.​

Comando: cat in.txt | grep a | sort | uniq -c > out.txt ​

 cd funcional sin pipes.​

Comando: cd /; cd /tmp (si implementas cd - puedes probarlo también)​

 Background + jobs y fg.​

Comando: sleep 60 &; jobs; fg​

 Señales: shell y background protegidos; foreground por defecto.​

Comando: sleep 30 Ctrl-C; sleep 30 & Ctrl-C.​

Compilación y entrega
 Compila con parser.h/libparser.a.​

Comando: gcc myshell.c libparser.a -o msh​

 Memoria PDF con apartados requeridos.​

Comando: n/a, verificar documento.​