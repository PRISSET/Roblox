#include "console_login.h"
#include "../auth.h"
#include "../../../ext/console/console.h"
#include <iostream>
#include <string>
#include <conio.h>
#include <Windows.h>

bool console_login::authenticate() {
    auto& auth = auth_manager::get_instance();
    
    if (auth.is_authenticated()) {
        return true;
    }
    
    std::string key;
    
    printf(">");
    std::getline(std::cin, key);
    
    LOG_INFO("Authenticating...");
    
    bool success = auth.login("", "", key);
    
    if (success) {
        LOG_SUCCESS("Successfully authenticated!");
        std::cout << "\n";
        return true;
    } else {
        std::string error = auth.get_error_message();
        if (!error.empty()) {
            LOG_ERROR("Invalid license key");
        } else {
            LOG_ERROR("Authentication failed!");
        }
        std::cout << "\n";
        return false;
    }
}
