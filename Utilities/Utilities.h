#pragma once
#include <fstream>
#include <string>
#include <vector>
#include "../Structs/Structs.h"

// Todo el acceso al disco se hace aquí. Ningún módulo abre/escribe archivos directamente.

namespace Utilities
{
    // Creacion de un archivo binario vacia en la ruta dada
    bool CreateFile(const std::string &path);

    // Abrir un archivo binario existente en modo lectura/escritura
    std::fstream OpenFile(const std::string &path);

    // Escribir un objeto de tipo T en la posición `pos` del archivo
    template <typename T>
    bool WriteObject(std::fstream &file, const T &data, std::streampos pos)
    {
        file.seekp(pos);
        file.write(reinterpret_cast<const char *>(&data), sizeof(T));
        file.flush();
        return file.good();
    }

    // Leer un objeto de tipo T desde la posición `pos` del archivo
    template <typename T>
    bool ReadObject(std::fstream &file, T &data, std::streampos pos)
    {
        file.seekg(pos);
        file.read(reinterpret_cast<char *>(&data), sizeof(T));
        return file.good();
    }

    std::string ToLower(const std::string &str);

    std::string CurrentDateTime();

    bool FileExists(const std::string &path);

    // Recolecta los bloques de datos de un inodo en orden logico.
    // Si pointerBlocks != nullptr, tambien agrega los bloques de apuntadores usados.
    void CollectInodeBlockIndices(std::fstream &file, const SuperBloque &sb,
                                  const Inodo &ino,
                                  std::vector<int> &dataBlocks,
                                  std::vector<int> *pointerBlocks = nullptr);

    // Lee el contenido completo del archivo representado por el inodo.
    std::string ReadFileData(std::fstream &file, const SuperBloque &sb, const Inodo &ino);

};