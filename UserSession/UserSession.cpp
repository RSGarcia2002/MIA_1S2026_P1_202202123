#include "UserSession.h"
#include "../DiskManagement/DiskManagement.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <sstream>
#include <vector>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace UserSession
{
    // ---------- Estado de sesion ----------
    struct Session
    {
        bool active = false;
        std::string user;
        std::string partId;
        int uid = 0;
        int gid = 0;
    };

    static Session s_session;

    bool IsLoggedIn() { return s_session.active; }
    std::string GetCurrentUser() { return s_session.user; }
    std::string GetCurrentId() { return s_session.partId; }
    int GetCurrentUid() { return s_session.uid; }
    int GetCurrentGid() { return s_session.gid; }

    // ---------- Helpers de disco ----------

    // Obtiene el byte de inicio del area EXT2 de la particion montada
    static bool GetPartStart(const DiskManagement::MountedPartition &mp, int &partStart)
    {
        std::fstream f = Utilities::OpenFile(mp.diskPath);
        if (!f.is_open())
            return false;

        if (mp.isLogical)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(f, ebr, mp.ebrPos))
            {
                f.close();
                return false;
            }
            partStart = ebr.Start;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(f, mbr, 0))
            {
                f.close();
                return false;
            }
            if (mp.partIndex < 0 || mp.partIndex >= 4)
            {
                f.close();
                return false;
            }
            partStart = mbr.Partitions[mp.partIndex].Start;
        }
        f.close();
        return true;
    }

    // Lee el contenido completo de users.txt (siempre inodo 0) del FS
    static std::string ReadUsersFile(const DiskManagement::MountedPartition &mp)
    {
        int partStart = 0;
        if (!GetPartStart(mp, partStart))
            return "";

        std::fstream f = Utilities::OpenFile(mp.diskPath);
        if (!f.is_open())
            return "";

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart))
        {
            f.close();
            return "";
        }
        if (sb.s_magic != 0xEF53)
        {
            f.close();
            return "";
        }

        // Inodo 0 = users.txt
        Inodo ino{};
        int inodePos = sb.s_inode_start;
        if (!Utilities::ReadObject(f, ino, inodePos))
        {
            f.close();
            return "";
        }

        std::string content = Utilities::ReadFileData(f, sb, ino);
        f.close();
        return content;
    }

    // Escribe el contenido de users.txt al disco, allocando bloques si es necesario
    static bool WriteUsersFile(const DiskManagement::MountedPartition &mp, const std::string &content)
    {
        int partStart = 0;
        if (!GetPartStart(mp, partStart))
            return false;

        std::fstream f = Utilities::OpenFile(mp.diskPath);
        if (!f.is_open())
            return false;

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart))
        {
            f.close();
            return false;
        }
        if (sb.s_magic != 0xEF53)
        {
            f.close();
            return false;
        }

        Inodo ino{};
        int inodePos = sb.s_inode_start;
        if (!Utilities::ReadObject(f, ino, inodePos))
        {
            f.close();
            return false;
        }

        int bsize = (int)sizeof(BloqueFile);
        int totalBytes = (int)content.size();
        int offset = 0;

        // Aloca el primer bloque libre en el bitmap de bloques
        auto allocBlock = [&]() -> int
        {
            for (int b = 0; b < sb.s_blocks_count; b++)
            {
                char bit = 0;
                f.seekg(sb.s_bm_block_start + b, std::ios::beg);
                f.read(&bit, 1);
                if (!bit)
                {
                    bit = 1;
                    f.seekp(sb.s_bm_block_start + b, std::ios::beg);
                    f.write(&bit, 1);
                    sb.s_free_blocks_count--;
                    sb.s_first_blo = b + 1;
                    return b;
                }
            }
            return -1;
        };

        // Bloques directos i_block[0..11]
        for (int i = 0; i < 12 && offset < totalBytes; i++)
        {
            if (ino.i_block[i] == -1)
            {
                int nb = allocBlock();
                if (nb == -1)
                {
                    f.close();
                    return false;
                }
                ino.i_block[i] = nb;
            }
            BloqueFile blk{};
            std::memset(blk.b_content, '\0', bsize);
            int take = std::min(totalBytes - offset, bsize);
            std::memcpy(blk.b_content, content.c_str() + offset, take);
            Utilities::WriteObject(f, blk, sb.s_block_start + ino.i_block[i] * bsize);
            offset += take;
        }

        // Bloque simple indirecto si aun queda contenido
        if (offset < totalBytes)
        {
            BloqueApunt ap{};
            int apSize = (int)sizeof(BloqueApunt);
            for (int k = 0; k < 16; k++)
                ap.b_pointers[k] = -1;

            if (ino.i_block[12] != -1)
                Utilities::ReadObject(f, ap, sb.s_block_start + ino.i_block[12] * apSize);
            else
            {
                int nb = allocBlock();
                if (nb == -1)
                {
                    f.close();
                    return false;
                }
                ino.i_block[12] = nb;
            }

            for (int i = 0; i < 16 && offset < totalBytes; i++)
            {
                if (ap.b_pointers[i] == -1)
                {
                    int nb = allocBlock();
                    if (nb == -1)
                    {
                        f.close();
                        return false;
                    }
                    ap.b_pointers[i] = nb;
                }
                BloqueFile blk{};
                std::memset(blk.b_content, '\0', bsize);
                int take = std::min(totalBytes - offset, bsize);
                std::memcpy(blk.b_content, content.c_str() + offset, take);
                Utilities::WriteObject(f, blk, sb.s_block_start + ap.b_pointers[i] * bsize);
                offset += take;
            }
            Utilities::WriteObject(f, ap, sb.s_block_start + ino.i_block[12] * apSize);
        }

        // Actualizar inodo 0
        ino.i_size = totalBytes;
        ino.i_mtime = (int64_t)std::time(nullptr);
        Utilities::WriteObject(f, ino, inodePos);

        // Actualizar SuperBloque
        Utilities::WriteObject(f, sb, partStart);

        f.close();
        return true;
    }

    // Divide una linea CSV del archivo users.txt en campos, eliminando espacios extra
    static std::vector<std::string> SplitLine(const std::string &line)
    {
        std::vector<std::string> fields;
        std::istringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            // trim
            auto s = token.find_first_not_of(" \t");
            auto e = token.find_last_not_of(" \t\r\n");
            fields.push_back((s == std::string::npos) ? "" : token.substr(s, e - s + 1));
        }
        return fields;
    }

    // Retorna el maximo ID presente para el tipo dado ("G" o "U")
    static int MaxId(const std::string &usersContent, const std::string &tipo)
    {
        int maxId = 0;
        std::istringstream ss(usersContent);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty())
                continue;
            auto fields = SplitLine(line);
            if (fields.size() < 2 || fields[1] != tipo)
                continue;
            int id = 0;
            try
            {
                id = std::stoi(fields[0]);
            }
            catch (...)
            {
            }
            if (id > maxId)
                maxId = id;
        }
        return maxId;
    }

    // ---------- Comandos ----------

    std::string Login(const std::string &user, const std::string &pass, const std::string &id)
    {
        if (s_session.active)
            return "Error [login]: ya hay una sesion activa. Cierre sesion con logout primero";

        auto it = DiskManagement::MountMap.find(id);
        if (it == DiskManagement::MountMap.end())
            return "Error [login]: id de particion no encontrado: " + id;

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [login]: no se pudo leer users.txt de la particion";

        // Buscar usuario con contrasena correcta
        int uid = 0, gid = 0;
        std::string userGroup;
        bool found = false;
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                // uid,U,grupo,nombre,pass
                if (fields.size() < 5 || fields[1] != "U" || fields[0] == "0")
                    continue;
                if (fields[3] == user && fields[4] == pass)
                {
                    try
                    {
                        uid = std::stoi(fields[0]);
                    }
                    catch (...)
                    {
                    }
                    userGroup = fields[2];
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return "Error [login]: usuario o contrasena incorrectos";

        // Obtener GID del grupo del usuario
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                if (fields.size() < 3 || fields[1] != "G" || fields[0] == "0")
                    continue;
                if (fields[2] == userGroup)
                {
                    try
                    {
                        gid = std::stoi(fields[0]);
                    }
                    catch (...)
                    {
                    }
                    break;
                }
            }
        }

        s_session.active = true;
        s_session.user = user;
        s_session.partId = id;
        s_session.uid = uid;
        s_session.gid = gid;
        return "Login exitoso. Usuario: " + user + " | Particion: " + id;
    }

    std::string Logout()
    {
        if (!s_session.active)
            return "Error [logout]: no hay sesion activa";
        std::string user = s_session.user;
        s_session = Session{};
        return "Logout exitoso. Sesion de '" + user + "' cerrada";
    }

    std::string Mkgrp(const std::string &name)
    {
        if (!s_session.active)
            return "Error [mkgrp]: no hay sesion activa";
        if (s_session.user != "root")
            return "Error [mkgrp]: solo root puede crear grupos";

        auto it = DiskManagement::MountMap.find(s_session.partId);
        if (it == DiskManagement::MountMap.end())
            return "Error [mkgrp]: particion de la sesion no encontrada";

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [mkgrp]: no se pudo leer users.txt";

        // Verificar que el grupo no exista activo
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                if (fields.size() < 3 || fields[1] != "G" || fields[0] == "0")
                    continue;
                if (fields[2] == name)
                    return "Error [mkgrp]: el grupo '" + name + "' ya existe";
            }
        }

        int newGid = MaxId(content, "G") + 1;
        content += std::to_string(newGid) + ",G," + name + "\n";

        if (!WriteUsersFile(mp, content))
            return "Error [mkgrp]: no se pudo escribir users.txt";
        return "Grupo '" + name + "' creado (GID=" + std::to_string(newGid) + ")";
    }

    std::string Rmgrp(const std::string &name)
    {
        if (!s_session.active)
            return "Error [rmgrp]: no hay sesion activa";
        if (s_session.user != "root")
            return "Error [rmgrp]: solo root puede eliminar grupos";

        auto it = DiskManagement::MountMap.find(s_session.partId);
        if (it == DiskManagement::MountMap.end())
            return "Error [rmgrp]: particion de la sesion no encontrada";

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [rmgrp]: no se pudo leer users.txt";

        bool found = false;
        std::string newContent;
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty())
            {
                newContent += "\n";
                continue;
            }
            auto fields = SplitLine(line);
            if (fields.size() >= 3 && fields[1] == "G" &&
                fields[2] == name && fields[0] != "0")
            {
                found = true;
                newContent += "0,G," + name + "\n"; // marcar eliminado
            }
            else
            {
                newContent += line + "\n";
            }
        }

        if (!found)
            return "Error [rmgrp]: el grupo '" + name + "' no existe";

        if (!WriteUsersFile(mp, newContent))
            return "Error [rmgrp]: no se pudo escribir users.txt";
        return "Grupo '" + name + "' eliminado";
    }

    std::string Mkusr(const std::string &user, const std::string &pass, const std::string &grp)
    {
        if (!s_session.active)
            return "Error [mkusr]: no hay sesion activa";
        if (s_session.user != "root")
            return "Error [mkusr]: solo root puede crear usuarios";

        auto it = DiskManagement::MountMap.find(s_session.partId);
        if (it == DiskManagement::MountMap.end())
            return "Error [mkusr]: particion de la sesion no encontrada";

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [mkusr]: no se pudo leer users.txt";

        // Verificar que el usuario no exista ya (activo)
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                if (fields.size() < 5 || fields[1] != "U" || fields[0] == "0")
                    continue;
                if (fields[3] == user)
                    return "Error [mkusr]: el usuario '" + user + "' ya existe";
            }
        }

        // Verificar que el grupo exista y no este eliminado
        bool grpOk = false;
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                if (fields.size() < 3 || fields[1] != "G" || fields[0] == "0")
                    continue;
                if (fields[2] == grp)
                {
                    grpOk = true;
                    break;
                }
            }
        }
        if (!grpOk)
            return "Error [mkusr]: el grupo '" + grp + "' no existe";

        int newUid = MaxId(content, "U") + 1;
        content += std::to_string(newUid) + ",U," + grp + "," + user + "," + pass + "\n";

        if (!WriteUsersFile(mp, content))
            return "Error [mkusr]: no se pudo escribir users.txt";
        return "Usuario '" + user + "' creado (UID=" + std::to_string(newUid) + ")";
    }

    std::string Rmusr(const std::string &user)
    {
        if (!s_session.active)
            return "Error [rmusr]: no hay sesion activa";
        if (s_session.user != "root")
            return "Error [rmusr]: solo root puede eliminar usuarios";

        auto it = DiskManagement::MountMap.find(s_session.partId);
        if (it == DiskManagement::MountMap.end())
            return "Error [rmusr]: particion de la sesion no encontrada";

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [rmusr]: no se pudo leer users.txt";

        bool found = false;
        std::string newContent;
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty())
            {
                newContent += "\n";
                continue;
            }
            auto fields = SplitLine(line);
            if (fields.size() >= 5 && fields[1] == "U" &&
                fields[3] == user && fields[0] != "0")
            {
                found = true;
                newContent += "0,U," + fields[2] + "," + user + "," + fields[4] + "\n";
            }
            else
            {
                newContent += line + "\n";
            }
        }

        if (!found)
            return "Error [rmusr]: el usuario '" + user + "' no existe";

        if (!WriteUsersFile(mp, newContent))
            return "Error [rmusr]: no se pudo escribir users.txt";
        return "Usuario '" + user + "' eliminado";
    }

    std::string Chgrp(const std::string &user, const std::string &grp)
    {
        if (!s_session.active)
            return "Error [chgrp]: no hay sesion activa";
        if (s_session.user != "root")
            return "Error [chgrp]: solo root puede cambiar grupos";

        auto it = DiskManagement::MountMap.find(s_session.partId);
        if (it == DiskManagement::MountMap.end())
            return "Error [chgrp]: particion de la sesion no encontrada";

        const auto &mp = it->second;
        std::string content = ReadUsersFile(mp);
        if (content.empty())
            return "Error [chgrp]: no se pudo leer users.txt";

        // Verificar que el grupo destino exista y no este eliminado
        bool grpOk = false;
        {
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                    continue;
                auto fields = SplitLine(line);
                if (fields.size() < 3 || fields[1] != "G" || fields[0] == "0")
                    continue;
                if (fields[2] == grp)
                {
                    grpOk = true;
                    break;
                }
            }
        }
        if (!grpOk)
            return "Error [chgrp]: el grupo '" + grp + "' no existe o fue eliminado";

        bool found = false;
        std::string newContent;
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty())
            {
                newContent += "\n";
                continue;
            }
            auto fields = SplitLine(line);
            if (fields.size() >= 5 && fields[1] == "U" &&
                fields[3] == user && fields[0] != "0")
            {
                found = true;
                newContent += fields[0] + ",U," + grp + "," + user + "," + fields[4] + "\n";
            }
            else
            {
                newContent += line + "\n";
            }
        }

        if (!found)
            return "Error [chgrp]: el usuario '" + user + "' no existe";

        if (!WriteUsersFile(mp, newContent))
            return "Error [chgrp]: no se pudo escribir users.txt";
        return "Usuario '" + user + "' movido al grupo '" + grp + "'";
    }

} // namespace UserSession
