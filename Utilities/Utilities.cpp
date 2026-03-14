#include "Utilities.h"
#include <filesystem> // Para manejo de archivos y directorios (C++17)
#include <algorithm>  // Para std::transform y ::tolower
#include <chrono>     // Para obtener la fecha y hora actual
#include <ctime>      // Para convertir el tiempo a formato legible
#include <iomanip>    // Para formatear la fecha y hora
#include <sstream>    // Para construir la cadena de fecha y hora formateada
#include <cstring>

namespace Utilities
{

    static void CollectIndirectBlocks(std::fstream &file, const SuperBloque &sb,
                                      int ptrBlockIdx, int level,
                                      std::vector<int> &dataBlocks,
                                      std::vector<int> *pointerBlocks)
    {
        if (ptrBlockIdx < 0 || ptrBlockIdx >= sb.s_blocks_count || level <= 0)
            return;

        if (pointerBlocks != nullptr)
            pointerBlocks->push_back(ptrBlockIdx);

        BloqueApunt ap{};
        if (!ReadObject(file, ap, sb.s_block_start + ptrBlockIdx * (int)sizeof(BloqueApunt)))
            return;

        for (int i = 0; i < 16; i++)
        {
            int child = ap.b_pointers[i];
            if (child < 0 || child >= sb.s_blocks_count)
                continue;

            if (level == 1)
                dataBlocks.push_back(child);
            else
                CollectIndirectBlocks(file, sb, child, level - 1, dataBlocks, pointerBlocks);
        }
    }

    bool CreateFile(const std::string &path)
    {
        // Crear directorios intermedios si no existen
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        // Crear (o truncar) el archivo
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        return file.good();
    }

    std::fstream OpenFile(const std::string &path)
    {
        return std::fstream(path, std::ios::in | std::ios::out | std::ios::binary);
    }

    std::string ToLower(const std::string &str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::string CurrentDateTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto local = *std::localtime(&time);

        std::ostringstream oss;
        oss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    bool FileExists(const std::string &path)
    {
        return std::filesystem::exists(path);
    }

    void CollectInodeBlockIndices(std::fstream &file, const SuperBloque &sb,
                                  const Inodo &ino,
                                  std::vector<int> &dataBlocks,
                                  std::vector<int> *pointerBlocks)
    {
        dataBlocks.clear();
        if (pointerBlocks != nullptr)
            pointerBlocks->clear();

        for (int i = 0; i < 12; i++)
        {
            if (ino.i_block[i] >= 0 && ino.i_block[i] < sb.s_blocks_count)
                dataBlocks.push_back(ino.i_block[i]);
        }

        if (ino.i_block[12] >= 0)
            CollectIndirectBlocks(file, sb, ino.i_block[12], 1, dataBlocks, pointerBlocks);
        if (ino.i_block[13] >= 0)
            CollectIndirectBlocks(file, sb, ino.i_block[13], 2, dataBlocks, pointerBlocks);
        if (ino.i_block[14] >= 0)
            CollectIndirectBlocks(file, sb, ino.i_block[14], 3, dataBlocks, pointerBlocks);
    }

    std::string ReadFileData(std::fstream &file, const SuperBloque &sb, const Inodo &ino)
    {
        std::vector<int> dataBlocks;
        CollectInodeBlockIndices(file, sb, ino, dataBlocks, nullptr);

        int bytesLeft = ino.i_size;
        std::string content;
        content.reserve(std::max(0, ino.i_size));

        for (int blkIdx : dataBlocks)
        {
            if (bytesLeft <= 0)
                break;

            BloqueFile blk{};
            if (!ReadObject(file, blk, sb.s_block_start + blkIdx * (int)sizeof(BloqueFile)))
                break;

            int take = std::min(bytesLeft, (int)sizeof(BloqueFile));
            content.append(blk.b_content, take);
            bytesLeft -= take;
        }

        return content;
    }

}