#pragma once
#include <string>

// Formateo de particiones EXT2 (Sprint 2)
namespace FileSystem
{
    // Formatea la particion indicada por el ID como EXT2
    std::string Mkfs(const std::string &id, const std::string &type);
}
