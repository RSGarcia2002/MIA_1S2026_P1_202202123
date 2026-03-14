#pragma once
#include <string>
#include <map>

// Procesa la línea de comando y despacha al módulo correcto. Devuelve SIEMPRE un string con el resultado para que HttpServer pueda devolverlo al frontend.
namespace Analyzer
{
    // Procesar una linea de comando completa
    std::string Analyze(const std::string &input);

    // Procesar un bloque de comandos (script .smia completo)
    std::string AnalyzeScript(const std::string &script);

    // Extrae todos los parámetros -key=value de una línea
    std::map<std::string, std::string> ParseParams(const std::string &input);
}