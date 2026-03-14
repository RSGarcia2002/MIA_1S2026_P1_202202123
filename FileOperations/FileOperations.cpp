// FileOperations: implementa MKDIR, MKFILE y CAT sobre el sistema EXT2 activo.
// Todas las operaciones trabajan sobre la particion de la sesion activa.
#include "FileOperations.h"
#include "../UserSession/UserSession.h"
#include "../DiskManagement/DiskManagement.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <sstream>
#include <fstream>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace FileOperations
{

    // ================================================================
    //  Helpers internos — acceso al disco
    // ================================================================

    // Obtiene inicio y tamanio del area de particion EXT2 segun el MountedPartition
    static bool GetPartBounds(std::fstream &f,
                              const DiskManagement::MountedPartition &mp,
                              int &partStart, int &partSize)
    {
        if (mp.isLogical)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(f, ebr, mp.ebrPos))
                return false;
            partStart = ebr.Start;
            partSize = ebr.Size;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(f, mbr, 0))
                return false;
            if (mp.partIndex < 0 || mp.partIndex >= 4)
                return false;
            partStart = mbr.Partitions[mp.partIndex].Start;
            partSize = mbr.Partitions[mp.partIndex].Size;
        }
        return true;
    }

    // Parte "/home/user/file.txt" en {"home","user","file.txt"}
    static std::vector<std::string> SplitPath(const std::string &path)
    {
        std::vector<std::string> parts;
        std::istringstream ss(path);
        std::string tok;
        while (std::getline(ss, tok, '/'))
            if (!tok.empty())
                parts.push_back(tok);
        return parts;
    }

    static bool ValidatePathExt2(const std::string &path, std::string &err)
    {
        if (path.empty() || path[0] != '/')
        {
            err = "la ruta debe ser absoluta";
            return false;
        }

        auto parts = SplitPath(path);
        if (parts.empty())
        {
            err = "ruta invalida";
            return false;
        }

        for (const auto &p : parts)
        {
            if (p == "." || p == "..")
            {
                err = "la ruta no permite componentes '.' o '..'";
                return false;
            }
            if (p.size() > 12)
            {
                err = "nombre excede 12 caracteres: " + p;
                return false;
            }
        }

        return true;
    }

    // Busca la entrada `name` en el directorio cuyo inodo tiene indice dirInoIdx.
    // Retorna el indice del inodo de la entrada, o -1 si no existe.
    static int FindInDir(std::fstream &f, const SuperBloque &sb,
                         int dirInoIdx, const std::string &name)
    {
        int inoSize = (int)sizeof(Inodo);

        Inodo dino{};
        if (!Utilities::ReadObject(f, dino, sb.s_inode_start + dirInoIdx * inoSize))
            return -1;
        if (dino.i_type != '0')
            return -1;

        std::vector<int> dirBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, dirBlocks, nullptr);

        for (int blkIdx : dirBlocks)
        {
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + blkIdx * (int)sizeof(BloqueDir)))
                continue;
            for (int j = 0; j < 4; j++)
            {
                if (bd.b_content[j].b_inodo == -1)
                    continue;
                if (std::strncmp(bd.b_content[j].b_name, name.c_str(), 12) == 0)
                    return bd.b_content[j].b_inodo;
            }
        }

        return -1;
    }

    static int FindFirstFreeBitmapIndex(std::fstream &f, int bmStart, int count, int hint)
    {
        if (count <= 0)
            return -1;

        if (hint < 0 || hint >= count)
            hint = 0;

        auto scanRange = [&](int from, int to) -> int
        {
            for (int i = from; i < to; i++)
            {
                char bit = 0;
                f.seekg(bmStart + i, std::ios::beg);
                f.read(&bit, 1);
                if (!bit)
                    return i;
            }
            return -1;
        };

        int idx = scanRange(hint, count);
        if (idx != -1)
            return idx;
        return scanRange(0, hint);
    }

    // Aloca el primer inodo libre en el bitmap y actualiza el SuperBloque en disco.
    // Retorna el indice del inodo alocado, o -1 si no hay espacio.
    static int AllocInode(std::fstream &f, SuperBloque &sb, int partStart)
    {
        if (sb.s_free_inodes_count <= 0)
            return -1;

        int idx = FindFirstFreeBitmapIndex(f, sb.s_bm_inode_start, sb.s_inodes_count, sb.s_firts_ino);
        if (idx == -1)
            return -1;

        char bit = 1;
        f.seekp(sb.s_bm_inode_start + idx, std::ios::beg);
        f.write(&bit, 1);

        sb.s_free_inodes_count--;
        sb.s_firts_ino = (idx + 1 < sb.s_inodes_count) ? (idx + 1) : 0;
        Utilities::WriteObject(f, sb, partStart);
        return idx;
    }

    // Aloca el primer bloque libre en el bitmap y actualiza el SuperBloque en disco.
    // Retorna el indice del bloque alocado, o -1 si no hay espacio.
    static int AllocBlock(std::fstream &f, SuperBloque &sb, int partStart)
    {
        if (sb.s_free_blocks_count <= 0)
            return -1;

        int idx = FindFirstFreeBitmapIndex(f, sb.s_bm_block_start, sb.s_blocks_count, sb.s_first_blo);
        if (idx == -1)
            return -1;

        char bit = 1;
        f.seekp(sb.s_bm_block_start + idx, std::ios::beg);
        f.write(&bit, 1);

        sb.s_free_blocks_count--;
        sb.s_first_blo = (idx + 1 < sb.s_blocks_count) ? (idx + 1) : 0;
        Utilities::WriteObject(f, sb, partStart);
        return idx;
    }

    static int CeilDiv(int a, int b)
    {
        return (a + b - 1) / b;
    }

    static void SetName12(char dest[12], const std::string &name)
    {
        std::memset(dest, 0, 12);
        size_t n = std::min<size_t>(11, name.size());
        if (n > 0)
            std::memcpy(dest, name.c_str(), n);
    }

    static int MaxFileDataBlocks()
    {
        return 12 + 16 + (16 * 16) + (16 * 16 * 16);
    }

    static int CountPointerBlocksForDataBlocks(int dataBlockCount)
    {
        int remaining = std::max(0, dataBlockCount - 12);
        int ptrCount = 0;

        int simpleData = std::min(remaining, 16);
        if (simpleData > 0)
            ptrCount += 1;
        remaining -= simpleData;

        int doubleData = std::min(remaining, 16 * 16);
        if (doubleData > 0)
            ptrCount += 1 + CeilDiv(doubleData, 16);
        remaining -= doubleData;

        int tripleData = std::min(remaining, 16 * 16 * 16);
        if (tripleData > 0)
            ptrCount += 1 + CeilDiv(tripleData, 16 * 16) + CeilDiv(tripleData, 16);

        return ptrCount;
    }

    static void InitPointerBlock(BloqueApunt &ap)
    {
        for (int i = 0; i < 16; i++)
            ap.b_pointers[i] = -1;
    }

    static void InitDirBlock(BloqueDir &bd)
    {
        for (int i = 0; i < 4; i++)
        {
            bd.b_content[i].b_inodo = -1;
            std::memset(bd.b_content[i].b_name, 0, sizeof(bd.b_content[i].b_name));
        }
    }

    static bool TryInsertInDirBlock(std::fstream &f, const SuperBloque &sb,
                                    int blkIdx, const std::string &entryName, int newInoIdx)
    {
        BloqueDir bd{};
        int pos = sb.s_block_start + blkIdx * (int)sizeof(BloqueDir);
        if (!Utilities::ReadObject(f, bd, pos))
            return false;

        for (int i = 0; i < 4; i++)
        {
            if (bd.b_content[i].b_inodo != -1)
                continue;
            SetName12(bd.b_content[i].b_name, entryName);
            bd.b_content[i].b_inodo = newInoIdx;
            return Utilities::WriteObject(f, bd, pos);
        }
        return false;
    }

    static int AllocDirDataBlock(std::fstream &f, SuperBloque &sb, int partStart,
                                 const std::string &entryName, int newInoIdx)
    {
        int newBlk = AllocBlock(f, sb, partStart);
        if (newBlk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        BloqueDir bd{};
        InitDirBlock(bd);
        SetName12(bd.b_content[0].b_name, entryName);
        bd.b_content[0].b_inodo = newInoIdx;
        if (!Utilities::WriteObject(f, bd, sb.s_block_start + newBlk * (int)sizeof(BloqueDir)))
            return -1;
        return newBlk;
    }

    static int AllocPointerDataBlock(std::fstream &f, SuperBloque &sb, int partStart)
    {
        int blk = AllocBlock(f, sb, partStart);
        if (blk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        BloqueApunt ap{};
        InitPointerBlock(ap);
        if (!Utilities::WriteObject(f, ap, sb.s_block_start + blk * (int)sizeof(BloqueApunt)))
            return -1;
        return blk;
    }

    static bool InsertDirEntryRecursive(std::fstream &f, SuperBloque &sb, int partStart,
                                        int ptrBlkIdx, int level,
                                        const std::string &entryName, int newInoIdx,
                                        bool &allocatedNewDir)
    {
        if (ptrBlkIdx < 0 || level <= 0)
            return false;

        BloqueApunt ap{};
        int apPos = sb.s_block_start + ptrBlkIdx * (int)sizeof(BloqueApunt);
        if (!Utilities::ReadObject(f, ap, apPos))
            return false;

        // Primero intentar reusar bloques ya enlazados.
        for (int i = 0; i < 16; i++)
        {
            int child = ap.b_pointers[i];
            if (child == -1)
                continue;

            if (level == 1)
            {
                if (TryInsertInDirBlock(f, sb, child, entryName, newInoIdx))
                    return true;
            }
            else
            {
                if (InsertDirEntryRecursive(f, sb, partStart, child, level - 1,
                                            entryName, newInoIdx, allocatedNewDir))
                    return true;
            }
        }

        // Si no hay espacio, intentar enlazar nuevo hijo en un puntero libre.
        for (int i = 0; i < 16; i++)
        {
            if (ap.b_pointers[i] != -1)
                continue;

            if (level == 1)
            {
                int newDirBlk = AllocDirDataBlock(f, sb, partStart, entryName, newInoIdx);
                if (newDirBlk == -1)
                    return false;
                ap.b_pointers[i] = newDirBlk;
                if (!Utilities::WriteObject(f, ap, apPos))
                    return false;
                allocatedNewDir = true;
                return true;
            }

            int newPtrBlk = AllocPointerDataBlock(f, sb, partStart);
            if (newPtrBlk == -1)
                return false;
            ap.b_pointers[i] = newPtrBlk;
            if (!Utilities::WriteObject(f, ap, apPos))
                return false;

            if (InsertDirEntryRecursive(f, sb, partStart, newPtrBlk, level - 1,
                                        entryName, newInoIdx, allocatedNewDir))
                return true;

            return false;
        }

        return false;
    }

    // Agrega la entrada (entryName -> newInoIdx) al directorio cuyo inodo es dirInoIdx.
    // Busca un slot libre en los bloques existentes; si no hay, aloca un nuevo bloque.
    static bool AddDirEntry(std::fstream &f, SuperBloque &sb, int partStart,
                            int dirInoIdx, const std::string &entryName, int newInoIdx)
    {
        int inoSize = (int)sizeof(Inodo);

        Inodo dino{};
        int dinoPos = sb.s_inode_start + dirInoIdx * inoSize;
        if (!Utilities::ReadObject(f, dino, dinoPos))
            return false;

        // Intentar insertar en cualquier bloque ya existente (directo/indirecto).
        std::vector<int> existingBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, existingBlocks, nullptr);
        for (int blkIdx : existingBlocks)
        {
            if (TryInsertInDirBlock(f, sb, blkIdx, entryName, newInoIdx))
                return true;
        }

        // Luego intentar crecer directos.
        for (int i = 0; i < 12; i++)
        {
            if (dino.i_block[i] != -1)
                continue;
            int newBlk = AllocDirDataBlock(f, sb, partStart, entryName, newInoIdx);
            if (newBlk == -1)
                return false;
            dino.i_block[i] = newBlk;
            dino.i_size += (int)sizeof(BloqueDir);
            return Utilities::WriteObject(f, dino, dinoPos);
        }

        // Si directos llenos, crecer por simple/doble/triple.
        for (int idx = 12; idx <= 14; idx++)
        {
            if (dino.i_block[idx] == -1)
            {
                int ptrBlk = AllocPointerDataBlock(f, sb, partStart);
                if (ptrBlk == -1)
                    return false;
                dino.i_block[idx] = ptrBlk;
                if (!Utilities::WriteObject(f, dino, dinoPos))
                    return false;
            }

            bool allocatedNewDir = false;
            if (InsertDirEntryRecursive(f, sb, partStart, dino.i_block[idx], idx - 11,
                                        entryName, newInoIdx, allocatedNewDir))
            {
                if (allocatedNewDir)
                {
                    dino.i_size += (int)sizeof(BloqueDir);
                    Utilities::WriteObject(f, dino, dinoPos);
                }
                return true;
            }
        }

        return false;
    }

    // Lee el contenido completo de un archivo dado su inodo.
    static std::string ReadFileContent(std::fstream &f, const SuperBloque &sb, const Inodo &ino)
    {
        return Utilities::ReadFileData(f, sb, ino);
    }

    // Verifica permiso de lectura (bit 4) para el usuario actual sobre el inodo.
    static bool CanRead(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 4) != 0;
    }

    // Verifica permiso de escritura (bit 2) para el usuario actual sobre el inodo.
    static bool CanWrite(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 2) != 0;
    }

    // Verifica permiso de ejecucion (bit 1), usado para atravesar directorios.
    static bool CanExec(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 1) != 0;
    }

    // Recorre una ruta aplicando permisos de traversal sobre directorios.
    // Retorna -1 si no existe algun componente, -2 en errores de permisos/tipo.
    static int TraversePathWithPerm(std::fstream &f, const SuperBloque &sb,
                                    const std::string &path, bool upToParent,
                                    std::string &lastName,
                                    int uid, int gid,
                                    std::string &err)
    {
        auto parts = SplitPath(path);
        if (parts.empty())
            return 1;

        int curIno = 1;
        int limit = upToParent ? (int)parts.size() - 1 : (int)parts.size();

        for (int i = 0; i < limit; i++)
        {
            Inodo current{};
            if (!Utilities::ReadObject(f, current, sb.s_inode_start + curIno * (int)sizeof(Inodo)))
            {
                err = "no se pudo leer inodo durante traversal";
                return -2;
            }
            if (current.i_type != '0')
            {
                err = "la ruta contiene un archivo intermedio";
                return -2;
            }
            if (!CanExec(current, uid, gid))
            {
                err = "permiso denegado para atravesar directorio";
                return -2;
            }

            int found = FindInDir(f, sb, curIno, parts[i]);
            if (found == -1)
                return -1;
            curIno = found;
        }

        if (upToParent && !parts.empty())
            lastName = parts.back();
        return curIno;
    }

    // Crea el nodo de directorio (inodo + BloqueDir con "." y ".."), retorna inodo index.
    static int CreateDirNode(std::fstream &f, SuperBloque &sb, int partStart,
                             int parentInoIdx, int uid, int gid)
    {
        int newIno = AllocInode(f, sb, partStart);
        if (newIno == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        int newBlk = AllocBlock(f, sb, partStart);
        if (newBlk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        int64_t now = (int64_t)std::time(nullptr);
        Inodo ino{};
        ino.i_uid = uid;
        ino.i_gid = gid;
        ino.i_size = (int32_t)sizeof(BloqueDir);
        ino.i_atime = ino.i_ctime = ino.i_mtime = now;
        for (int k = 0; k < 15; k++)
            ino.i_block[k] = -1;
        ino.i_block[0] = newBlk;
        ino.i_type = '0';
        std::memcpy(ino.i_perm, "664", 3);
        Utilities::WriteObject(f, ino, sb.s_inode_start + newIno * (int)sizeof(Inodo));

        BloqueDir bd{};
        for (int k = 0; k < 4; k++)
        {
            bd.b_content[k].b_inodo = -1;
            bd.b_content[k].b_name[0] = '\0';
        }
        SetName12(bd.b_content[0].b_name, ".");
        bd.b_content[0].b_inodo = newIno;
        SetName12(bd.b_content[1].b_name, "..");
        bd.b_content[1].b_inodo = parentInoIdx;
        Utilities::WriteObject(f, bd, sb.s_block_start + newBlk * (int)sizeof(BloqueDir));

        return newIno;
    }

    // ================================================================
    //  Helpers para obtener la particion activa
    // ================================================================

    static bool GetActivePartition(const DiskManagement::MountedPartition *&mpOut,
                                   std::string &errOut)
    {
        if (!UserSession::IsLoggedIn())
        {
            errOut = "no hay sesion activa";
            return false;
        }
        auto it = DiskManagement::MountMap.find(UserSession::GetCurrentId());
        if (it == DiskManagement::MountMap.end())
        {
            errOut = "particion no encontrada: " + UserSession::GetCurrentId();
            return false;
        }
        mpOut = &it->second;
        return true;
    }

    // ================================================================
    //  MKDIR
    // ================================================================

    std::string Mkdir(const std::string &path, bool parents)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [mkdir]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [mkdir]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [mkdir]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [mkdir]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        auto parts = SplitPath(path);
        if (parts.empty())
        {
            f.close();
            return "Error [mkdir]: ruta invalida";
        }

        std::string pathErr;
        if (!ValidatePathExt2(path, pathErr))
        {
            f.close();
            return "Error [mkdir]: " + pathErr;
        }

        int curIno = 1; // inodo raiz
        for (int i = 0; i < (int)parts.size(); i++)
        {
            Inodo currentIno{};
            if (!Utilities::ReadObject(f, currentIno, sb.s_inode_start + curIno * (int)sizeof(Inodo)))
            {
                f.close();
                return "Error [mkdir]: no se pudo leer inodo del directorio actual";
            }
            if (currentIno.i_type != '0')
            {
                f.close();
                return "Error [mkdir]: la ruta contiene un archivo intermedio";
            }
            if (!CanExec(currentIno, uid, gid))
            {
                f.close();
                return "Error [mkdir]: permiso denegado para atravesar directorio";
            }

            int found = FindInDir(f, sb, curIno, parts[i]);
            if (found == -1)
            {
                // El componente no existe: solo se puede crear si parents=true o es el ultimo
                if (i < (int)parts.size() - 1 && !parents)
                {
                    f.close();
                    return "Error [mkdir]: directorio padre no existe: " + parts[i] + " (usa -p)";
                }

                // Verificar permiso de escritura en el directorio actual
                if (!CanWrite(currentIno, uid, gid) || !CanExec(currentIno, uid, gid))
                {
                    f.close();
                    return "Error [mkdir]: permiso denegado en directorio: /" + parts[i];
                }

                int newIno = CreateDirNode(f, sb, partStart, curIno, uid, gid);
                if (newIno == -1)
                {
                    f.close();
                    return "Error [mkdir]: sin espacio disponible";
                }

                // Re-leer sb luego de las alocaciones
                Utilities::ReadObject(f, sb, partStart);

                if (!AddDirEntry(f, sb, partStart, curIno, parts[i], newIno))
                {
                    f.close();
                    return "Error [mkdir]: no se pudo agregar entrada al directorio padre";
                }
                Utilities::ReadObject(f, sb, partStart);
                curIno = newIno;
            }
            else
            {
                Inodo foundIno{};
                if (!Utilities::ReadObject(f, foundIno, sb.s_inode_start + found * (int)sizeof(Inodo)))
                {
                    f.close();
                    return "Error [mkdir]: no se pudo leer inodo existente";
                }
                if (foundIno.i_type != '0')
                {
                    f.close();
                    return "Error [mkdir]: ya existe un archivo con ese nombre en la ruta";
                }

                if (i == (int)parts.size() - 1 && !parents)
                {
                    f.close();
                    return "Error [mkdir]: el directorio ya existe: " + path;
                }
                curIno = found;
            }
        }

        f.close();
        return "OK [mkdir]: directorio creado: " + path;
    }

    // ================================================================
    //  MKFILE
    // ================================================================

    std::string Mkfile(const std::string &path, bool recursive, int size, const std::string &cont)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [mkfile]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [mkfile]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [mkfile]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [mkfile]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        if (size < 0)
        {
            f.close();
            return "Error [mkfile]: -size no puede ser negativo";
        }

        std::string pathErr;
        if (!ValidatePathExt2(path, pathErr))
        {
            f.close();
            return "Error [mkfile]: " + pathErr;
        }

        // Construir contenido del archivo
        std::string fileContent;
        if (!cont.empty())
        {
            std::ifstream cf(cont, std::ios::binary);
            if (!cf.is_open())
            {
                // Si no es un path valido, usar el valor como contenido inline
                fileContent = cont;
            }
            else
            {
                fileContent.assign((std::istreambuf_iterator<char>(cf)),
                                   std::istreambuf_iterator<char>());
            }
        }
        else if (size > 0)
        {
            fileContent.resize(size);
            for (int i = 0; i < size; i++)
                fileContent[i] = '0' + (i % 10);
        }
        // Si ni cont ni size: archivo queda vacio

        // Obtener directorio padre e identificar el nombre del archivo
        std::string lastName;
        std::string travErr;
        int parentInoIdx = TraversePathWithPerm(f, sb, path, true, lastName, uid, gid, travErr);

        if (parentInoIdx == -2)
        {
            f.close();
            return "Error [mkfile]: " + travErr;
        }

        if (parentInoIdx == -1)
        {
            if (!recursive)
            {
                f.close();
                return "Error [mkfile]: directorio padre no existe (usa -r)";
            }

            // Crear todos los directorios intermedios
            auto parts = SplitPath(path);
            if (parts.empty())
            {
                f.close();
                return "Error [mkfile]: ruta invalida";
            }
            lastName = parts.back();

            int curIno = 1;
            for (int i = 0; i < (int)parts.size() - 1; i++)
            {
                Inodo curInoMeta{};
                if (!Utilities::ReadObject(f, curInoMeta, sb.s_inode_start + curIno * (int)sizeof(Inodo)) ||
                    curInoMeta.i_type != '0')
                {
                    f.close();
                    return "Error [mkfile]: la ruta padre contiene un archivo intermedio";
                }
                if (!CanExec(curInoMeta, uid, gid))
                {
                    f.close();
                    return "Error [mkfile]: permiso denegado para atravesar ruta intermedia";
                }

                int found = FindInDir(f, sb, curIno, parts[i]);
                if (found == -1)
                {
                    if (!CanWrite(curInoMeta, uid, gid) || !CanExec(curInoMeta, uid, gid))
                    {
                        f.close();
                        return "Error [mkfile]: permiso denegado en ruta intermedia";
                    }

                    int newIno = CreateDirNode(f, sb, partStart, curIno, uid, gid);
                    if (newIno == -1)
                    {
                        f.close();
                        return "Error [mkfile]: sin espacio";
                    }
                    Utilities::ReadObject(f, sb, partStart);
                    if (!AddDirEntry(f, sb, partStart, curIno, parts[i], newIno))
                    {
                        f.close();
                        return "Error [mkfile]: no se pudo crear dir: " + parts[i];
                    }
                    Utilities::ReadObject(f, sb, partStart);
                    curIno = newIno;
                }
                else
                {
                    Inodo foundIno{};
                    if (!Utilities::ReadObject(f, foundIno, sb.s_inode_start + found * (int)sizeof(Inodo)) ||
                        foundIno.i_type != '0')
                    {
                        f.close();
                        return "Error [mkfile]: la ruta padre contiene un archivo intermedio";
                    }
                    curIno = found;
                }
            }
            parentInoIdx = curIno;
        }

        // Verificar permiso de escritura en el directorio padre
        Inodo parentIno{};
        Utilities::ReadObject(f, parentIno, sb.s_inode_start + parentInoIdx * (int)sizeof(Inodo));
        if (parentIno.i_type != '0')
        {
            f.close();
            return "Error [mkfile]: el padre indicado no es un directorio";
        }
        if (!CanWrite(parentIno, uid, gid) || !CanExec(parentIno, uid, gid))
        {
            f.close();
            return "Error [mkfile]: permiso denegado en directorio padre";
        }

        // Verificar si ya existe
        if (FindInDir(f, sb, parentInoIdx, lastName) != -1)
        {
            f.close();
            return "Error [mkfile]: el archivo ya existe: " + path;
        }

        // Alocar inodo para el nuevo archivo
        int fileIno = AllocInode(f, sb, partStart);
        if (fileIno == -1)
        {
            f.close();
            return "Error [mkfile]: sin inodos libres";
        }
        Utilities::ReadObject(f, sb, partStart);

        int64_t now = (int64_t)std::time(nullptr);
        int bsize = (int)sizeof(BloqueFile);
        int totalBytes = (int)fileContent.size();
        int dataBlockCount = CeilDiv(totalBytes, bsize);

        if (dataBlockCount > MaxFileDataBlocks())
        {
            f.close();
            return "Error [mkfile]: el archivo excede la capacidad maxima del inodo";
        }

        int pointerBlockCount = CountPointerBlocksForDataBlocks(dataBlockCount);
        if (sb.s_free_blocks_count < dataBlockCount + pointerBlockCount)
        {
            f.close();
            return "Error [mkfile]: sin bloques libres suficientes";
        }

        std::vector<int> dataBlocks;
        dataBlocks.reserve(dataBlockCount);

        for (int blockNo = 0; blockNo < dataBlockCount; blockNo++)
        {
            int blkIdx = AllocBlock(f, sb, partStart);
            if (blkIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueFile bf{};
            std::memset(bf.b_content, 0, bsize);
            int offset = blockNo * bsize;
            int take = std::min(bsize, totalBytes - offset);
            if (take > 0)
                std::memcpy(bf.b_content, fileContent.c_str() + offset, take);
            Utilities::WriteObject(f, bf, sb.s_block_start + blkIdx * bsize);
            dataBlocks.push_back(blkIdx);
        }

        Inodo ino{};
        ino.i_uid = uid;
        ino.i_gid = gid;
        ino.i_size = totalBytes;
        ino.i_atime = ino.i_ctime = ino.i_mtime = now;
        for (int k = 0; k < 15; k++)
            ino.i_block[k] = -1;
        ino.i_type = '1';
        std::memcpy(ino.i_perm, "664", 3);

        int cursor = 0;
        for (int i = 0; i < 12 && cursor < (int)dataBlocks.size(); i++)
            ino.i_block[i] = dataBlocks[cursor++];

        if (cursor < (int)dataBlocks.size())
        {
            int apIdx = AllocBlock(f, sb, partStart);
            if (apIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador simple)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt ap{};
            InitPointerBlock(ap);
            ino.i_block[12] = apIdx;
            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
                ap.b_pointers[i] = dataBlocks[cursor++];
            Utilities::WriteObject(f, ap, sb.s_block_start + apIdx * (int)sizeof(BloqueApunt));
        }

        if (cursor < (int)dataBlocks.size())
        {
            int dblIdx = AllocBlock(f, sb, partStart);
            if (dblIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador doble)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt dbl{};
            InitPointerBlock(dbl);
            ino.i_block[13] = dblIdx;

            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
            {
                int lvl1Idx = AllocBlock(f, sb, partStart);
                if (lvl1Idx == -1)
                {
                    f.close();
                    return "Error [mkfile]: sin bloques libres (nivel doble)";
                }
                Utilities::ReadObject(f, sb, partStart);

                BloqueApunt lvl1{};
                InitPointerBlock(lvl1);
                dbl.b_pointers[i] = lvl1Idx;

                for (int j = 0; j < 16 && cursor < (int)dataBlocks.size(); j++)
                    lvl1.b_pointers[j] = dataBlocks[cursor++];

                Utilities::WriteObject(f, lvl1, sb.s_block_start + lvl1Idx * (int)sizeof(BloqueApunt));
            }

            Utilities::WriteObject(f, dbl, sb.s_block_start + dblIdx * (int)sizeof(BloqueApunt));
        }

        if (cursor < (int)dataBlocks.size())
        {
            int triIdx = AllocBlock(f, sb, partStart);
            if (triIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador triple)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt tri{};
            InitPointerBlock(tri);
            ino.i_block[14] = triIdx;

            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
            {
                int lvl2Idx = AllocBlock(f, sb, partStart);
                if (lvl2Idx == -1)
                {
                    f.close();
                    return "Error [mkfile]: sin bloques libres (nivel triple 1)";
                }
                Utilities::ReadObject(f, sb, partStart);

                BloqueApunt lvl2{};
                InitPointerBlock(lvl2);
                tri.b_pointers[i] = lvl2Idx;

                for (int j = 0; j < 16 && cursor < (int)dataBlocks.size(); j++)
                {
                    int lvl1Idx = AllocBlock(f, sb, partStart);
                    if (lvl1Idx == -1)
                    {
                        f.close();
                        return "Error [mkfile]: sin bloques libres (nivel triple 2)";
                    }
                    Utilities::ReadObject(f, sb, partStart);

                    BloqueApunt lvl1{};
                    InitPointerBlock(lvl1);
                    lvl2.b_pointers[j] = lvl1Idx;

                    for (int k = 0; k < 16 && cursor < (int)dataBlocks.size(); k++)
                        lvl1.b_pointers[k] = dataBlocks[cursor++];

                    Utilities::WriteObject(f, lvl1, sb.s_block_start + lvl1Idx * (int)sizeof(BloqueApunt));
                }

                Utilities::WriteObject(f, lvl2, sb.s_block_start + lvl2Idx * (int)sizeof(BloqueApunt));
            }

            Utilities::WriteObject(f, tri, sb.s_block_start + triIdx * (int)sizeof(BloqueApunt));
        }

        // Escribir el inodo del archivo
        Utilities::WriteObject(f, ino, sb.s_inode_start + fileIno * (int)sizeof(Inodo));

        // Insertar entrada en el directorio padre
        if (!AddDirEntry(f, sb, partStart, parentInoIdx, lastName, fileIno))
        {
            f.close();
            return "Error [mkfile]: no se pudo agregar entrada al directorio padre";
        }

        f.close();
        return "OK [mkfile]: archivo creado: " + path;
    }

    // ================================================================
    //  CAT
    // ================================================================

    std::string Cat(const std::vector<std::string> &files)
    {
        if (files.empty())
            return "Error [cat]: no se especificaron archivos";

        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [cat]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [cat]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [cat]: no se pudo leer limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [cat]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        std::ostringstream output;
        for (const auto &filePath : files)
        {
            std::string pathErr;
            if (!ValidatePathExt2(filePath, pathErr))
            {
                output << "Error [cat]: " << pathErr << ": " << filePath << "\n";
                continue;
            }

            std::string dummy;
            std::string travErr;
            int inoIdx = TraversePathWithPerm(f, sb, filePath, false, dummy, uid, gid, travErr);
            if (inoIdx == -2)
            {
                output << "Error [cat]: " << travErr << ": " << filePath << "\n";
                continue;
            }
            if (inoIdx == -1)
            {
                output << "Error [cat]: archivo no encontrado: " << filePath << "\n";
                continue;
            }

            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
            {
                output << "Error [cat]: no se pudo leer inodo de: " << filePath << "\n";
                continue;
            }

            if (ino.i_type != '1')
            {
                output << "Error [cat]: " << filePath << " es un directorio\n";
                continue;
            }

            if (!CanRead(ino, uid, gid))
            {
                output << "Error [cat]: permiso denegado: " << filePath << "\n";
                continue;
            }

            output << ReadFileContent(f, sb, ino);
        }

        f.close();
        return output.str();
    }

} // namespace FileOperations
