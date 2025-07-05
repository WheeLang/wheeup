// wheeup.cpp
// Updated to support full YAML schema including users, directories, remotes, content, and dependencies

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdlib>
#include <yaml-cpp/yaml.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
namespace fs = std::filesystem;
using json = nlohmann::json;

// Helper for downloading JSON content as string
std::string download_text(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string result;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            ((std::string*)userdata)->append((char*)ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) return "";
    }

    return result;
}

const std::string BASE_URL = "https://raw.githubusercontent.com/WheeLang/wheedb/main/packages/";
const std::string TMP_DIR = "/tmp/wheeup";

size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    size_t totalSize = size * nmemb;
    out->write(static_cast<char*>(ptr), totalSize);
    return totalSize;
}

bool download_file(const std::string& url, const std::string& output_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream out(output_path, std::ios::binary);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out.close();
    return res == CURLE_OK;
}

void create_user_and_group(const YAML::Node& users) {
    std::string group = users["group"].as<std::string>();
    std::string user = users["user"].as<std::string>();
    std::string whoami = getenv("USER") ? getenv("USER") : "";

    std::string cmd;
    cmd = "getent group " + group + " >/dev/null || sudo groupadd " + group;
    std::system(cmd.c_str());

    cmd = "sudo usermod -aG " + group + " " + whoami;
    std::system(cmd.c_str());

    cmd = "sudo usermod -aG " + group + " root";
    std::system(cmd.c_str());

    cmd = "id -u " + user + " >/dev/null 2>&1 || sudo useradd -r -s /usr/sbin/nologin -g " + group + " " + user;
    std::system(cmd.c_str());
}

void create_directories(const YAML::Node& dirs) {
    for (const auto& dir : dirs) {
        std::string path = dir["path"].as<std::string>();
        std::string owner = dir["owner"].as<std::string>("");
        std::string group = dir["group"].as<std::string>("");
        std::string perms = dir["permissions"].as<std::string>("");

        std::string cmd = "sudo mkdir -p " + path + "; ";
        if (!owner.empty() && !group.empty()) {
            cmd += "sudo chown -R " + owner + ":" + group + " " + path + "; ";
        }
        if (!perms.empty()) {
            cmd += "sudo chmod -R " + perms + " " + path;
        }
        std::system(cmd.c_str());
    }
}

void install_remotes(const YAML::Node& remotes) {
    for (const auto& remote : remotes) {
        std::string url = remote["url"].as<std::string>();
        std::string target = remote["target"].as<std::string>();
        fs::create_directories(fs::path(target).parent_path());
        download_file(url, "/tmp/remote.yml");
        std::string cmd = "sudo cp /tmp/remote.yml " + target;
        std::system(cmd.c_str());
    }
}

void install_content(const YAML::Node& content) {
    for (const auto& item : content) {
        std::string name = item["name"].as<std::string>();
        std::string source = item["source"].as<std::string>();
        std::string location = item["location"].as<std::string>();
        std::string binlink = item["binlink"].as<std::string>("");

        std::string temp_path = TMP_DIR + "/" + name;
        std::cout << "Installing " << name << "..." << std::endl;
        if (download_file(source, temp_path)) {
            std::string cmd = "sudo cp " + temp_path + " " + location + "; sudo chmod +x " + location;
            std::system(cmd.c_str());
            if (!binlink.empty()) {
                cmd = "sudo ln -sf " + location + " " + binlink;
                std::system(cmd.c_str());
            }
        } else {
            std::cerr << "Failed to download " << name << std::endl;
        }
    }
}

void install_dependencies(const YAML::Node& deps) {
    for (const auto& dep : deps) {
        std::string name = dep["name"].as<std::string>();
        std::string source = dep["source"].as<std::string>();
        std::string binary = dep["binary"].as<std::string>();
        std::string location = dep["location"].as<std::string>();
        std::string binlink = dep["binlink"].as<std::string>("");

        std::cout << "Installing dependency: " << name << std::endl;

        std::string json_str = download_text(source);
        if (json_str.empty()) {
            std::cerr << "Failed to fetch release info from: " << source << std::endl;
            continue;
        }

        json release = json::parse(json_str);
        std::string url;
        for (const auto& asset : release["assets"]) {
            if (asset["name"] == binary) {
                url = asset["browser_download_url"];
                break;
            }
        }

        if (url.empty()) {
            std::cerr << "Could not find binary " << binary << " in GitHub release.\n";
            continue;
        }

        std::string temp_path = TMP_DIR + "/" + binary;
        if (!download_file(url, temp_path)) {
            std::cerr << "Failed to download binary from: " << url << std::endl;
            continue;
        }

        std::string cmd = "sudo cp " + temp_path + " " + location + "; sudo chmod +x " + location;
        std::system(cmd.c_str());

        if (!binlink.empty()) {
            cmd = "sudo ln -sf " + location + " " + binlink;
            std::system(cmd.c_str());
        }
    }
}

void install_package(const std::string& package_name) {
    std::string yaml_url = BASE_URL + package_name + ".yml";
    std::string local_yaml_path = TMP_DIR + "/package.yml";

    std::cout << "Fetching package info from: " << yaml_url << std::endl;
    fs::create_directories(TMP_DIR);
    if (!download_file(yaml_url, local_yaml_path)) {
        std::cerr << "Failed to download package YAML." << std::endl;
        return;
    }

    YAML::Node config = YAML::LoadFile(local_yaml_path);

    create_user_and_group(config["users"]);
    create_directories(config["directories"]);
    install_remotes(config["remotes"]);
    install_content(config["content"]);
    install_dependencies(config["dependencies"]);

    std::cout << "Installation complete." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "install") {
        std::cerr << "Usage: wheeup install <package>" << std::endl;
        return 1;
    }

    std::string package = argv[2];
    install_package(package);
    return 0;
}
