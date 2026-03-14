#pragma once
#include <string>
#include <vector>
#include <map>
#include "../Structs/Structs.h"

// Todo lo relacionado a discos y particiones
namespace DiskManagement
{
    // Struct interno para el mapa de montajes en RAM
    struct MountedPartition
    {
        std::string diskPath; // path absoluto del .mia
        std::string name;     // nombre de la partición
        std::string id;       // ID asignado, ej: "341A"
        int partIndex;        // índice en MBR.Partitions (0-3), -1 si lógica
        bool isLogical;       // true si es partición lógica (dentro de extendida)
        int ebrPos;           // posición en bytes del EBR en el disco (-1 si no es lógica)
    };

    // Mapa global
    extern std::map<std::string, MountedPartition> MountMap;
    // 2 dígitos del carnet para generar IDs
    extern const std::string CARNET_DIGITS;

    // mkdisk: Crea un disco virtual .mia
    std::string Mkdisk(int size, const std::string &path,
                       const std::string &fit, const std::string &unit);

    // rmdisk: Elimina un disco virtual .mia (pide confirmación via output)
    std::string Rmdisk(const std::string &path);

    // fdisk: crea, elimina o ajusta una particion en el disco
    std::string Fdisk(int size, const std::string &path,
                      const std::string &name,
                      const std::string &type,
                      const std::string &fit,
                      const std::string &unit,
                      const std::string &deleteMode = "",
                      int addValue = 0);

    // mount: Monta una partición y le asigna un ID único
    std::string Mount(const std::string &path, const std::string &name);

    // mounted: Devuelve un listado de particiones montadas (RAM)
    std::string Mounted();

    // Helpers
    int ToBytes(int size, const std::string &unit);
    char FitChar(const std::string &fit);

    // Ajustes para encontrar espacio libre en el disco
    int FirstFit(const MBR &mbr, int requiredSize, int diskSize);
    int BestFit(const MBR &mbr, int requiredSize, int diskSize);
    int WorstFit(const MBR &mbr, int requiredSize, int diskSize);
}
