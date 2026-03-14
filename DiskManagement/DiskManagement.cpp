#include "DiskManagement.h"
#include "../Utilities/Utilities.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace DiskManagement
{

    // Ultimos 2 digitos del carnet, se usan para generar los IDs de montaje
    const std::string CARNET_DIGITS = "23";

    // Mapa en RAM de particiones montadas: id → MountedPartition
    std::map<std::string, MountedPartition> MountMap;

    // Cuantas veces se ha montado algo en cada disco (determina el numero en el ID)
    static std::map<std::string, int> s_mountCountPerDisk;

    // Letra asignada a cada disco (el primer disco nuevo recibe A, el segundo B, etc.)
    static std::map<std::string, char> s_diskLetterMap;
    static int s_nextLetterIdx = 0;

    // Convierte un tamaño + unidad a bytes (b, k, m)
    int ToBytes(int size, const std::string &unit)
    {
        if (unit == "k")
            return size * 1024;
        if (unit == "m")
            return size * 1024 * 1024;
        return size;
    }

    // Convierte el string del ajuste a su caracter correspondiente
    char FitChar(const std::string &fit)
    {
        if (fit == "bf")
            return 'B';
        if (fit == "wf")
            return 'W';
        return 'F';
    }

    // Representa un hueco libre entre particiones
    struct Gap
    {
        int start;
        int size;
    };

    // Devuelve todos los huecos libres del disco basados en el MBR actual
    static std::vector<Gap> GetFreeGaps(const MBR &mbr)
    {
        // Se marcan como ocupados el MBR y cada particion existente
        std::vector<std::pair<int, int>> occupied;
        occupied.push_back({0, (int)sizeof(MBR)});

        for (int i = 0; i < 4; i++)
        {
            const Partition &p = mbr.Partitions[i];
            if (p.Start != -1 && p.Size > 0)
            {
                occupied.push_back({p.Start, p.Start + p.Size});
            }
        }

        std::sort(occupied.begin(), occupied.end());

        // Se calculan los espacios entre las zonas ocupadas
        std::vector<Gap> gaps;
        int prev = 0;
        for (auto &[start, end] : occupied)
        {
            if (start > prev)
            {
                gaps.push_back({prev, start - prev});
            }
            prev = std::max(prev, end);
        }
        if (prev < mbr.MbrSize)
        {
            gaps.push_back({prev, mbr.MbrSize - prev});
        }

        return gaps;
    }

    // Algoritmos de ajuste: devuelven el byte de inicio del hueco elegido, o -1 si no hay espacio

    int FirstFit(const MBR &mbr, int requiredSize, int /*diskSize*/)
    {
        for (auto &g : GetFreeGaps(mbr))
        {
            if (g.size >= requiredSize)
                return g.start;
        }
        return -1;
    }

    int BestFit(const MBR &mbr, int requiredSize, int /*diskSize*/)
    {
        int best = -1, bestSize = INT32_MAX;
        for (auto &g : GetFreeGaps(mbr))
        {
            if (g.size >= requiredSize && g.size < bestSize)
            {
                best = g.start;
                bestSize = g.size;
            }
        }
        return best;
    }

    int WorstFit(const MBR &mbr, int requiredSize, int /*diskSize*/)
    {
        int best = -1, bestSize = -1;
        for (auto &g : GetFreeGaps(mbr))
        {
            if (g.size >= requiredSize && g.size > bestSize)
            {
                best = g.start;
                bestSize = g.size;
            }
        }
        return best;
    }

    // Aplica el algoritmo de ajuste segun el caracter indicado
    static int ApplyFit(const MBR &mbr, int requiredSize, char fit)
    {
        if (fit == 'B')
            return BestFit(mbr, requiredSize, mbr.MbrSize);
        if (fit == 'W')
            return WorstFit(mbr, requiredSize, mbr.MbrSize);
        return FirstFit(mbr, requiredSize, mbr.MbrSize);
    }

    static bool FillWithZeros(std::fstream &file, int start, int size)
    {
        if (size <= 0)
            return true;

        file.seekp(start, std::ios::beg);
        if (!file)
            return false;

        char buffer[1024] = {};
        int remaining = size;
        while (remaining > 0)
        {
            int chunk = std::min(remaining, (int)sizeof(buffer));
            file.write(buffer, chunk);
            if (!file)
                return false;
            remaining -= chunk;
        }
        file.flush();
        return true;
    }

    static bool IsPartitionMounted(const std::string &path, const std::string &name)
    {
        for (const auto &[id, mp] : MountMap)
        {
            (void)id;
            if (mp.diskPath == path && mp.name == name)
                return true;
        }
        return false;
    }

    static bool HasMountedLogicalInside(const std::string &path, int extStart, int extSize)
    {
        int extEnd = extStart + extSize;
        for (const auto &[id, mp] : MountMap)
        {
            (void)id;
            if (mp.diskPath == path && mp.isLogical && mp.ebrPos >= extStart && mp.ebrPos < extEnd)
                return true;
        }
        return false;
    }

    static bool FindLogicalPartition(std::fstream &file, const MBR &mbr,
                                     const std::string &name,
                                     int &extIdxOut, int &prevEbrPosOut,
                                     int &ebrPosOut, EBR &ebrOut)
    {
        extIdxOut = -1;
        prevEbrPosOut = -1;
        ebrPosOut = -1;

        for (int i = 0; i < 4; i++)
        {
            if (mbr.Partitions[i].Start == -1 || mbr.Partitions[i].Type != 'E')
                continue;

            int prevPos = -1;
            int curPos = mbr.Partitions[i].Start;
            while (curPos != -1)
            {
                EBR cur{};
                if (!Utilities::ReadObject(file, cur, curPos))
                    break;

                if (cur.Size > 0 && std::string(cur.Name) == name)
                {
                    extIdxOut = i;
                    prevEbrPosOut = prevPos;
                    ebrPosOut = curPos;
                    ebrOut = cur;
                    return true;
                }

                prevPos = curPos;
                curPos = cur.Next;
            }
        }

        return false;
    }

    static int LastLogicalDataEnd(std::fstream &file, const Partition &ext)
    {
        int maxEnd = ext.Start;
        int curPos = ext.Start;
        while (curPos != -1)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(file, ebr, curPos))
                break;
            if (ebr.Size > 0)
                maxEnd = std::max(maxEnd, ebr.Start + ebr.Size);
            curPos = ebr.Next;
        }
        return maxEnd;
    }

    // Crea un disco virtual .mia en el path dado
    std::string Mkdisk(int size, const std::string &path,
                       const std::string &fit, const std::string &unit)
    {
        int totalBytes = ToBytes(size, unit);
        if (totalBytes <= (int)sizeof(MBR))
        {
            return "Error [mkdisk]: el tamanio es demasiado pequenio (minimo para MBR: " + std::to_string(sizeof(MBR)) + " bytes)";
        }

        if (!Utilities::CreateFile(path))
        {
            return "Error [mkdisk]: no se pudo crear el archivo en: " + path;
        }

        // Llenar el archivo con ceros antes de escribir el MBR
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
                return "Error [mkdisk]: no se pudo abrir el archivo para escritura";

            char buffer[1024] = {};
            int remaining = totalBytes;
            while (remaining > 0)
            {
                int chunk = std::min(remaining, 1024);
                f.write(buffer, chunk);
                remaining -= chunk;
            }
        }

        MBR mbr{};
        mbr.MbrSize = totalBytes;
        mbr.Fit = FitChar(fit);
        mbr.Signature = std::rand();

        std::string dt = Utilities::CurrentDateTime();
        std::strncpy(mbr.CreationDate, dt.c_str(), sizeof(mbr.CreationDate) - 1);

        for (int i = 0; i < 4; i++)
        {
            mbr.Partitions[i].Status = '\0';
            mbr.Partitions[i].Type = '\0';
            mbr.Partitions[i].Fit = mbr.Fit;
            mbr.Partitions[i].Start = -1;
            mbr.Partitions[i].Size = -1;
            mbr.Partitions[i].Correlative = -1;
            mbr.Partitions[i].Name[0] = '\0';
            mbr.Partitions[i].Id[0] = '\0';
        }

        std::fstream file = Utilities::OpenFile(path);
        if (!file.is_open())
        {
            return "Error [mkdisk]: no se pudo abrir el archivo para escribir MBR";
        }

        if (!Utilities::WriteObject(file, mbr, 0))
        {
            return "Error [mkdisk]: no se pudo escribir el MBR";
        }

        file.close();

        std::ostringstream oss;
        oss << "MKDISK exitoso\n"
            << "   Path:  " << path << "\n"
            << "   Size:  " << totalBytes << " bytes (" << size << " " << unit << ")\n"
            << "   Fit:   " << FitChar(fit) << "\n"
            << "   Fecha: " << dt;
        return oss.str();
    }

    // Elimina un disco virtual del sistema de archivos
    std::string Rmdisk(const std::string &path)
    {
        if (!Utilities::FileExists(path))
        {
            return "Error [rmdisk]: el archivo no existe: " + path;
        }

        // No se puede borrar si tiene particiones montadas
        for (auto &[id, mp] : MountMap)
        {
            if (mp.diskPath == path)
            {
                return "Error [rmdisk]: el disco tiene particiones montadas (id: " + id + "). Desmonta primero.";
            }
        }

        std::filesystem::remove(path);
        return "RMDISK: disco eliminado -> " + path;
    }

    // Crea una particion en el disco. Solo primarias y extendidas por ahora (logicas en Sprint 2)
    std::string Fdisk(int size, const std::string &path,
                      const std::string &name,
                      const std::string &type,
                      const std::string &fit,
                      const std::string &unit,
                      const std::string &deleteMode,
                      int addValue)
    {
        if (!Utilities::FileExists(path))
        {
            return "Error [fdisk]: disco no encontrado: " + path;
        }

        std::fstream file = Utilities::OpenFile(path);
        if (!file.is_open())
        {
            return "Error [fdisk]: no se pudo abrir el disco";
        }

        MBR mbr{};
        if (!Utilities::ReadObject(file, mbr, 0))
        {
            return "Error [fdisk]: no se pudo leer el MBR";
        }

        if (!deleteMode.empty() && addValue != 0)
            return "Error [fdisk]: no se puede usar -delete y -add al mismo tiempo";

        // Operacion: eliminar particion
        if (!deleteMode.empty())
        {
            if (deleteMode != "fast" && deleteMode != "full")
                return "Error [fdisk]: -delete solo acepta fast o full";

            for (int i = 0; i < 4; i++)
            {
                Partition &p = mbr.Partitions[i];
                if (p.Start == -1 || std::string(p.Name) != name)
                    continue;

                if (IsPartitionMounted(path, name))
                    return "Error [fdisk]: la particion esta montada y no puede eliminarse";
                if (p.Type == 'E' && HasMountedLogicalInside(path, p.Start, p.Size))
                    return "Error [fdisk]: hay particiones logicas montadas dentro de la extendida";

                if (deleteMode == "full" && !FillWithZeros(file, p.Start, p.Size))
                    return "Error [fdisk]: no se pudo limpiar el espacio de la particion";

                p.Status = '\0';
                p.Type = '\0';
                p.Fit = mbr.Fit;
                p.Start = -1;
                p.Size = -1;
                p.Correlative = -1;
                p.Name[0] = '\0';
                p.Id[0] = '\0';

                if (!Utilities::WriteObject(file, mbr, 0))
                    return "Error [fdisk]: no se pudo escribir el MBR actualizado";

                file.close();
                return "FDISK: particion eliminada -> " + name;
            }

            int extIdx = -1, prevEbrPos = -1, ebrPos = -1;
            EBR ebr{};
            if (FindLogicalPartition(file, mbr, name, extIdx, prevEbrPos, ebrPos, ebr))
            {
                if (IsPartitionMounted(path, name))
                    return "Error [fdisk]: la particion esta montada y no puede eliminarse";

                if (prevEbrPos == -1)
                {
                    if (ebr.Next == -1)
                    {
                        EBR empty{};
                        empty.Mount = '0';
                        empty.Fit = ebr.Fit;
                        empty.Start = -1;
                        empty.Size = -1;
                        empty.Next = -1;
                        empty.Name[0] = '\0';
                        Utilities::WriteObject(file, empty, ebrPos);
                    }
                    else
                    {
                        EBR nextEbr{};
                        if (!Utilities::ReadObject(file, nextEbr, ebr.Next))
                            return "Error [fdisk]: no se pudo leer el siguiente EBR";
                        Utilities::WriteObject(file, nextEbr, ebrPos);
                        if (deleteMode == "full")
                            FillWithZeros(file, ebr.Next, (int)sizeof(EBR));
                    }
                }
                else
                {
                    EBR prev{};
                    if (!Utilities::ReadObject(file, prev, prevEbrPos))
                        return "Error [fdisk]: no se pudo leer el EBR previo";
                    prev.Next = ebr.Next;
                    Utilities::WriteObject(file, prev, prevEbrPos);
                    if (deleteMode == "full")
                        FillWithZeros(file, ebrPos, (int)sizeof(EBR));
                }

                if (deleteMode == "full")
                    FillWithZeros(file, ebr.Start, ebr.Size);

                file.close();
                return "FDISK: particion logica eliminada -> " + name;
            }

            return "Error [fdisk]: no existe una particion con nombre '" + name + "'";
        }

        // Operacion: ajustar tamanio de particion existente
        if (addValue != 0)
        {
            int delta = ToBytes(std::abs(addValue), unit);
            if (delta <= 0)
                return "Error [fdisk]: tamanio invalido en -add";
            if (addValue < 0)
                delta = -delta;

            for (int i = 0; i < 4; i++)
            {
                Partition &p = mbr.Partitions[i];
                if (p.Start == -1 || std::string(p.Name) != name)
                    continue;

                int newSize = p.Size + delta;
                if (newSize <= 0)
                    return "Error [fdisk]: el ajuste deja tamanio invalido";

                if (delta > 0)
                {
                    int nextStart = mbr.MbrSize;
                    for (int j = 0; j < 4; j++)
                    {
                        if (j == i || mbr.Partitions[j].Start == -1)
                            continue;
                        if (mbr.Partitions[j].Start > p.Start)
                            nextStart = std::min(nextStart, (int)mbr.Partitions[j].Start);
                    }
                    if (p.Start + newSize > nextStart)
                        return "Error [fdisk]: no hay espacio contiguo suficiente para ampliar la particion";
                }
                else if (p.Type == 'E')
                {
                    int lastLogicalEnd = LastLogicalDataEnd(file, p);
                    if (p.Start + newSize < lastLogicalEnd)
                        return "Error [fdisk]: no se puede reducir la extendida porque contiene logicas al final";
                }

                p.Size = newSize;
                if (!Utilities::WriteObject(file, mbr, 0))
                    return "Error [fdisk]: no se pudo escribir el MBR actualizado";

                file.close();
                std::ostringstream oss;
                oss << "FDISK: particion ajustada\n"
                    << "   Particion: " << name << "\n"
                    << "   Nuevo size: " << newSize << " bytes";
                return oss.str();
            }

            int extIdx = -1, prevEbrPos = -1, ebrPos = -1;
            EBR ebr{};
            if (FindLogicalPartition(file, mbr, name, extIdx, prevEbrPos, ebrPos, ebr))
            {
                int newSize = ebr.Size + delta;
                if (newSize <= 0)
                    return "Error [fdisk]: el ajuste deja tamanio invalido";

                const Partition &ext = mbr.Partitions[extIdx];
                if (delta > 0)
                {
                    int nextBoundary = (ebr.Next != -1) ? ebr.Next : (ext.Start + ext.Size);
                    if (ebr.Start + newSize > nextBoundary)
                        return "Error [fdisk]: no hay espacio contiguo suficiente para ampliar la logica";
                }

                ebr.Size = newSize;
                if (!Utilities::WriteObject(file, ebr, ebrPos))
                    return "Error [fdisk]: no se pudo escribir el EBR actualizado";

                file.close();
                std::ostringstream oss;
                oss << "FDISK: particion logica ajustada\n"
                    << "   Particion: " << name << "\n"
                    << "   Nuevo size: " << newSize << " bytes";
                return oss.str();
            }

            return "Error [fdisk]: no existe una particion con nombre '" + name + "'";
        }

        char typeChar = 'P';
        if (type == "e")
            typeChar = 'E';
        else if (type == "l")
            typeChar = 'L';

        // Partición Lógica
        if (typeChar == 'L')
        {
            // Encontrar la particion extendida
            int extIdx = -1;
            for (int i = 0; i < 4; i++)
                if (mbr.Partitions[i].Start != -1 && mbr.Partitions[i].Type == 'E')
                {
                    extIdx = i;
                    break;
                }

            if (extIdx == -1)
                return "Error [fdisk]: no existe particion extendida en este disco";

            const Partition &ext = mbr.Partitions[extIdx];
            int sizeBytes = ToBytes(size, unit);
            if (sizeBytes <= 0)
                return "Error [fdisk]: tamanio invalido";

            char fitChar = fit.empty() ? mbr.Fit : FitChar(fit);

            // Verificar nombre duplicado en la cadena de EBRs
            {
                int pos = ext.Start;
                while (pos != -1)
                {
                    EBR tmpEbr{};
                    if (!Utilities::ReadObject(file, tmpEbr, pos))
                        break;
                    if (tmpEbr.Size > 0 && std::string(tmpEbr.Name) == name)
                        return "Error [fdisk]: ya existe una particion logica con nombre '" + name + "'";
                    pos = tmpEbr.Next;
                }
            }

            // Leer el primer EBR de la extendida
            EBR firstEbr{};
            if (!Utilities::ReadObject(file, firstEbr, ext.Start))
                return "Error [fdisk]: no se pudo leer el EBR inicial";

            // Si el primer EBR esta vacio (no tiene particion asignada todavia)
            if (firstEbr.Size == -1 || firstEbr.Size == 0)
            {
                int dataStart = ext.Start + (int)sizeof(EBR);
                if ((int)sizeof(EBR) + sizeBytes > ext.Size)
                    return "Error [fdisk]: no hay espacio en la particion extendida";

                EBR newEbr{};
                newEbr.Mount = '0';
                newEbr.Fit = fitChar;
                newEbr.Start = dataStart;
                newEbr.Size = sizeBytes;
                newEbr.Next = -1;
                std::strncpy(newEbr.Name, name.c_str(), sizeof(newEbr.Name) - 1);

                if (!Utilities::WriteObject(file, newEbr, ext.Start))
                    return "Error [fdisk]: no se pudo escribir el EBR";

                file.close();
                std::ostringstream oss;
                oss << "FDISK (logica) exitoso\n"
                    << "   Particion: " << name << " [L]\n"
                    << "   EBR en:    " << ext.Start << "\n"
                    << "   Datos en:  " << dataStart << "\n"
                    << "   Tamanio:   " << sizeBytes << " bytes";
                return oss.str();
            }

            // Caminar la cadena hasta el ultimo EBR
            int lastEbrPos = ext.Start;
            EBR lastEbr = firstEbr;
            while (lastEbr.Next != -1)
            {
                lastEbrPos = lastEbr.Next;
                if (!Utilities::ReadObject(file, lastEbr, lastEbrPos))
                    break;
            }

            // El nuevo EBR va justo despues del area de datos del ultimo EBR
            int newEbrPos = lastEbrPos + (int)sizeof(EBR) + lastEbr.Size;
            int newDataStart = newEbrPos + (int)sizeof(EBR);

            // Verificar que cabe dentro de la extendida
            if (newEbrPos + (int)sizeof(EBR) + sizeBytes > ext.Start + ext.Size)
                return "Error [fdisk]: no hay espacio en la particion extendida";

            // Enlazar el ultimo EBR con el nuevo
            lastEbr.Next = newEbrPos;
            Utilities::WriteObject(file, lastEbr, lastEbrPos);

            EBR newEbr{};
            newEbr.Mount = '0';
            newEbr.Fit = fitChar;
            newEbr.Start = newDataStart;
            newEbr.Size = sizeBytes;
            newEbr.Next = -1;
            std::strncpy(newEbr.Name, name.c_str(), sizeof(newEbr.Name) - 1);

            if (!Utilities::WriteObject(file, newEbr, newEbrPos))
                return "Error [fdisk]: no se pudo escribir el nuevo EBR";

            file.close();
            std::ostringstream oss;
            oss << "FDISK (logica) exitoso\n"
                << "   Particion: " << name << " [L]\n"
                << "   EBR en:    " << newEbrPos << "\n"
                << "   Datos en:  " << newDataStart << "\n"
                << "   Tamanio:   " << sizeBytes << " bytes";
            return oss.str();
        }

        // Partición Primaria / Extendida

        // Verificar nombre duplicado en MBR
        for (int i = 0; i < 4; i++)
        {
            if (mbr.Partitions[i].Start != -1 &&
                std::string(mbr.Partitions[i].Name) == name)
            {
                return "Error [fdisk]: ya existe una particion con el nombre '" + name + "'";
            }
        }

        int usedSlots = 0;
        int extendedIdx = -1;
        int freeSlot = -1;

        for (int i = 0; i < 4; i++)
        {
            if (mbr.Partitions[i].Start == -1)
            {
                if (freeSlot == -1)
                    freeSlot = i;
            }
            else
            {
                usedSlots++;
                if (mbr.Partitions[i].Type == 'E')
                    extendedIdx = i;
            }
        }
        (void)usedSlots;

        if (freeSlot == -1)
            return "Error [fdisk]: el disco ya tiene 4 particiones (maximo permitido)";
        if (typeChar == 'E' && extendedIdx != -1)
            return "Error [fdisk]: ya existe una particion extendida en este disco";

        int sizeBytes = ToBytes(size, unit);
        if (sizeBytes <= 0)
            return "Error [fdisk]: tamanio invalido";

        char fitChar = fit.empty() ? mbr.Fit : FitChar(fit);

        int startByte = ApplyFit(mbr, sizeBytes, fitChar);
        if (startByte == -1)
            return "Error [fdisk]: no hay espacio suficiente en el disco para " + std::to_string(sizeBytes) + " bytes";

        Partition &p = mbr.Partitions[freeSlot];
        p.Status = '0';
        p.Type = typeChar;
        p.Fit = fitChar;
        p.Start = startByte;
        p.Size = sizeBytes;
        p.Correlative = -1;
        p.Id[0] = '\0';
        std::strncpy(p.Name, name.c_str(), sizeof(p.Name) - 1);
        p.Name[sizeof(p.Name) - 1] = '\0';

        if (!Utilities::WriteObject(file, mbr, 0))
            return "Error [fdisk]: no se pudo escribir el MBR actualizado";

        // Para particiones extendidas: escribir un EBR vacio al inicio del area
        if (typeChar == 'E')
        {
            EBR initEbr{};
            initEbr.Mount = '0';
            initEbr.Fit = fitChar;
            initEbr.Start = -1;
            initEbr.Size = -1;
            initEbr.Next = -1;
            initEbr.Name[0] = '\0';
            Utilities::WriteObject(file, initEbr, startByte);
        }

        file.close();

        std::ostringstream oss;
        oss << "FDISK exitoso\n"
            << "   Particion: " << name << " [" << typeChar << "]\n"
            << "   Inicio:    " << startByte << " bytes\n"
            << "   Tamanio:   " << sizeBytes << " bytes\n"
            << "   Ajuste:    " << fitChar;
        return oss.str();
    }

    // Monta una particion del disco y le asigna un ID unico basado en el carnet
    std::string Mount(const std::string &path, const std::string &name)
    {
        if (!Utilities::FileExists(path))
        {
            return "Error [mount]: disco no encontrado: " + path;
        }

        std::fstream file = Utilities::OpenFile(path);
        if (!file.is_open())
        {
            return "Error [mount]: no se pudo abrir el disco";
        }

        MBR mbr{};
        if (!Utilities::ReadObject(file, mbr, 0))
        {
            return "Error [mount]: no se pudo leer el MBR";
        }

        // No montar si ya esta montada
        for (auto &[existId, mp] : MountMap)
        {
            if (mp.diskPath == path && mp.name == name)
                return "Error [mount]: la particion ya esta montada con id: " + existId;
        }

        // Asignar letra al disco si es la primera vez que se monta algo de el
        if (s_diskLetterMap.find(path) == s_diskLetterMap.end())
        {
            s_diskLetterMap[path] = (char)('A' + (s_nextLetterIdx % 26));
            s_nextLetterIdx++;
        }
        char letter = s_diskLetterMap[path];

        // Buscar primero en particiones primarias/extendidas (MBR)
        int foundIdx = -1;
        for (int i = 0; i < 4; i++)
        {
            if (mbr.Partitions[i].Start != -1 &&
                std::string(mbr.Partitions[i].Name) == name)
            {
                foundIdx = i;
                break;
            }
        }

        if (foundIdx != -1)
        {
            int diskMountNum = ++s_mountCountPerDisk[path];
            std::string id = CARNET_DIGITS + std::to_string(diskMountNum) + letter;

            // Particion primaria o extendida
            MountMap[id] = {path, name, id, foundIdx, false, -1};

            mbr.Partitions[foundIdx].Status = '1';
            mbr.Partitions[foundIdx].Correlative = diskMountNum;
            std::strncpy(mbr.Partitions[foundIdx].Id, id.c_str(),
                         sizeof(mbr.Partitions[foundIdx].Id) - 1);
            mbr.Partitions[foundIdx].Id[sizeof(mbr.Partitions[foundIdx].Id) - 1] = '\0';
            Utilities::WriteObject(file, mbr, 0);
            file.close();

            std::ostringstream oss;
            oss << "MOUNT exitoso\n"
                << "   Particion: " << name << "\n"
                << "   Disco:     " << path << "\n"
                << "   ID:        " << id;
            return oss.str();
        }

        // Buscar en particiones logicas (cadena de EBRs dentro de la extendida)
        for (int i = 0; i < 4; i++)
        {
            if (mbr.Partitions[i].Start == -1 || mbr.Partitions[i].Type != 'E')
                continue;

            int ebrPos = mbr.Partitions[i].Start;
            while (ebrPos != -1)
            {
                EBR ebr{};
                if (!Utilities::ReadObject(file, ebr, ebrPos))
                    break;

                if (ebr.Size > 0 && std::string(ebr.Name) == name)
                {
                    int diskMountNum = ++s_mountCountPerDisk[path];
                    std::string id = CARNET_DIGITS + std::to_string(diskMountNum) + letter;

                    // Encontrada como logica
                    MountMap[id] = {path, name, id, -1, true, ebrPos};

                    ebr.Mount = '1';
                    Utilities::WriteObject(file, ebr, ebrPos);
                    file.close();

                    std::ostringstream oss;
                    oss << "MOUNT exitoso (logica)\n"
                        << "   Particion: " << name << "\n"
                        << "   Disco:     " << path << "\n"
                        << "   EBR pos:   " << ebrPos << "\n"
                        << "   ID:        " << id;
                    return oss.str();
                }
                ebrPos = ebr.Next;
            }
        }

        file.close();
        return "Error [mount]: no existe la particion '" + name + "' en " + path;
    }

    // Lista todas las particiones actualmente montadas en RAM
    std::string Mounted()
    {
        if (MountMap.empty())
        {
            return "No hay particiones montadas actualmente.";
        }

        std::ostringstream oss;
        oss << "Particiones montadas:\n";
        oss << std::left
            << std::setw(8) << "ID"
            << std::setw(20) << "Nombre"
            << "Disco\n";
        oss << std::string(60, '-') << "\n";

        for (auto &[id, mp] : MountMap)
        {
            oss << std::left
                << std::setw(8) << id
                << std::setw(20) << mp.name
                << mp.diskPath << "\n";
        }
        return oss.str();
    }

} // namespace DiskManagement