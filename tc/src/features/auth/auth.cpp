#include "auth.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>
#include <Windows.h>

auth_manager& auth_manager::get_instance() {
    static auth_manager instance;
    return instance;
}

auth_manager::auth_manager() : authenticated(false) {
    char* appdata_path = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata_path, &len, "APPDATA") == 0 && appdata_path != nullptr) {
        config_path = std::string(appdata_path) + "\\Turtle.Club\\auth.json";
        free(appdata_path);
    } else {
        config_path = "C:\\Users\\Public\\Turtle.Club\\auth.json";
    }
    std::filesystem::create_directories(std::filesystem::path(config_path).parent_path());
}

auth_manager::~auth_manager() {
    if (api_instance && authenticated) {
        api_instance->logout();
    }
}

bool auth_manager::initialize() {
    std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
    if (!std::filesystem::exists(config_dir)) {
        std::filesystem::create_directories(config_dir);
    }
    
    if (!std::filesystem::exists(config_path)) {
        std::ofstream file(config_path);
        if (file.is_open()) {
            file << "{}";
            file.close();
        }
    }
    
    std::string name = "turtle";
    std::string ownerid = "r3IUV1w0GB";
    std::string version = "1.0";
    std::string url = "https://keyauth.win/api/1.3/";
    std::string path = config_path;
    
    api_instance = std::make_unique<KeyAuth::api>(name, ownerid, version, url, path, false);
    
    try {
        api_instance->init();
        load_saved_credentials();
        return true;
    }
    catch (...) {
        load_saved_credentials();
        return true;
    }
}

bool auth_manager::is_authenticated() const {
    return authenticated;
}

bool auth_manager::login(const std::string& username, const std::string& password, const std::string& key) {
    if (!api_instance) {
        error_message = "KeyAuth not initialized";
        return false;
    }
    
    try {
        if (!key.empty()) {
            api_instance->license(key);
        } else {
            api_instance->login(username, password);
        }
        
        if (api_instance->response.success) {
            authenticated = true;
            if (!key.empty()) {
                save_credentials("", key);
            } else {
                save_credentials(username, password);
            }
            error_message.clear();
            
            std::thread([this]() {
                check_authentication();
            }).detach();
            
            return true;
        } else {
            error_message = api_instance->response.message;
            authenticated = false;
            return false;
        }
    }
    catch (const std::exception& e) {
        error_message = e.what();
        authenticated = false;
        return false;
    }
    catch (...) {
        error_message = "Unknown error during login";
        authenticated = false;
        return false;
    }
}

bool auth_manager::register_user(const std::string& username, const std::string& password, const std::string& key, const std::string& email) {
    if (!api_instance) {
        error_message = "KeyAuth not initialized";
        return false;
    }
    
    try {
        api_instance->regstr(username, password, key, email);
        
        if (api_instance->response.success) {
            authenticated = true;
            save_credentials(username, password);
            error_message.clear();
            return true;
        } else {
            error_message = api_instance->response.message;
            authenticated = false;
            return false;
        }
    }
    catch (const std::exception& e) {
        error_message = e.what();
        authenticated = false;
        return false;
    }
    catch (...) {
        error_message = "Unknown error during registration";
        authenticated = false;
        return false;
    }
}

void auth_manager::logout() {
    if (api_instance && authenticated) {
        api_instance->logout();
    }
    authenticated = false;
    
    if (std::filesystem::exists(config_path)) {
        std::filesystem::remove(config_path);
    }
}

void auth_manager::check_authentication() {
    while (authenticated) {
        if (api_instance) {
            try {
                api_instance->check();
                if (!api_instance->response.success) {
                    authenticated = false;
                    break;
                }
            }
            catch (...) {
                authenticated = false;
                break;
            }
        }
        Sleep(5000);
    }
}

std::string auth_manager::get_username() const {
    if (api_instance && authenticated) {
        return api_instance->user_data.username;
    }
    return "";
}

std::string auth_manager::get_error_message() const {
    return error_message;
}

std::string auth_manager::get_config_path() const {
    return config_path;
}

void auth_manager::load_saved_credentials() {
    if (!std::filesystem::exists(config_path)) {
        return;
    }
    
    try {
        if (CheckIfJsonKeyExists(config_path, "username") && CheckIfJsonKeyExists(config_path, "password")) {
            std::string username = ReadFromJson(config_path, "username");
            std::string password = ReadFromJson(config_path, "password");
            
            if (username.empty() && !password.empty()) {
                login("", "", password);
            } else if (!username.empty() && !password.empty()) {
                login(username, password);
            }
        }
    }
    catch (...) {
    }
}

void auth_manager::save_credentials(const std::string& username, const std::string& password) {
    try {
        WriteToJson(config_path, "username", username, true, "password", password);
    }
    catch (...) {
    }
}
