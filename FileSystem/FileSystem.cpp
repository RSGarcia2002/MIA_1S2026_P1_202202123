// FileSystem es el modulo encargado de formatear particiones con el sistema de archivos EXT2
#include "FileSystem.h"
#include "../DiskManagement/DiskManagement.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <fstream>

namespace FileSystem
{

    // Calcula n (numero de inodos = numero de unidades del FS).
    // Layout por unidad: 1 byte bitmap inodo + 3 bytes bitmap bloque
    //                   + sizeof(Inodo) + 3*sizeof(BloqueFile)
    // El SuperBloque se descuenta del espacio disponible.
    // Formula: n = floor((partSize - sizeof(SB)) / (4 + sizeof(Inodo) + 3*sizeof(Block)))
    static int CalcN(int partSize)
    {
        int denom = 4 + (int)sizeof(Inodo) + 3 * (int)sizeof(BloqueFile);
        int avail = partSize - (int)sizeof(SuperBloque);
        if (avail <= 0 || denom <= 0)
            return 0;
        return avail / denom;
    }

    // Escribe exactamente `count` bytes de valor `val` en la posicion `pos` del archivo
    static bool FillBytes(std::fstream &f, int pos, int count, char val = '\0')
    {
        f.seekp(pos, std::ios::beg);
        if (!f)
            return false;
        char buf[1024];
        std::memset(buf, val, sizeof(buf));
        int rem = count;
        while (rem > 0)
        {
            int chunk = std::min(rem, (int)sizeof(buf));
            f.write(buf, chunk);
            if (!f)
                return false;
            rem -= chunk;
        }
        return true;
    }

    std::string Mkfs(const std::string &id, const std::string &type)
    {
        // Buscar la particion en el mapa de montajes
        auto it = DiskManagement::MountMap.find(id);
        if (it == DiskManagement::MountMap.end())
            return "Error [mkfs]: id no encontrado en particiones montadas: " + id;

        const DiskManagement::MountedPartition &mp = it->second;

        std::fstream file = Utilities::OpenFile(mp.diskPath);
        if (!file.is_open())
            return "Error [mkfs]: no se pudo abrir el disco: " + mp.diskPath;

        // Determinar inicio y tamanio del area de la particion
        int partStart = 0, partSize = 0;

        if (mp.isLogical)
        {
            // Para particion logica: leer su EBR
            EBR ebr{};
            if (!Utilities::ReadObject(file, ebr, mp.ebrPos))
            {
                file.close();
                return "Error [mkfs]: no se pudo leer el EBR de la particion logica";
            }
            partStart = ebr.Start; // inicio de los datos (despues del EBR)
            partSize = ebr.Size;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(file, mbr, 0))
            {
                file.close();
                return "Error [mkfs]: no se pudo leer el MBR";
            }
            if (mp.partIndex < 0 || mp.partIndex >= 4)
            {
                file.close();
                return "Error [mkfs]: indice de particion invalido";
            }
            partStart = mbr.Partitions[mp.partIndex].Start;
            partSize = mbr.Partitions[mp.partIndex].Size;
        }

        int n = CalcN(partSize);
        if (n < 2)
        {
            file.close();
            return "Error [mkfs]: particion demasiado pequenia (n=" + std::to_string(n) + ")";
        }

        // Offsets absolutos dentro del archivo .mia
        int bmInodeStart = partStart + (int)sizeof(SuperBloque);
        int bmBlockStart = bmInodeStart + n;
        int inodeStart = bmBlockStart + 3 * n;
        int blockStart = inodeStart + n * (int)sizeof(Inodo);

        // Tipo de filesystem: 2 = EXT2, 3 = EXT3
        int fsType = (type == "fast") ? 3 : 2;

        // Construir SuperBloque
        SuperBloque sb{};
        sb.s_filesystem_type = fsType;
        sb.s_inodes_count = n;
        sb.s_blocks_count = 3 * n;
        sb.s_free_inodes_count = n - 2;     // inodos 0 y 1 usados
        sb.s_free_blocks_count = 3 * n - 2; // bloques 0 y 1 usados
        sb.s_mtime = (int64_t)std::time(nullptr);
        sb.s_umtime = 0;
        sb.s_mnt_count = 1;
        sb.s_magic = 0xEF53;
        sb.s_inode_s = (int32_t)sizeof(Inodo);
        sb.s_block_s = (int32_t)sizeof(BloqueFile);
        sb.s_firts_ino = 2; // proximo inodo libre
        sb.s_first_blo = 2; // proximo bloque libre
        sb.s_bm_inode_start = bmInodeStart;
        sb.s_bm_block_start = bmBlockStart;
        sb.s_inode_start = inodeStart;
        sb.s_block_start = blockStart;

        // Escribir bitmaps a cero
        FillBytes(file, bmInodeStart, n, '\0');
        FillBytes(file, bmBlockStart, 3 * n, '\0');

        // Marcar inodos 0 y 1 como usados en el bitmap
        char one = 1;
        file.seekp(bmInodeStart, std::ios::beg);
        file.write(&one, 1);
        file.seekp(bmInodeStart + 1, std::ios::beg);
        file.write(&one, 1);
        // Marcar bloques 0 y 1 como usados en el bitmap
        file.seekp(bmBlockStart, std::ios::beg);
        file.write(&one, 1);
        file.seekp(bmBlockStart + 1, std::ios::beg);
        file.write(&one, 1);

        // Contenido de users.txt
        // Formato: "uid,tipo,nombre" para grupos y "uid,tipo,nombre,grupo,pass" para usuarios
        const std::string usersContent = "1,G,root\n1,U,root,root,123\n";

        // Inodo 0 = users.txt (archivo)
        int64_t now = (int64_t)std::time(nullptr);
        Inodo inodo0{};
        inodo0.i_uid = 1;
        inodo0.i_gid = 1;
        inodo0.i_size = (int32_t)usersContent.size();
        inodo0.i_atime = now;
        inodo0.i_ctime = now;
        inodo0.i_mtime = now;
        for (int k = 0; k < 15; k++)
            inodo0.i_block[k] = -1;
        inodo0.i_block[0] = 0; // bloque 0 contiene el texto
        inodo0.i_type = '1';   // '1' = archivo
        std::memcpy(inodo0.i_perm, "664", 3);

        // Inodo 1 = directorio raiz '/'
        Inodo inodo1{};
        inodo1.i_uid = 1;
        inodo1.i_gid = 1;
        inodo1.i_size = (int32_t)sizeof(BloqueDir);
        inodo1.i_atime = now;
        inodo1.i_ctime = now;
        inodo1.i_mtime = now;
        for (int k = 0; k < 15; k++)
            inodo1.i_block[k] = -1;
        inodo1.i_block[0] = 1; // bloque 1 = entradas del directorio raiz
        inodo1.i_type = '0';   // '0' = carpeta
        std::memcpy(inodo1.i_perm, "755", 3);

        Utilities::WriteObject(file, inodo0, inodeStart);
        Utilities::WriteObject(file, inodo1, inodeStart + (int)sizeof(Inodo));

        // Bloque 0 = contenido de users.txt (BloqueFile)
        BloqueFile bfile{};
        std::memset(bfile.b_content, '\0', sizeof(bfile.b_content));
        std::memcpy(bfile.b_content, usersContent.c_str(),
                    std::min((int)usersContent.size(), (int)sizeof(bfile.b_content)));
        Utilities::WriteObject(file, bfile, blockStart);

        // Bloque 1 = entradas del directorio raiz (BloqueDir)
        BloqueDir bdir{};
        // Inicializar todas las entradas como vacias
        for (int k = 0; k < 4; k++)
        {
            bdir.b_content[k].b_name[0] = '\0';
            bdir.b_content[k].b_inodo = -1;
        }
        // Entrada "." -> inodo 1 (propio directorio)
        std::strncpy(bdir.b_content[0].b_name, ".", 12);
        bdir.b_content[0].b_inodo = 1;
        // Entrada ".." -> inodo 1 (padre = raiz para root)
        std::strncpy(bdir.b_content[1].b_name, "..", 12);
        bdir.b_content[1].b_inodo = 1;
        // Entrada "users.txt" -> inodo 0
        std::strncpy(bdir.b_content[2].b_name, "users.txt", 12);
        bdir.b_content[2].b_inodo = 0;
        Utilities::WriteObject(file, bdir, blockStart + (int)sizeof(BloqueFile));

        // Escribir SuperBloque al inicio de la particion
        Utilities::WriteObject(file, sb, partStart);

        file.close();

        std::ostringstream out;
        out << "MKFS exitoso\n"
            << "   Tipo FS:    EXT" << fsType << "\n"
            << "   n (inodos): " << n << "\n"
            << "   Bloques:    " << 3 * n << "\n"
            << "   SB inicio:  " << partStart << "\n"
            << "   BM inodos:  " << bmInodeStart << "\n"
            << "   BM bloques: " << bmBlockStart << "\n"
            << "   Inodos:     " << inodeStart << "\n"
            << "   Bloques tbl:" << blockStart << "\n"
            << "   users.txt creado con root:root:123";
        return out.str();
    }

} // namespace FileSystem
