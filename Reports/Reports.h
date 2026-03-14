#pragma once
#include <string>

// Generacion de reportes con Graphviz
namespace Reports
{
    // Punto de entrada principal: despacha segun el nombre del reporte
    std::string Rep(const std::string &name, const std::string &path,
                    const std::string &id, const std::string &pathFileLs);
}
