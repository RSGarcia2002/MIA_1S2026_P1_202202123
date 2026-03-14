#pragma once
#include <string>
#include <vector>

// Operaciones sobre archivos y carpetas del sistema EXT2 (Sprint 4)
namespace FileOperations
{
    std::string Mkfile(const std::string &path, bool recursive, int size, const std::string &cont);
    std::string Mkdir(const std::string &path, bool parents);
    std::string Cat(const std::vector<std::string> &files);
}
