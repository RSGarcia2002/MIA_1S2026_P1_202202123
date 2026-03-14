#include "HttpServer.h"
#include "../Analyzer/Analyzer.h"
#include "../external/crow_all.h"

#include <string>
#include <fstream>
#include <filesystem>

namespace HttpServer
{

    // Escapa caracteres especiales de JSON en una cadena
    static std::string JsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else
                out += c;
        }
        return out;
    }

    // Extrae el valor del campo "commands" de un cuerpo JSON simple
    // Soporta: {"commands":"..."} o {"commands": "..."}
    static std::string ExtractCommands(const std::string &body)
    {
        const std::string key = "\"commands\"";
        auto pos = body.find(key);
        if (pos == std::string::npos)
            return "";

        pos = body.find('"', pos + key.size());
        if (pos == std::string::npos)
            return "";
        pos++;

        std::string result;
        bool escape = false;
        for (size_t i = pos; i < body.size(); i++)
        {
            char c = body[i];
            if (escape)
            {
                if (c == 'n')
                    result += '\n';
                else if (c == 'r')
                    result += '\r';
                else if (c == 't')
                    result += '\t';
                else if (c == '\\')
                    result += '\\';
                else if (c == '"')
                    result += '"';
                else
                    result += c;
                escape = false;
            }
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                break;
            else
                result += c;
        }
        return result;
    }

    void Start(int port)
    {
        crow::SimpleApp app;

        // Silenciar logs de Crow (solo errores)
        app.loglevel(crow::LogLevel::Warning);

        // ── POST /execute 
        // Body:    { "commands": "mkdisk -size=10 ..." }
        // Response:{ "output":   "..." }
        CROW_ROUTE(app, "/execute").methods(crow::HTTPMethod::POST)([](const crow::request &req)
                                                                    {
        std::string commands = ExtractCommands(req.body);
        std::string output;

        if (commands.empty())
            output = "Error: campo 'commands' no encontrado en el cuerpo JSON";
        else
            output = Analyzer::AnalyzeScript(commands);

        std::string json = "{\"output\":\"" + JsonEscape(output) + "\"}";

        crow::response res(200, json);
        res.add_header("Content-Type",                 "application/json");
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res; });

        // ── OPTIONS /execute (preflight CORS) 
        CROW_ROUTE(app, "/execute").methods(crow::HTTPMethod::OPTIONS)([](const crow::request &)
                                                                       {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res; });

        // ── GET /report?path=<filepath> 
        // Sirve el archivo generado por un reporte (imagen o texto)
        CROW_ROUTE(app, "/report").methods(crow::HTTPMethod::GET)([](const crow::request &req)
                                                                  {
        auto filePath = req.url_params.get("path");
        if (!filePath)
        {
            crow::response res(400, "{\"error\":\"falta parametro path\"}");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        std::string path(filePath);

        if (!std::filesystem::exists(path))
        {
            crow::response res(404, "{\"error\":\"archivo no encontrado\"}");
            res.add_header("Content-Type", "application/json");
            res.add_header("Access-Control-Allow-Origin", "*");
            return res;
        }

        // Detectar Content-Type segun la extension
        std::string ext;
        auto dotPos = path.rfind('.');
        if (dotPos != std::string::npos) ext = path.substr(dotPos + 1);

        std::string mime = "application/octet-stream";
        if (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
        else if (ext == "png")  mime = "image/png";
        else if (ext == "svg")  mime = "image/svg+xml";
        else if (ext == "pdf")  mime = "application/pdf";
        else if (ext == "txt")  mime = "text/plain; charset=utf-8";

        // Leer el archivo completo y enviarlo
        std::ifstream f(path, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

        crow::response res(200, body);
        res.add_header("Content-Type",                mime);
        res.add_header("Access-Control-Allow-Origin", "*");
        return res; });

        CROW_LOG_INFO << "Servidor iniciado en http://0.0.0.0:" << port;
        CROW_LOG_INFO << "Endpoint: POST /execute  body: {\"commands\":\"...\"}";

        app.port(port).multithreaded().run();
    }

} // namespace HttpServer
