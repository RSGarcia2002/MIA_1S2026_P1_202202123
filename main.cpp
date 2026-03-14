#include <iostream>
#include <string>
#include <cassert>
#include <unistd.h>
#include <libgen.h>
#include <climits>
#include "Analyzer/Analyzer.h"
#include "Structs/Structs.h"
#include "HttpServer/HttpServer.h"

// Verifica que las estructuras tengan el tamanio correcto al compilar.
// Si falla, hay un problema con #pragma pack o el compilador.
void CheckStructSizes()
{
    static_assert(sizeof(Partition) == 35, "Partition debe ser 35 bytes");
    static_assert(sizeof(MBR) == 168, "MBR debe ser 168 bytes");
    static_assert(sizeof(EBR) == 30, "EBR debe ser 30 bytes");
    static_assert(sizeof(BloqueDir) == 64, "BloqueDir debe ser 64 bytes");
    static_assert(sizeof(BloqueFile) == 64, "BloqueFile debe ser 64 bytes");
    static_assert(sizeof(BloqueApunt) == 64, "BloqueApunt debe ser 64 bytes");

    std::cout << "Tamanios de estructuras correctos\n";
    std::cout << "  MBR:       " << sizeof(MBR) << " bytes\n";
    std::cout << "  Partition: " << sizeof(Partition) << " bytes\n";
    std::cout << "  EBR:       " << sizeof(EBR) << " bytes\n\n";
}

// Modo consola para desarrollo y pruebas locales
void RunConsole()
{
    std::cout << "MIA P1 - EXT2 Disk Simulator\n";
    std::cout << "Comandos: mkdisk, rmdisk, fdisk, mount, mounted\n\n";

    std::string line;
    while (true)
    {
        std::cout << "mia> ";
        if (!std::getline(std::cin, line))
            break;

        std::string result = Analyzer::Analyze(line);
        if (!result.empty())
            std::cout << result << "\n\n";
    }
}

int main(int argc, char *argv[])
{
    // Cambiar cwd al directorio del binario para que todos los
    // archivos relativos (discos, reports/) siempre queden en build/
    {
        char exePath[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0)
        {
            exePath[len] = '\0';
            chdir(dirname(exePath));
        }
    }

    CheckStructSizes();

    // Modo servidor: ./MIA_P1 --server [puerto]
    if (argc >= 2 && std::string(argv[1]) == "--server")
    {
        int port = 8080;
        if (argc >= 3)
            port = std::stoi(argv[2]);
        HttpServer::Start(port);
    }
    else
    {
        RunConsole();
    }

    return 0;
}
