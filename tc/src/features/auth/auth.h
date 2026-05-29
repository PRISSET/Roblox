#pragma once

#include <string>
#include <memory>
#include "../../../keyauth/auth.hpp"
#include "../../../keyauth/utils.hpp"

class auth_manager {
public:
    static auth_manager& get_instance();
    
    bool initialize();
    bool is_authenticated() const;
    bool login(const std::string& username, const std::string& password, const std::string& key = "");
    bool register_user(const std::string& username, const std::string& password, const std::string& key, const std::string& email = "");
    void logout();
    void check_authentication();
    
    std::string get_username() const;
    std::string get_error_message() const;
    std::string get_config_path() const;
    
private:
    auth_manager();
    ~auth_manager();
    auth_manager(const auth_manager&) = delete;
    auth_manager& operator=(const auth_manager&) = delete;
    
    std::unique_ptr<KeyAuth::api> api_instance;
    bool authenticated;
    std::string error_message;
    std::string config_path;
    
    void load_saved_credentials();
    void save_credentials(const std::string& username, const std::string& password);
};
