#pragma once
#include <string>

// Sesion de usuario y administracion de usuarios/grupos
namespace UserSession
{
    // Estado de la sesion activa en memoria
    bool IsLoggedIn();
    std::string GetCurrentUser();
    std::string GetCurrentId(); // ID de la particion montada de la sesion
    int GetCurrentUid();
    int GetCurrentGid();

    // Comandos
    std::string Login(const std::string &user, const std::string &pass, const std::string &id);
    std::string Logout();
    std::string Mkgrp(const std::string &name);
    std::string Rmgrp(const std::string &name);
    std::string Mkusr(const std::string &user, const std::string &pass, const std::string &grp);
    std::string Rmusr(const std::string &user);
    std::string Chgrp(const std::string &user, const std::string &grp);
}
