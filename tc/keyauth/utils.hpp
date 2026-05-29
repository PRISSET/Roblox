#pragma once
#include <filesystem> 
#include <string> 
#include <fstream>
#include "skStr.h"
#include "json.hpp"
using json = nlohmann::json;

// Function declarations
std::string ReadFromJson(std::string path, std::string section);
bool CheckIfJsonKeyExists(std::string path, std::string section);
bool WriteToJson(std::string path, std::string name, std::string value, bool userpass, std::string name2, std::string value2);
void checkAuthenticated(std::string ownerid);