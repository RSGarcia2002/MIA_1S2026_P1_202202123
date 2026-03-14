EXTREAMFS – Simulador de Sistema de Archivos EXT2

Universidad de San Carlos de Guatemala Facultad de Ingeniería Curso:
Manejo e Implementación de Archivos Proyecto 1

Estudiante: Randall García Carnet: 202202123

------------------------------------------------------------------------

DESCRIPCIÓN DEL PROYECTO

EXTREAMFS es un simulador de sistema de archivos inspirado en EXT2,
desarrollado en C++, que permite administrar discos virtuales,
particiones y estructuras internas del sistema de archivos mediante una
interfaz de comandos.

El proyecto implementa:

-   Creación y eliminación de discos virtuales
-   Administración de particiones
-   Montaje de particiones
-   Formateo de sistemas de archivos
-   Manejo de archivos y directorios
-   Gestión de usuarios y sesiones
-   Generación de reportes gráficos del sistema

Además, incluye una interfaz web que permite ejecutar comandos y
visualizar reportes generados por el sistema.

------------------------------------------------------------------------

ARQUITECTURA DEL PROYECTO

Backend (C++) Responsable de:

-   Interpretación de comandos
-   Manejo del sistema de archivos
-   Administración de discos
-   Generación de reportes
-   API HTTP para comunicación con el frontend

Frontend (React + Vite) Responsable de:

-   Interfaz gráfica para ejecutar comandos
-   Visualización de resultados
-   Visualización de reportes generados
-   Carga de scripts .smia

------------------------------------------------------------------------

ESTRUCTURA DEL PROYECTO

MIA_1S2026_P1_202202123

Analyzer DiskManagement FileSystem FileOperations HttpServer Reports
Structs Utilities UserSession frontend main.cpp CMakeLists.txt README.md

------------------------------------------------------------------------

REQUISITOS DEL SISTEMA

Linux recomendado

Herramientas necesarias:

g++ cmake nodejs npm graphviz boost

Instalación en Ubuntu:

sudo apt update sudo apt install build-essential cmake nodejs npm
graphviz libboost-all-dev

------------------------------------------------------------------------

COMPILACIÓN DEL BACKEND

Desde la carpeta raíz del proyecto ejecutar:

cmake -S . -B build cmake –build build -j

Esto generará el ejecutable:

build/MIA_P1

------------------------------------------------------------------------

EJECUCIÓN DEL BACKEND

./build/MIA_P1 –server 8080

Si todo funciona correctamente aparecerá:

Tamanios de estructuras correctos MBR: 168 bytes Partition: 35 bytes
EBR: 30 bytes

------------------------------------------------------------------------

EJECUCIÓN DEL FRONTEND

cd frontend npm install npm run dev

Abrir en el navegador:

http://localhost:3000

------------------------------------------------------------------------

EJEMPLO DE COMANDOS

mkdisk -size=50 -unit=M -path=/home/randall/disco.mia fdisk -size=10
-unit=M -path=/home/randall/disco.mia -name=Part1 mount
-path=/home/randall/disco.mia -name=Part1 mkfs -id=231A login -user=root
-pass=123 -id=231A mkdir -path=/home/proyectos mkfile
-path=/home/proyectos/test.txt -size=100 rep -id=231A -path=/tmp/mbr.jpg
-name=mbr

------------------------------------------------------------------------

TECNOLOGÍAS UTILIZADAS

Backend - C++ - Crow HTTP Framework - Graphviz - CMake

Frontend - React - Vite - CSS

------------------------------------------------------------------------

REPOSITORIO

https://github.com/RSGarcia2002/MIA_1S2026_P1_202202123

------------------------------------------------------------------------

AUTOR

Randall García Carnet: 202202123

Proyecto desarrollado para el curso Manejo e Implementación de Archivos
Facultad de Ingeniería – USAC