#include "Reports.h"
#include "../DiskManagement/DiskManagement.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <sstream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <ctime>

namespace Reports
{

    // Detecta la extension del archivo de salida para pasarla a dot (-Tjpg, -Tpng, etc.)
    static std::string GetDotFormat(const std::string &outPath)
    {
        auto pos = outPath.rfind('.');
        if (pos == std::string::npos)
            return "png";
        std::string ext = outPath.substr(pos + 1);
        if (ext == "jpeg")
            return "jpg";
        if (ext == "pdf" || ext == "png" || ext == "svg" || ext == "jpg")
            return ext;
        return "png";
    }

    static bool WantsTextOutput(const std::string &outPath)
    {
        auto pos = outPath.rfind('.');
        if (pos == std::string::npos)
            return false;
        std::string ext = Utilities::ToLower(outPath.substr(pos + 1));
        return ext == "txt";
    }

    // Normaliza la ruta de salida:
    // - Si es relativa sin directorio padre, la coloca en ./reports/
    // - Si no tiene extension, agrega .png (excepto para archivos .txt)
    static std::string NormalizeOutPath(const std::string &outPath)
    {
        // Siempre garantizar que exista ./reports/
        std::filesystem::create_directories("reports");

        std::filesystem::path p(outPath);
        std::string resolved = outPath;

        // Si no es absoluto y no tiene directorio padre, poner en reports/
        if (!p.is_absolute() && p.parent_path().empty())
            resolved = "reports/" + outPath;

        // Si no tiene extension, agregar .png
        std::filesystem::path rp(resolved);
        if (rp.extension().empty())
            resolved += ".png";

        // Crear directorio padre si no existe
        auto parent = std::filesystem::path(resolved).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        return resolved;
    }

    // Escribe el contenido DOT a un archivo temporal y llama a graphviz para generar la imagen
    static std::string RenderDot(const std::string &dotContent, const std::string &outPath)
    {
        // Crear directorio de salida si no existe
        auto parent = std::filesystem::path(outPath).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        std::string tmpFile = outPath + ".dot";
        {
            std::ofstream f(tmpFile);
            if (!f)
                return "Error [rep]: no se pudo escribir archivo dot temporal: " + tmpFile;
            f << dotContent;
        }

        std::string fmt = GetDotFormat(outPath);
        std::string cmd = "dot -T" + fmt + " -o\"" + outPath + "\" \"" + tmpFile + "\" 2>&1";

        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
            return "Error [rep]: no se pudo ejecutar graphviz (dot)";

        char buf[256];
        std::string dotErr;
        while (fgets(buf, sizeof(buf), pipe))
            dotErr += buf;
        int rc = pclose(pipe);

        std::filesystem::remove(tmpFile);

        if (rc != 0)
            return "Error [rep]: graphviz fallo: " + dotErr;
        return "";
    }

    // Reporte MBR: tabla con contenido del MBR y cada particion
    static std::string RepMbr(const std::string &outPath, const std::string &diskPath)
    {
        std::fstream file = Utilities::OpenFile(diskPath);
        if (!file.is_open())
            return "Error [rep]: no se pudo abrir el disco: " + diskPath;

        MBR mbr{};
        if (!Utilities::ReadObject(file, mbr, 0))
            return "Error [rep]: no se pudo leer el MBR";

        std::ostringstream dot;
        dot << "digraph MBR {\n"
            << "  graph [rankdir=TB bgcolor=\"#f0f0f0\"]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=11]\n"
            << "  edge  [color=\"#555555\"]\n\n";

        // Nodo MBR principal - estilo enunciado: header purpura, filas alternantes
        dot << "  mbr [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n"
            << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4A148C\"><FONT COLOR=\"white\"><B>REPORTE DE MBR</B></FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#CE93D8\">mbr_tamano</TD><TD BGCOLOR=\"#F3E5F5\">" << mbr.MbrSize << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#BA68C8\">mbr_fecha_creacion</TD><TD BGCOLOR=\"#E1BEE7\">" << mbr.CreationDate << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#CE93D8\">mbr_dsk_signature</TD><TD BGCOLOR=\"#F3E5F5\">" << mbr.Signature << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#BA68C8\">dsk_fit</TD><TD BGCOLOR=\"#E1BEE7\">" << mbr.Fit << "</TD></TR>\n"
            << "    </TABLE>>]\n\n";

        // Nodos de particion
        int partCount = 0;
        for (int i = 0; i < 4; i++)
        {
            const Partition &p = mbr.Partitions[i];
            if (p.Start == -1)
                continue;

            bool isExt = (p.Type == 'E');
            std::string hdrBg = isExt ? "#880E4F" : "#1A237E";
            std::string typeLabel = isExt ? "Particion Extendida" : "Particion";
            std::string nodeId = "part" + std::to_string(i);

            dot << "  " << nodeId << " [label=<\n"
                << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n"
                << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"" << hdrBg << "\"><FONT COLOR=\"white\"><B>" << typeLabel << "</B></FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#C5CAE9\">part_status</TD><TD BGCOLOR=\"#E8EAF6\">" << p.Status << "</TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#9FA8DA\">part_type</TD><TD BGCOLOR=\"#C5CAE9\">" << p.Type << "</TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#C5CAE9\">part_fit</TD><TD BGCOLOR=\"#E8EAF6\">" << p.Fit << "</TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#9FA8DA\">part_start</TD><TD BGCOLOR=\"#C5CAE9\">" << p.Start << "</TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#C5CAE9\">part_size</TD><TD BGCOLOR=\"#E8EAF6\">" << p.Size << "</TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#9FA8DA\">part_name</TD><TD BGCOLOR=\"#C5CAE9\">" << p.Name << "</TD></TR>\n"
                << "    </TABLE>>]\n\n";

            dot << "  mbr -> " << nodeId << "\n";
            partCount++;

            // Si es extendida, leer la cadena de EBRs
            if (p.Type == 'E')
            {
                int ebrPos = p.Start;
                int ebrIdx = 0;
                while (ebrPos != -1)
                {
                    EBR ebr{};
                    if (!Utilities::ReadObject(file, ebr, ebrPos))
                        break;

                    std::string ebrNode = "ebr_" + std::to_string(i) + "_" + std::to_string(ebrIdx);
                    dot << "  " << ebrNode << " [label=<\n"
                        << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n"
                        << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#B71C1C\"><FONT COLOR=\"white\"><B>Particion Logica</B></FONT></TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#FFCDD2\">part_status</TD><TD BGCOLOR=\"#FFEBEE\">" << ebr.Mount << "</TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#EF9A9A\">part_next</TD><TD BGCOLOR=\"#FFCDD2\">" << ebr.Next << "</TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#FFCDD2\">part_fit</TD><TD BGCOLOR=\"#FFEBEE\">" << ebr.Fit << "</TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#EF9A9A\">part_start</TD><TD BGCOLOR=\"#FFCDD2\">" << ebr.Start << "</TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#FFCDD2\">part_size</TD><TD BGCOLOR=\"#FFEBEE\">" << ebr.Size << "</TD></TR>\n"
                        << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#EF9A9A\">part_name</TD><TD BGCOLOR=\"#FFCDD2\">" << ebr.Name << "</TD></TR>\n"
                        << "    </TABLE>>]\n\n";

                    if (ebrIdx == 0)
                        dot << "  " << nodeId << " -> " << ebrNode << "\n";
                    else
                    {
                        std::string prev = "ebr_" + std::to_string(i) + "_" + std::to_string(ebrIdx - 1);
                        dot << "  " << prev << " -> " << ebrNode << "\n";
                    }

                    ebrPos = (ebr.Next == -1) ? -1 : ebr.Next;
                    ebrIdx++;
                }
            }
        }

        if (partCount == 0)
            dot << "  empty [label=\"Sin particiones\" shape=box]\n  mbr -> empty\n";

        dot << "}\n";
        file.close();

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty())
            return err;
        return "Reporte MBR generado: " + outPath;
    }

    // Lee el SuperBloque de una particion (primaria o logica) dado el MountedPartition
    static bool ReadSuperBloque(const DiskManagement::MountedPartition &mp, SuperBloque &sb)
    {
        std::fstream f = Utilities::OpenFile(mp.diskPath);
        if (!f.is_open())
            return false;

        int partStart = 0;
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
            partStart = mbr.Partitions[mp.partIndex].Start;
        }

        bool ok = Utilities::ReadObject(f, sb, partStart);
        f.close();
        return ok;
    }

    // Reporte bm_inode o bm_block: genera imagen PNG con grilla de bits coloreados
    static std::string RepBitmap(const std::string &outPath,
                                 const DiskManagement::MountedPartition &mp,
                                 bool isInode)
    {
        SuperBloque sb{};
        if (!ReadSuperBloque(mp, sb))
            return "Error [rep]: no se pudo leer el SuperBloque para el bitmap";

        if (sb.s_magic != 0xEF53)
            return "Error [rep]: la particion no tiene un FS EXT2/EXT3 formateado (magic invalido)";

        int count = isInode ? sb.s_inodes_count : sb.s_blocks_count;
        int bmPos  = isInode ? sb.s_bm_inode_start : sb.s_bm_block_start;

        if (count <= 0)
            return "Error [rep]: bitmap sin elementos para reportar";

        std::fstream diskFile = Utilities::OpenFile(mp.diskPath);
        if (!diskFile.is_open())
            return "Error [rep]: no se pudo abrir el disco";

        std::vector<char> bitmap(count, 0);
        diskFile.seekg(bmPos, std::ios::beg);
        diskFile.read(bitmap.data(), count);
        diskFile.close();

        const int COLS = 20;
        std::string title = isInode ? "Bitmap de Inodos" : "Bitmap de Bloques";

        // Contar usados y hallar el ultimo bit en 1
        int usedCount = 0;
        int lastUsed  = -1;
        for (int i = 0; i < count; i++)
        {
            if ((unsigned char)bitmap[i])
            {
                usedCount++;
                lastUsed = i;
            }
        }

        // Mostrar solo hasta el ultimo bit usado + 1 fila de contexto, min 1 fila
        int showUntil = (lastUsed < 0) ? COLS : (lastUsed + COLS + 1);
        if (showUntil > count) showUntil = count;
        // Redondear al multiplo de COLS
        showUntil = ((showUntil + COLS - 1) / COLS) * COLS;
        if (showUntil > count) showUntil = count;
        int rows = (showUntil + COLS - 1) / COLS;

        if (WantsTextOutput(outPath))
        {
            std::ofstream out(outPath);
            if (!out)
                return "Error [rep]: no se pudo escribir archivo txt: " + outPath;

            out << title << "\n";
            out << "usados: " << usedCount << " / " << count << "\n\n";

            for (int r = 0; r < rows; r++)
            {
                for (int c = 0; c < COLS; c++)
                {
                    int idx = r * COLS + c;
                    if (idx >= count)
                        break;
                    out << ((((unsigned char)bitmap[idx]) != 0) ? '1' : '0');
                    if (c + 1 < COLS && idx + 1 < count)
                        out << ' ';
                }
                out << "\n";
            }

            if (showUntil < count)
                out << "... " << (count - showUntil) << " bits libres restantes omitidos\n";

            out.close();
            return (isInode ? "Reporte bm_inode" : "Reporte bm_block") +
                   std::string(" generado: ") + outPath;
        }

        std::ostringstream dot;
        dot << "digraph BM {\n"
            << "  graph [bgcolor=\"#1e1e2e\"]\n"
            << "  node  [shape=none margin=0]\n"
            << "  bm [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"2\" BGCOLOR=\"#1e1e2e\">\n"
            << "      <TR><TD COLSPAN=\"" << COLS << "\" BGCOLOR=\"#1565C0\"><FONT COLOR=\"white\"><B>"
            << title << " (" << usedCount << " usados / " << count << " total)"
            << "</B></FONT></TD></TR>\n";

        for (int r = 0; r < rows; r++)
        {
            dot << "      <TR>";
            for (int c = 0; c < COLS; c++)
            {
                int idx = r * COLS + c;
                if (idx >= count)
                {
                    dot << "<TD BGCOLOR=\"#263238\"><FONT COLOR=\"#37474F\"> </FONT></TD>";
                    continue;
                }
                bool used = ((unsigned char)bitmap[idx]) != 0;
                if (used)
                    dot << "<TD BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\"><B>1</B></FONT></TD>";
                else
                    dot << "<TD BGCOLOR=\"#37474F\"><FONT COLOR=\"#78909C\">0</FONT></TD>";
            }
            dot << "</TR>\n";
        }
        // Fila resumen si se recortaron bits
        if (showUntil < count)
        {
            dot << "      <TR><TD COLSPAN=\"" << COLS << "\" BGCOLOR=\"#263238\">"
                << "<FONT COLOR=\"#78909C\">... " << (count - showUntil)
                << " bits libres restantes omitidos</FONT></TD></TR>\n";
        }
        dot << "    </TABLE>>]\n}\n";

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return (isInode ? "Reporte bm_inode" : "Reporte bm_block") +
               std::string(" generado: ") + outPath;
    }

    static std::string RepDisk(const std::string &outPath, const std::string &diskPath)
    {
        std::fstream file = Utilities::OpenFile(diskPath);
        if (!file.is_open())
            return "Error [rep]: no se pudo abrir el disco: " + diskPath;

        MBR mbr{};
        if (!Utilities::ReadObject(file, mbr, 0))
            return "Error [rep]: no se pudo leer el MBR";
        file.close();

        // Recopilar segmentos: MBR + particiones + huecos libres
        struct Segment
        {
            std::string label;
            int start;
            int size;
            std::string color;
        };
        std::vector<Segment> segs;

        segs.push_back({"MBR", 0, (int)sizeof(MBR), "#aec6e8"});

        // Incluir solo particiones con Start != -1, ordenadas por inicio
        std::vector<std::pair<int, int>> parts; // (start, idx)
        for (int i = 0; i < 4; i++)
            if (mbr.Partitions[i].Start != -1)
                parts.push_back({mbr.Partitions[i].Start, i});
        std::sort(parts.begin(), parts.end());

        int prev = (int)sizeof(MBR);
        for (auto &[start, idx] : parts)
        {
            if (start > prev)
                segs.push_back({"Libre", prev, start - prev, "#e8e8e8"});

            const Partition &p = mbr.Partitions[idx];
            std::string col = (p.Type == 'E') ? "#f7c4be" : "#b8f0b8";
            std::string lbl = std::string(p.Name) + "\\n[" + p.Type + "]";
            segs.push_back({lbl, start, p.Size, col});
            prev = start + p.Size;
        }
        if (prev < mbr.MbrSize)
            segs.push_back({"Libre", prev, mbr.MbrSize - prev, "#e8e8e8"});

        // Total para calcular porcentajes
        double total = (double)mbr.MbrSize;

        // Generar DOT con tabla de una fila
        std::ostringstream dot;
        dot << "digraph DISK {\n"
            << "  graph [rankdir=LR bgcolor=\"#f5f5f5\"]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=10]\n\n"
            << "  disk [label=<\n"
            << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n"
            << "      <TR>\n";

        for (auto &s : segs)
        {
            double pct = (s.size / total) * 100.0;
            // Ancho minimo 40, maximo 200, proporcional al porcentaje
            int w = std::max(40, std::min(200, (int)(pct * 8)));

            std::ostringstream pctStr;
            pctStr << std::fixed << std::setprecision(2) << pct << "%";

            dot << "        <TD WIDTH=\"" << w << "\" BGCOLOR=\"" << s.color << "\">"
                << s.label << "<BR/>" << pctStr.str() << "</TD>\n";
        }

        dot << "      </TR>\n"
            << "    </TABLE>>]\n"
            << "}\n";

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty())
            return err;
        return "Reporte DISK generado: " + outPath;
    }

    // ================================================================
    //  Helpers de traversal para reportes Sprint 5
    // ================================================================

    // Apertura del disco + lectura de SB para los reportes Sprint 5
    static bool OpenDiskAndSB(const DiskManagement::MountedPartition &mp,
                               std::fstream &f, SuperBloque &sb, int &partStart)
    {
        f = Utilities::OpenFile(mp.diskPath);
        if (!f.is_open()) return false;

        if (mp.isLogical)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(f, ebr, mp.ebrPos)) return false;
            partStart = ebr.Start;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(f, mbr, 0)) return false;
            if (mp.partIndex < 0 || mp.partIndex >= 4) return false;
            partStart = mbr.Partitions[mp.partIndex].Start;
        }
        return Utilities::ReadObject(f, sb, partStart) && sb.s_magic == 0xEF53;
    }

    // Busca `name` en el directorio de inodo `dirIdx`, retorna inodo index o -1
    static int FindInDirRep(std::fstream &f, const SuperBloque &sb,
                             int dirIdx, const std::string &name)
    {
        int inoSz = (int)sizeof(Inodo);
        int blkSz = (int)sizeof(BloqueDir);
        Inodo d{};
        if (!Utilities::ReadObject(f, d, sb.s_inode_start + dirIdx * inoSz)) return -1;
        if (d.i_type != '0') return -1;
        for (int i = 0; i < 12; i++)
        {
            if (d.i_block[i] == -1) continue;
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + d.i_block[i] * blkSz)) continue;
            for (int j = 0; j < 4; j++)
            {
                if (bd.b_content[j].b_inodo == -1) continue;
                if (std::strncmp(bd.b_content[j].b_name, name.c_str(), 12) == 0)
                    return bd.b_content[j].b_inodo;
            }
        }
        return -1;
    }

    // Parte ruta en componentes y navega hasta el inodo correspondiente (o -1)
    static int TraversePathRep(std::fstream &f, const SuperBloque &sb,
                                const std::string &path)
    {
        std::vector<std::string> parts;
        std::istringstream ss(path);
        std::string tok;
        while (std::getline(ss, tok, '/'))
            if (!tok.empty()) parts.push_back(tok);

        int cur = 1;
        for (auto &p : parts)
        {
            int found = FindInDirRep(f, sb, cur, p);
            if (found == -1) return -1;
            cur = found;
        }
        return cur;
    }

    // Formatea i_perm[3] como "drwxrwxrwx" / "-rwxrwxrwx"
    static std::string FormatPerm(char itype, const char perm[3])
    {
        std::string s;
        s += (itype == '0') ? 'd' : '-';
        for (int i = 0; i < 3; i++)
        {
            int v = perm[i] - '0';
            s += (v & 4) ? 'r' : '-';
            s += (v & 2) ? 'w' : '-';
            s += (v & 1) ? 'x' : '-';
        }
        return s;
    }

    // Formatea epoch a cadena legible
    static std::string FmtEpoch(int64_t t)
    {
        if (t == 0) return "-";
        char buf[32];
        std::time_t tt = (std::time_t)t;
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        return std::string(buf);
    }

    // Escaping de caracteres especiales para DOT/HTML
    static std::string HtmlEsc(const std::string &s)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (c == '&')  out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '"') out += "&quot;";
            else if (c < 32 || c > 126) out += '.';
            else out += (char)c;
        }
        return out;
    }

    // ================================================================
    //  rep inode — tabla de todos los inodos usados
    // ================================================================
    static std::string RepInode(const std::string &outPath,
                                 const DiskManagement::MountedPartition &mp)
    {
        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        if (!OpenDiskAndSB(mp, f, sb, partStart))
            return "Error [rep]: no se pudo leer el SuperBloque (inode)";

        int inoSz = (int)sizeof(Inodo);
        std::ostringstream dot;
        dot << "digraph INODE {\n"
            << "  graph [rankdir=LR bgcolor=\"#1e1e2e\" splines=ortho]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=10]\n\n";

        int rendered = 0;
        for (int i = 0; i < sb.s_inodes_count; i++)
        {
            char bit = 0;
            f.seekg(sb.s_bm_inode_start + i, std::ios::beg);
            f.read(&bit, 1);
            if (!bit) continue;

            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + i * inoSz)) continue;

            bool isDir = (ino.i_type == '0');
            std::string bgColor  = isDir ? "#5B9BD5" : "#5CB85C";
            std::string hdrColor = isDir ? "#2E75B6" : "#3A8A3A";
            std::string typeStr  = isDir ? "Carpeta" : "Archivo";
            const char* blkLbls[] = {
                "A0","A1","A2","A3","A4","A5","A6","A7",
                "A8","A9","A10","A11","ind.0","dbl.0","tri.0"
            };

            dot << "  ino" << i << " [label=<\n"
                << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"" << bgColor << "\">\n"
                << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"" << hdrColor << "\"><FONT COLOR=\"white\"><B>I-nodo " << i << "</B></FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">UID</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_uid << "</FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">GID</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_gid << "</FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Fecha</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << FmtEpoch(ino.i_ctime) << "</FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Tipo</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << typeStr << "</FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Size</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_size << "</FONT></TD></TR>\n"
                << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Perm</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << FormatPerm(ino.i_type, ino.i_perm) << "</FONT></TD></TR>\n";
            for (int k = 0; k < 15; k++)
            {
                if (ino.i_block[k] != -1)
                    dot << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << blkLbls[k] << "</FONT></TD>"
                        << "<TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_block[k] << "</FONT></TD></TR>\n";
            }
            dot << "    </TABLE>>]\n\n";
            rendered++;
        }

        // Segunda pasada: aristas entre inodos conectados via bloques directorio
        for (int i = 0; i < sb.s_inodes_count; i++)
        {
            char bit2 = 0;
            f.seekg(sb.s_bm_inode_start + i, std::ios::beg);
            f.read(&bit2, 1);
            if (!bit2) continue;
            Inodo ino2{};
            if (!Utilities::ReadObject(f, ino2, sb.s_inode_start + i * inoSz)) continue;
            if (ino2.i_type != '0') continue; // solo dirs referencian inodos hijo
            int blkSz2 = (int)sizeof(BloqueDir);
            for (int k = 0; k < 12; k++)
            {
                if (ino2.i_block[k] == -1) continue;
                BloqueDir bd{};
                if (!Utilities::ReadObject(f, bd, sb.s_block_start + ino2.i_block[k] * blkSz2)) continue;
                for (int j = 0; j < 4; j++)
                {
                    int childIdx = bd.b_content[j].b_inodo;
                    if (childIdx < 0 || childIdx == i) continue;
                    std::string cname(bd.b_content[j].b_name,
                                      strnlen(bd.b_content[j].b_name, 12));
                    if (cname == ".." || cname.empty()) continue;
                    // Solo dibujar arista si el hijo tiene bitmap en 1
                    char cb = 0;
                    f.seekg(sb.s_bm_inode_start + childIdx, std::ios::beg);
                    f.read(&cb, 1);
                    if (!cb) continue;
                    dot << "  ino" << i << " -> ino" << childIdx
                        << " [color=\"#aaaaaa\" label=\"" << HtmlEsc(cname)
                        << "\" fontsize=8 fontcolor=\"white\"]\n";
                }
            }
        }

        if (rendered == 0)
            dot << "  empty [label=\"Sin inodos usados\" shape=box style=filled fillcolor=white]\n";

        dot << "}\n";
        f.close();

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte inode generado: " + outPath;
    }

    // ================================================================
    //  rep block — tabla de todos los bloques usados
    // ================================================================
    static std::string RepBlock(const std::string &outPath,
                                 const DiskManagement::MountedPartition &mp)
    {
        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        if (!OpenDiskAndSB(mp, f, sb, partStart))
            return "Error [rep]: no se pudo leer el SuperBloque (block)";

        int inoSz = (int)sizeof(Inodo);
        int blkSz = (int)sizeof(BloqueDir); // all blocks 64 bytes

        // Mapa de bloque -> tipo ('d','f','a') construido caminando inodos
        std::vector<char> blkType(sb.s_blocks_count, 0);
        for (int i = 0; i < sb.s_inodes_count; i++)
        {
            char bit = 0;
            f.seekg(sb.s_bm_inode_start + i, std::ios::beg);
            f.read(&bit, 1);
            if (!bit) continue;
            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + i * inoSz)) continue;
            char t = (ino.i_type == '0') ? 'd' : 'f';
            std::vector<int> dataBlocks;
            std::vector<int> ptrBlocks;
            Utilities::CollectInodeBlockIndices(f, sb, ino, dataBlocks, &ptrBlocks);
            for (int blkIdx : dataBlocks)
                if (blkIdx >= 0 && blkIdx < sb.s_blocks_count)
                    blkType[blkIdx] = t;
            for (int blkIdx : ptrBlocks)
                if (blkIdx >= 0 && blkIdx < sb.s_blocks_count)
                    blkType[blkIdx] = 'a';
        }

        std::ostringstream dot;
        dot << "digraph BLOCK {\n"
            << "  graph [rankdir=LR bgcolor=\"#1e1e2e\" splines=ortho]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=10]\n\n";

        int rendered = 0;
        for (int i = 0; i < sb.s_blocks_count; i++)
        {
            char bit = 0;
            f.seekg(sb.s_bm_block_start + i, std::ios::beg);
            f.read(&bit, 1);
            if (!bit) continue;

            int blkPos = sb.s_block_start + i * blkSz;
            char bt = blkType[i];

            if (bt == 'd')
            {
                BloqueDir bd{};
                if (!Utilities::ReadObject(f, bd, blkPos)) continue;
                dot << "  blk" << i << " [label=<\n"
                    << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#F0A030\">\n"
                    << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#C07010\"><FONT COLOR=\"white\"><B>Bloque " << i << "</B></FONT></TD></TR>\n";
                for (int j = 0; j < 4; j++)
                {
                    std::string nm = (bd.b_content[j].b_inodo != -1)
                                     ? HtmlEsc(std::string(bd.b_content[j].b_name, strnlen(bd.b_content[j].b_name, 12)))
                                     : "-";
                    std::string ino_s = (bd.b_content[j].b_inodo != -1)
                                      ? std::to_string(bd.b_content[j].b_inodo) : "-";
                    dot << "      <TR><TD ALIGN=\"LEFT\">" << nm << "</TD><TD ALIGN=\"LEFT\">" << ino_s << "</TD></TR>\n";
                }
                dot << "    </TABLE>>]\n\n";
            }
            else if (bt == 'f')
            {
                BloqueFile bf{};
                if (!Utilities::ReadObject(f, bf, blkPos)) continue;
                std::string content = HtmlEsc(std::string(bf.b_content, 64));
                if (content.size() > 40) content = content.substr(0, 40) + "...";
                dot << "  blk" << i << " [label=<\n"
                    << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#5CB85C\">\n"
                    << "      <TR><TD BGCOLOR=\"#3A8A3A\"><FONT COLOR=\"white\"><B>Bloque " << i << "</B></FONT></TD></TR>\n"
                    << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << content << "</FONT></TD></TR>\n"
                    << "    </TABLE>>]\n\n";
            }
            else if (bt == 'a')
            {
                BloqueApunt ap{};
                if (!Utilities::ReadObject(f, ap, blkPos)) continue;
                dot << "  blk" << i << " [label=<\n"
                    << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#F5D76E\">\n"
                    << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#C8A800\"><B>Bloque " << i << " (ptr)</B></TD></TR>\n";
                for (int k = 0; k < 16; k++)
                    if (ap.b_pointers[k] != -1)
                        dot << "      <TR><TD ALIGN=\"LEFT\">ptr[" << k << "]</TD><TD ALIGN=\"LEFT\">" << ap.b_pointers[k] << "</TD></TR>\n";
                dot << "    </TABLE>>]\n\n";
                // Aristas: bloque apuntador -> bloques de datos referenciados
                for (int k = 0; k < 16; k++)
                {
                    if (ap.b_pointers[k] < 0 || ap.b_pointers[k] >= sb.s_blocks_count) continue;
                    char ptBit = 0;
                    f.seekg(sb.s_bm_block_start + ap.b_pointers[k], std::ios::beg);
                    f.read(&ptBit, 1);
                    if (ptBit)
                        dot << "  blk" << i << " -> blk" << ap.b_pointers[k]
                            << " [color=\"#C8A800\" style=dashed arrowsize=0.7]\n";
                }
            }
            else
            {
                dot << "  blk" << i << " [label=<\n"
                    << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#888888\">\n"
                    << "      <TR><TD><FONT COLOR=\"white\"><B>Bloque " << i << "</B></FONT></TD></TR>\n"
                    << "    </TABLE>>]\n\n";
            }
            rendered++;
        }

        if (rendered == 0)
            dot << "  empty [label=\"Sin bloques usados\" shape=box style=filled fillcolor=white]\n";

        // Segunda pasada: aristas entre bloques consecutivos de datos del mismo inodo
        for (int i = 0; i < sb.s_inodes_count; i++)
        {
            char bit2 = 0;
            f.seekg(sb.s_bm_inode_start + i, std::ios::beg);
            f.read(&bit2, 1);
            if (!bit2) continue;
            Inodo ino2{};
            if (!Utilities::ReadObject(f, ino2, sb.s_inode_start + i * inoSz)) continue;
            std::vector<int> dataBlocks;
            Utilities::CollectInodeBlockIndices(f, sb, ino2, dataBlocks, nullptr);
            int prevBlk = -1;
            for (int bIdx : dataBlocks)
            {
                char bBit = 0;
                f.seekg(sb.s_bm_block_start + bIdx, std::ios::beg);
                f.read(&bBit, 1);
                if (!bBit) continue;
                if (prevBlk >= 0)
                    dot << "  blk" << prevBlk << " -> blk" << bIdx
                        << " [color=\"#888888\" style=dotted arrowsize=0.7]\n";
                prevBlk = bIdx;
            }
        }

        dot << "}\n";
        f.close();

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte block generado: " + outPath;
    }

    // ================================================================
    //  rep tree — arbol EXT2 desde la raiz
    // ================================================================

    static void BuildTree(std::fstream &f, const SuperBloque &sb,
                          int inoIdx,
                          std::ostringstream &dot,
                          std::vector<bool> &visitedIno,
                          std::vector<bool> &visitedBlk);

    static void RenderTreeDataBlock(std::fstream &f, const SuperBloque &sb,
                                    int blkIdx, bool isDir,
                                    std::ostringstream &dot,
                                    std::vector<bool> &visitedIno,
                                    std::vector<bool> &visitedBlk)
    {
        if (blkIdx < 0 || blkIdx >= sb.s_blocks_count)
            return;
        if (visitedBlk[blkIdx])
            return;

        visitedBlk[blkIdx] = true;

        if (isDir)
        {
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + blkIdx * (int)sizeof(BloqueDir)))
                return;

            dot << "  n_blk" << blkIdx << " [label=<\n"
                << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#F0A030\">\n"
                << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#C07010\"><FONT COLOR=\"white\"><B>Bloque " << blkIdx << "</B></FONT></TD></TR>\n";
            for (int j = 0; j < 4; j++)
            {
                std::string nm = (bd.b_content[j].b_inodo != -1)
                    ? HtmlEsc(std::string(bd.b_content[j].b_name, strnlen(bd.b_content[j].b_name, 12)))
                    : "-";
                std::string ino_s = (bd.b_content[j].b_inodo != -1)
                    ? std::to_string(bd.b_content[j].b_inodo) : "-";
                bool isChild = (bd.b_content[j].b_inodo != -1 && nm != "." && nm != "..");
                if (isChild)
                    dot << "      <TR><TD ALIGN=\"LEFT\">" << nm << "</TD>"
                        << "<TD ALIGN=\"LEFT\" PORT=\"c" << j << "\">" << ino_s << "</TD></TR>\n";
                else
                    dot << "      <TR><TD ALIGN=\"LEFT\">" << nm << "</TD>"
                        << "<TD ALIGN=\"LEFT\">" << ino_s << "</TD></TR>\n";
            }
            dot << "    </TABLE>>]\n\n";

            for (int j = 0; j < 4; j++)
            {
                int childIno = bd.b_content[j].b_inodo;
                if (childIno == -1)
                    continue;
                std::string cname(bd.b_content[j].b_name, strnlen(bd.b_content[j].b_name, 12));
                if (cname == "." || cname == "..")
                    continue;
                dot << "  n_blk" << blkIdx << ":c" << j << " -> n_ino" << childIno << "\n";
                BuildTree(f, sb, childIno, dot, visitedIno, visitedBlk);
            }
            return;
        }

        BloqueFile bf{};
        if (!Utilities::ReadObject(f, bf, sb.s_block_start + blkIdx * (int)sizeof(BloqueFile)))
            return;

        std::string ct = HtmlEsc(std::string(bf.b_content, strnlen(bf.b_content, 64)));
        if (ct.size() > 32)
            ct = ct.substr(0, 32) + "...";
        dot << "  n_blk" << blkIdx << " [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#5CB85C\">\n"
            << "      <TR><TD BGCOLOR=\"#3A8A3A\"><FONT COLOR=\"white\"><B>Bloque " << blkIdx << "</B></FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ct << "</FONT></TD></TR>\n"
            << "    </TABLE>>]\n\n";
    }

    static void RenderTreePointerBlock(std::fstream &f, const SuperBloque &sb,
                                       int ptrIdx, int level, bool isDir,
                                       std::ostringstream &dot,
                                       std::vector<bool> &visitedIno,
                                       std::vector<bool> &visitedBlk)
    {
        if (ptrIdx < 0 || ptrIdx >= sb.s_blocks_count || level <= 0)
            return;

        if (!visitedBlk[ptrIdx])
        {
            visitedBlk[ptrIdx] = true;

            BloqueApunt ap{};
            if (!Utilities::ReadObject(f, ap, sb.s_block_start + ptrIdx * (int)sizeof(BloqueApunt)))
                return;

            std::string tag = (level == 1) ? "ind" : (level == 2 ? "dbl" : "tri");
            dot << "  n_blk" << ptrIdx << " [label=<\n"
                << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#F5D76E\">\n"
                << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#C8A800\"><B>Bloque " << ptrIdx << " (" << tag << ")</B></TD></TR>\n";
            for (int k = 0; k < 16; k++)
                if (ap.b_pointers[k] >= 0 && ap.b_pointers[k] < sb.s_blocks_count)
                    dot << "      <TR><TD ALIGN=\"LEFT\" PORT=\"pp" << k << "\">ptr[" << k << "]</TD>"
                        << "<TD ALIGN=\"LEFT\">" << ap.b_pointers[k] << "</TD></TR>\n";
            dot << "    </TABLE>>]\n\n";
        }

        BloqueApunt ap2{};
        if (!Utilities::ReadObject(f, ap2, sb.s_block_start + ptrIdx * (int)sizeof(BloqueApunt)))
            return;

        for (int k = 0; k < 16; k++)
        {
            int child = ap2.b_pointers[k];
            if (child < 0 || child >= sb.s_blocks_count)
                continue;

            dot << "  n_blk" << ptrIdx << ":pp" << k << " -> n_blk" << child << "\n";

            if (level == 1)
                RenderTreeDataBlock(f, sb, child, isDir, dot, visitedIno, visitedBlk);
            else
                RenderTreePointerBlock(f, sb, child, level - 1, isDir, dot, visitedIno, visitedBlk);
        }
    }

    // Construye recursivamente el grafo EXT2: inodos + bloques con sus conexiones
    static void BuildTree(std::fstream &f, const SuperBloque &sb,
                          int inoIdx,
                          std::ostringstream &dot,
                          std::vector<bool> &visitedIno,
                          std::vector<bool> &visitedBlk)
    {
        if (inoIdx < 0 || inoIdx >= sb.s_inodes_count) return;
        if (visitedIno[inoIdx]) return;
        visitedIno[inoIdx] = true;

        int inoSz = (int)sizeof(Inodo);

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * inoSz)) return;

        bool isDir    = (ino.i_type == '0');
        std::string bgColor  = isDir ? "#5B9BD5" : "#5CB85C";
        std::string hdrColor = isDir ? "#2E75B6" : "#3A8A3A";
        std::string typeStr  = isDir ? "Carpeta" : "Archivo";
        const char* blkLbls[] = {
            "A0","A1","A2","A3","A4","A5","A6","A7",
            "A8","A9","A10","A11","ind.0","dbl.0","tri.0"
        };

        dot << "  n_ino" << inoIdx << " [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"" << bgColor << "\">\n"
            << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"" << hdrColor << "\"><FONT COLOR=\"white\"><B>I-nodo " << inoIdx << "</B></FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">UID</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_uid << "</FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Fecha</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << FmtEpoch(ino.i_ctime) << "</FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Tipo</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << typeStr << "</FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">Size</FONT></TD><TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_size << "</FONT></TD></TR>\n";
        for (int k = 0; k < 15; k++)
        {
            if (ino.i_block[k] != -1)
                dot << "      <TR><TD ALIGN=\"LEFT\" PORT=\"p" << k << "\"><FONT COLOR=\"white\">" << blkLbls[k] << "</FONT></TD>"
                    << "<TD ALIGN=\"LEFT\"><FONT COLOR=\"white\">" << ino.i_block[k] << "</FONT></TD></TR>\n";
        }
        dot << "    </TABLE>>]\n\n";

        for (int k = 0; k < 12; k++)
        {
            int bIdx = ino.i_block[k];
            if (bIdx < 0 || bIdx >= sb.s_blocks_count)
                continue;

            dot << "  n_ino" << inoIdx << ":p" << k << " -> n_blk" << bIdx << "\n";
            RenderTreeDataBlock(f, sb, bIdx, isDir, dot, visitedIno, visitedBlk);
        }

        for (int idx = 12; idx <= 14; idx++)
        {
            if (ino.i_block[idx] < 0 || ino.i_block[idx] >= sb.s_blocks_count)
                continue;

            dot << "  n_ino" << inoIdx << ":p" << idx << " -> n_blk" << ino.i_block[idx] << "\n";
            RenderTreePointerBlock(f, sb, ino.i_block[idx], idx - 11, isDir, dot, visitedIno, visitedBlk);
        }
    }

    static std::string RepTree(const std::string &outPath,
                                const DiskManagement::MountedPartition &mp)
    {
        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        if (!OpenDiskAndSB(mp, f, sb, partStart))
            return "Error [rep]: no se pudo leer el SuperBloque (tree)";
        if (sb.s_inodes_count <= 1 || sb.s_blocks_count <= 0)
        {
            f.close();
            return "Error [rep]: estructura de FS invalida para generar tree";
        }

        std::ostringstream dot;
        dot << "digraph TREE {\n"
            << "  graph [rankdir=LR bgcolor=\"#1e1e2e\" splines=ortho pad=\"0.5\"]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=10]\n"
            << "  edge  [color=\"#aaaaaa\" arrowsize=0.7]\n\n";

        std::vector<bool> visitedIno(sb.s_inodes_count, false);
        std::vector<bool> visitedBlk(sb.s_blocks_count, false);
        BuildTree(f, sb, 1, dot, visitedIno, visitedBlk);

        dot << "}\n";
        f.close();

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte tree generado: " + outPath;
    }

    // ================================================================
    //  rep file — contenido de un archivo como .txt
    // ================================================================
    static std::string RepFile(const std::string &outPath,
                                const DiskManagement::MountedPartition &mp,
                                const std::string &pathFileLs)
    {
        if (pathFileLs.empty())
            return "Error [rep]: falta -path_file_ls para rep file";

        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        if (!OpenDiskAndSB(mp, f, sb, partStart))
            return "Error [rep]: no se pudo leer el SuperBloque (file)";

        int inoIdx = TraversePathRep(f, sb, pathFileLs);
        if (inoIdx == -1)
        {
            f.close();
            return "Error [rep]: archivo no encontrado: " + pathFileLs;
        }

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [rep]: no se pudo leer el inodo";
        }
        if (ino.i_type != '1')
        {
            f.close();
            return "Error [rep]: la ruta especificada es un directorio, no un archivo";
        }

        std::string content = Utilities::ReadFileData(f, sb, ino);
        f.close();

        if (WantsTextOutput(outPath))
        {
            std::ofstream out(outPath);
            if (!out)
                return "Error [rep]: no se pudo escribir archivo txt: " + outPath;
            out << content;
            out.close();
            return "Reporte file generado: " + outPath;
        }

        // Generar imagen DOT con el contenido del archivo
        std::ostringstream dot;
        dot << "digraph FILE {\n"
            << "  graph [bgcolor=\"#1e1e2e\"]\n"
            << "  node  [shape=none fontname=\"Courier\"]\n"
            << "  file [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#263238\">\n"
            << "      <TR><TD BGCOLOR=\"#1565C0\"><FONT COLOR=\"white\"><B>Archivo: "
            << HtmlEsc(pathFileLs) << "</B></FONT></TD></TR>\n";

        // Dividir contenido en lineas
        std::istringstream ss(content);
        std::string cline;
        bool hasLines = false;
        const size_t wrapWidth = 80;
        while (std::getline(ss, cline))
        {
            hasLines = true;
            if (cline.empty())
            {
                dot << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"#CFD8DC\"> </FONT></TD></TR>\n";
                continue;
            }

            for (size_t pos = 0; pos < cline.size(); pos += wrapWidth)
            {
                dot << "      <TR><TD ALIGN=\"LEFT\"><FONT COLOR=\"#CFD8DC\">"
                    << HtmlEsc(cline.substr(pos, wrapWidth))
                    << "</FONT></TD></TR>\n";
            }
        }
        if (!hasLines)
            dot << "      <TR><TD><FONT COLOR=\"#78909C\">(archivo vacio)</FONT></TD></TR>\n";
        dot << "    </TABLE>>]\n}\n";

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte file generado: " + outPath;
    }

    // ================================================================
    //  rep ls — tabla de contenidos de un directorio
    // ================================================================
    static std::string RepLs(const std::string &outPath,
                              const DiskManagement::MountedPartition &mp,
                              const std::string &pathFileLs)
    {
        std::string dirPath = pathFileLs.empty() ? "/" : pathFileLs;

        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        if (!OpenDiskAndSB(mp, f, sb, partStart))
            return "Error [rep]: no se pudo leer el SuperBloque (ls)";

        int dirIno = TraversePathRep(f, sb, dirPath);
        if (dirIno == -1)
        {
            f.close();
            return "Error [rep]: directorio no encontrado: " + dirPath;
        }

        int inoSz = (int)sizeof(Inodo);
        int blkSz = (int)sizeof(BloqueDir);

        Inodo dino{};
        if (!Utilities::ReadObject(f, dino, sb.s_inode_start + dirIno * inoSz) || dino.i_type != '0')
        {
            f.close();
            return "Error [rep]: la ruta indicada no es un directorio";
        }

        std::ostringstream dot;
        dot << "digraph LS {\n"
            << "  graph [bgcolor=\"#f5f5f5\"]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=10]\n\n"
            << "  ls [label=<\n"
            << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" BGCOLOR=\"#f0f8ff\">\n"
            << "      <TR>\n"
            << "        <TD><B>Permisos</B></TD>\n"
            << "        <TD><B>UID</B></TD>\n"
            << "        <TD><B>GID</B></TD>\n"
            << "        <TD><B>Tamanio</B></TD>\n"
            << "        <TD><B>Nombre</B></TD>\n"
            << "        <TD><B>Tipo</B></TD>\n"
            << "        <TD><B>Modificado</B></TD>\n"
            << "      </TR>\n";

        // Iterar entradas del directorio
        for (int i = 0; i < 12; i++)
        {
            if (dino.i_block[i] == -1) continue;
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + dino.i_block[i] * blkSz)) continue;
            for (int j = 0; j < 4; j++)
            {
                int childIdx = bd.b_content[j].b_inodo;
                if (childIdx == -1) continue;
                std::string cname(bd.b_content[j].b_name,
                                  strnlen(bd.b_content[j].b_name, 12));

                Inodo cino{};
                if (!Utilities::ReadObject(f, cino, sb.s_inode_start + childIdx * inoSz)) continue;

                std::string typeStr = (cino.i_type == '0') ? "carpeta" : "archivo";
                std::string bgcol  = (cino.i_type == '0') ? "#dce8f7" : "#dcf7e4";

                dot << "      <TR BGCOLOR=\"" << bgcol << "\">\n"
                    << "        <TD>" << FormatPerm(cino.i_type, cino.i_perm) << "</TD>\n"
                    << "        <TD>" << cino.i_uid << "</TD>\n"
                    << "        <TD>" << cino.i_gid << "</TD>\n"
                    << "        <TD>" << cino.i_size << "</TD>\n"
                    << "        <TD>" << HtmlEsc(cname) << "</TD>\n"
                    << "        <TD>" << typeStr << "</TD>\n"
                    << "        <TD>" << FmtEpoch(cino.i_mtime) << "</TD>\n"
                    << "      </TR>\n";
            }
        }

        dot << "    </TABLE>>]\n}\n";
        f.close();

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte ls generado: " + outPath;
    }

    // ================================================================
    //  rep sb
    // ================================================================

    // Reporte SB: muestra todos los campos del SuperBloque en una tabla
    static std::string RepSb(const std::string &outPath,
                              const DiskManagement::MountedPartition &mp)
    {
        SuperBloque sb{};
        if (!ReadSuperBloque(mp, sb))
            return "Error [rep]: no se pudo leer el SuperBloque";
        if (sb.s_magic != 0xEF53)
            return "Error [rep]: la particion no tiene EXT2/EXT3 formateado";

        // Formatea un timestamp epoch a cadena legible
        auto fmtTime = [](int64_t t) -> std::string
        {
            if (t == 0) return "-";
            char buf[32];
            std::time_t tt = (std::time_t)t;
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
            return std::string(buf);
        };

        std::ostringstream dot;
        dot << "digraph SB {\n"
            << "  graph [bgcolor=\"#f0f0f0\"]\n"
            << "  node  [shape=none fontname=\"Helvetica\" fontsize=11]\n\n"
            << "  sb [label=<\n"
            << "    <TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n"
            << "      <TR><TD COLSPAN=\"2\" BGCOLOR=\"#1B5E20\"><FONT COLOR=\"white\"><B>Reporte de SUPERBLOQUE</B></FONT></TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_filesystem_type</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_filesystem_type << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_inodes_count</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_inodes_count << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_blocks_count</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_blocks_count << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_free_blocks_count</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_free_blocks_count << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_free_inodes_count</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_free_inodes_count << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_mtime</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << fmtTime(sb.s_mtime) << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_umtime</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << fmtTime(sb.s_umtime) << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_mnt_count</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_mnt_count << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_magic</FONT></TD><TD BGCOLOR=\"#A5D6A7\">0x" << std::hex << sb.s_magic << std::dec << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_inode_s</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_inode_s << " bytes</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_block_s</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_block_s << " bytes</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_firts_ino</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_firts_ino << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_first_blo</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_first_blo << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_bm_inode_start</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_bm_inode_start << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_bm_block_start</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_bm_block_start << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#388E3C\"><FONT COLOR=\"white\">s_inode_start</FONT></TD><TD BGCOLOR=\"#C8E6C9\">" << sb.s_inode_start << "</TD></TR>\n"
            << "      <TR><TD ALIGN=\"LEFT\" BGCOLOR=\"#2E7D32\"><FONT COLOR=\"white\">s_block_start</FONT></TD><TD BGCOLOR=\"#A5D6A7\">" << sb.s_block_start << "</TD></TR>\n"
            << "    </TABLE>>]\n"
            << "}\n";

        std::string err = RenderDot(dot.str(), outPath);
        if (!err.empty()) return err;
        return "Reporte SB generado: " + outPath;
    }

    // Punto de entrada: decide que reporte generar segun el nombre
    std::string Rep(const std::string &name, const std::string &outPath,
                    const std::string &id, const std::string &pathFileLs)
    {
        // Buscar el disco asociado al ID montado
        auto it = DiskManagement::MountMap.find(id);
        if (it == DiskManagement::MountMap.end())
            return "Error [rep]: id no encontrado: " + id;

        const std::string &diskPath = it->second.diskPath;

        // Normalizar ruta: PNG por defecto, reports/ si relativa sin directorio
        std::string rPath = NormalizeOutPath(outPath);

        if (name == "mbr")
            return RepMbr(rPath, diskPath);
        else if (name == "disk")
            return RepDisk(rPath, diskPath);
        else if (name == "bm_inode")
            return RepBitmap(rPath, it->second, true);
        else if (name == "bm_block")
            return RepBitmap(rPath, it->second, false);
        else if (name == "sb")
            return RepSb(rPath, it->second);
        else if (name == "inode")
            return RepInode(rPath, it->second);
        else if (name == "block")
            return RepBlock(rPath, it->second);
        else if (name == "tree")
            return RepTree(rPath, it->second);
        else if (name == "file")
            return RepFile(rPath, it->second, pathFileLs);
        else if (name == "ls")
            return RepLs(rPath, it->second, pathFileLs);

        return "Error [rep]: nombre de reporte no reconocido: " + name;
    }

} // namespace Reports
