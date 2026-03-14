#include "Analyzer.h"
#include "../DiskManagement/DiskManagement.h"
#include "../FileSystem/FileSystem.h"
#include "../UserSession/UserSession.h"
#include "../FileOperations/FileOperations.h"
#include "../Reports/Reports.h"
#include "../Utilities/Utilities.h"

#include <iostream>
#include <regex>
#include <sstream>
#include <algorithm>

namespace Analyzer
{

    static std::string NormalizeCommandText(const std::string &text)
    {
        std::string normalized;
        normalized.reserve(text.size());

        for (size_t index = 0; index < text.size();)
        {
            if (index + 2 < text.size() &&
                (unsigned char)text[index] == 0xEF &&
                (unsigned char)text[index + 1] == 0xBB &&
                (unsigned char)text[index + 2] == 0xBF)
            {
                index += 3;
                continue;
            }

            if (index + 2 < text.size() &&
                (unsigned char)text[index] == 0xE2 &&
                (unsigned char)text[index + 1] == 0x80 &&
                ((unsigned char)text[index + 2] == 0x9C ||
                 (unsigned char)text[index + 2] == 0x9D))
            {
                normalized.push_back('"');
                index += 3;
                continue;
            }

            normalized.push_back(text[index]);
            index++;
        }

        return normalized;
    }

    // Extrae pares -key=value o -key="valor con espacios" o -flag de una linea de entrada
    std::map<std::string, std::string> ParseParams(const std::string &input)
    {
        std::map<std::string, std::string> params;

        std::string normalized = NormalizeCommandText(input);

        std::regex re("-([\\w]+)(?:=(?:\"([^\"]*)\"|(\\S+)))?");
        auto it = std::sregex_iterator(normalized.begin(), normalized.end(), re);
        auto end = std::sregex_iterator();

        for (; it != end; ++it)
        {
            std::string key = Utilities::ToLower((*it)[1].str());
            std::string value = (*it)[2].matched   ? (*it)[2].str()
                                : (*it)[3].matched ? (*it)[3].str()
                                                   : "";

            if ((*it)[3].matched)
            {
                value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
            }

            params[key] = value;
        }
        return params;
    }

    // Procesa una sola linea: la limpia, identifica el comando y lo despacha al modulo correspondiente
    std::string Analyze(const std::string &input)
    {
        std::string normalized = NormalizeCommandText(input);

        // Trim de espacios al inicio y al final
        std::string trimmed = normalized;
        trimmed.erase(trimmed.begin(),
                      std::find_if(trimmed.begin(), trimmed.end(),
                                   [](unsigned char c)
                                   { return !std::isspace(c); }));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(),
                                   [](unsigned char c)
                                   { return !std::isspace(c); })
                          .base(),
                      trimmed.end());

        if (trimmed.empty())
            return "";

        // Las lineas de comentario se devuelven tal cual
        if (trimmed[0] == '#')
        {
            return trimmed;
        }

        // Extraer la primera palabra como comando
        std::istringstream iss(trimmed);
        std::string command;
        iss >> command;
        command = Utilities::ToLower(command);

        auto params = ParseParams(trimmed);

        // Retorna el valor del parametro o vacio si no existe
        auto require = [&](const std::string &key) -> std::string
        {
            if (params.find(key) == params.end() || params[key].empty())
            {
                return "";
            }
            return params[key];
        };

        // Administracion de discos
        if (command == "mkdisk")
        {
            for (const auto &entry : params)
            {
                const std::string &k = entry.first;
                if (k != "size" && k != "path" && k != "fit" && k != "unit")
                    return "Error [mkdisk]: parametro no permitido -" + k;
            }

            std::string size = require("size");
            std::string path = require("path");
            if (size.empty())
                return "Error [mkdisk]: falta -size";
            if (path.empty())
                return "Error [mkdisk]: falta -path";

            int sizeVal = 0;
            try
            {
                sizeVal = std::stoi(size);
            }
            catch (...)
            {
                return "Error [mkdisk]: -size debe ser un numero entero";
            }

            std::string fit = params.count("fit") ? Utilities::ToLower(params["fit"]) : "ff";
            std::string unit = params.count("unit") ? Utilities::ToLower(params["unit"]) : "m";

            return DiskManagement::Mkdisk(sizeVal, path, fit, unit);
        }

        if (command == "rmdisk")
        {
            std::string path = require("path");
            if (path.empty())
                return "Error [rmdisk]: falta -path";
            return DiskManagement::Rmdisk(path);
        }

        if (command == "fdisk")
        {
            std::string path = require("path");
            std::string name = require("name");
            if (path.empty())
                return "Error [fdisk]: falta -path";
            if (name.empty())
                return "Error [fdisk]: falta -name";

            std::string deleteMode = params.count("delete") ? Utilities::ToLower(params["delete"]) : "";
            int addVal = 0;
            if (params.count("add"))
            {
                try
                {
                    addVal = std::stoi(params["add"]);
                }
                catch (...)
                {
                    return "Error [fdisk]: -add debe ser un numero entero";
                }
            }

            std::string sizeStr = params.count("size") ? params["size"] : "";
            int sizeVal = 0;
            if (!sizeStr.empty())
            {
                try
                {
                    sizeVal = std::stoi(sizeStr);
                }
                catch (...)
                {
                    return "Error [fdisk]: -size debe ser un numero entero";
                }
            }
            else if (deleteMode.empty() && addVal == 0)
            {
                return "Error [fdisk]: falta -size";
            }

            std::string type = params.count("type") ? Utilities::ToLower(params["type"]) : "p";
            std::string fit = params.count("fit") ? Utilities::ToLower(params["fit"]) : "wf";
            std::string unit = params.count("unit") ? Utilities::ToLower(params["unit"]) : "k";

            return DiskManagement::Fdisk(sizeVal, path, name, type, fit, unit, deleteMode, addVal);
        }

        if (command == "mount")
        {
            std::string path = require("path");
            std::string name = require("name");
            if (path.empty())
                return "Error [mount]: falta -path";
            if (name.empty())
                return "Error [mount]: falta -name";
            return DiskManagement::Mount(path, name);
        }

        if (command == "mounted")
        {
            return DiskManagement::Mounted();
        }

        // Sistema de archivos
        if (command == "mkfs")
        {
            std::string id = require("id");
            if (id.empty())
                return "Error [mkfs]: falta -id";
            std::string type = params.count("type") ? Utilities::ToLower(params["type"]) : "full";
            return FileSystem::Mkfs(id, type);
        }

        // Sesion de usuario
        if (command == "login")
        {
            std::string user = require("user");
            std::string pass = require("pass");
            std::string id = require("id");
            if (user.empty())
                return "Error [login]: falta -user";
            if (pass.empty())
                return "Error [login]: falta -pass";
            if (id.empty())
                return "Error [login]: falta -id";
            return UserSession::Login(user, pass, id);
        }

        if (command == "logout")
        {
            return UserSession::Logout();
        }

        if (command == "mkgrp")
        {
            std::string name = require("name");
            if (name.empty())
                return "Error [mkgrp]: falta -name";
            return UserSession::Mkgrp(name);
        }

        if (command == "rmgrp")
        {
            std::string name = require("name");
            if (name.empty())
                return "Error [rmgrp]: falta -name";
            return UserSession::Rmgrp(name);
        }

        if (command == "mkusr")
        {
            std::string user = require("user");
            std::string pass = require("pass");
            std::string grp = require("grp");
            if (user.empty())
                return "Error [mkusr]: falta -user";
            if (pass.empty())
                return "Error [mkusr]: falta -pass";
            if (grp.empty())
                return "Error [mkusr]: falta -grp";
            return UserSession::Mkusr(user, pass, grp);
        }

        if (command == "rmusr")
        {
            std::string user = require("user");
            if (user.empty())
                return "Error [rmusr]: falta -user";
            return UserSession::Rmusr(user);
        }

        if (command == "chgrp")
        {
            std::string user = require("user");
            std::string grp = require("grp");
            if (user.empty())
                return "Error [chgrp]: falta -user";
            if (grp.empty())
                return "Error [chgrp]: falta -grp";
            return UserSession::Chgrp(user, grp);
        }

        // Operaciones de archivos y carpetas
        if (command == "mkfile")
        {
            std::string path = require("path");
            if (path.empty())
                return "Error [mkfile]: falta -path";
            bool rec = params.count("r") > 0;
            int sz = 0;
            if (params.count("size"))
            {
                try
                {
                    sz = std::stoi(params["size"]);
                }
                catch (...)
                {
                    return "Error [mkfile]: -size debe ser un numero entero";
                }
            }
            std::string cont = params.count("cont") ? params["cont"] : "";
            return FileOperations::Mkfile(path, rec, sz, cont);
        }

        if (command == "mkdir")
        {
            std::string path = require("path");
            if (path.empty())
                return "Error [mkdir]: falta -path";
            bool parents = params.count("p") > 0;
            return FileOperations::Mkdir(path, parents);
        }

        if (command == "cat")
        {
            // Acepta -file1, -file2, ..., -fileN
            std::vector<std::string> files;
            for (int i = 1;; i++)
            {
                std::string key = "file" + std::to_string(i);
                if (params.count(key))
                {
                    files.push_back(params[key]);
                }
                else
                    break;
            }
            if (files.empty())
                return "Error [cat]: falta -file1";
            return FileOperations::Cat(files);
        }

        // Reportes
        if (command == "rep")
        {
            std::string name = require("name");
            std::string path = require("path");
            std::string id = require("id");
            if (name.empty())
                return "Error [rep]: falta -name";
            if (path.empty())
                return "Error [rep]: falta -path";
            if (id.empty())
                return "Error [rep]: falta -id";
            std::string pathFileLs = params.count("path_file_ls") ? params["path_file_ls"] : "";
            return Reports::Rep(name, path, id, pathFileLs);
        }

        if (command == "exit")
        {
            std::exit(0);
        }

        return "Error: comando '" + command + "' no reconocido";
    }

    // Procesa un bloque de texto completo (script .smia) linea por linea
    std::string AnalyzeScript(const std::string &script)
    {
        std::ostringstream output;
        std::istringstream stream(script);
        std::string line;

        while (std::getline(stream, line))
        {
            std::string result = Analyze(line);
            if (!result.empty())
            {
                output << result << "\n";
            }
        }
        return output.str();
    }

} // namespace Analyzer
