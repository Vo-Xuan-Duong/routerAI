#include "security/CredentialStore.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#endif

namespace routerai {

namespace {

std::string safeReference(std::string value) {
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '-' && ch != '_' && ch != '.') {
            ch = '_';
        }
    }
    if (value.empty()) {
        throw std::runtime_error("Credential reference cannot be empty");
    }
    return value;
}

std::vector<unsigned char> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open credential file for writing: " + path.string());
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("Failed to write credential file: " + path.string());
    }
#ifndef _WIN32
    chmod(path.string().c_str(), S_IRUSR | S_IWUSR);
#endif
}

#ifdef __linux__

bool secretToolAvailable() {
    const char* sessionBus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!sessionBus || !*sessionBus) {
        return false;
    }
    const int rc = std::system("command -v secret-tool >/dev/null 2>&1");
    return rc == 0;
}

std::string secretToolCommand(const char* action, const std::string& reference) {
    return std::string("secret-tool ") + action +
        " application routerAI reference " + safeReference(reference) +
        " 2>/dev/null";
}

bool storeInSecretService(const std::string& reference, const std::string& secret) {
    if (!secretToolAvailable()) {
        return false;
    }

    const std::string command =
        "secret-tool store --label='routerAI' application routerAI reference " +
        safeReference(reference) + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        return false;
    }

    const std::string payload = secret + '\n';
    const bool wrote =
        std::fwrite(payload.data(), 1, payload.size(), pipe) == payload.size();
    const int rc = pclose(pipe);
    return wrote && rc == 0;
}

std::optional<std::string> readFromSecretService(const std::string& reference) {
    if (!secretToolAvailable()) {
        return std::nullopt;
    }

    const std::string command = secretToolCommand("lookup", reference);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }

    std::array<char, 512> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    const int rc = pclose(pipe);
    if (rc != 0) {
        return std::nullopt;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (output.empty()) {
        return std::nullopt;
    }
    return output;
}

void eraseFromSecretService(const std::string& reference) {
    if (!secretToolAvailable()) {
        return;
    }
    const std::string command = secretToolCommand("clear", reference);
    (void)std::system(command.c_str());
}

#endif

}  // namespace

CredentialStore::CredentialStore(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_);
#ifndef _WIN32
    chmod(root_.string().c_str(), S_IRWXU);
#endif
}

std::filesystem::path CredentialStore::pathFor(const std::string& reference) const {
    return root_ / (safeReference(reference) + ".cred");
}

void CredentialStore::put(const std::string& reference, const std::string& secret) const {
    if (secret.empty()) {
        throw std::runtime_error("Credential secret cannot be empty");
    }

#ifdef __linux__
    if (storeInSecretService(reference, secret)) {
        std::error_code ignored;
        std::filesystem::remove(pathFor(reference), ignored);
        return;
    }
#endif

    std::vector<unsigned char> stored;
#ifdef _WIN32
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()));
    input.cbData = static_cast<DWORD>(secret.size());
    DATA_BLOB encrypted{};
    if (!CryptProtectData(
            &input,
            L"routerAI credential",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &encrypted)) {
        throw std::runtime_error("Windows DPAPI failed to protect credential");
    }
    stored.assign(encrypted.pbData, encrypted.pbData + encrypted.cbData);
    LocalFree(encrypted.pbData);
#else
    stored.assign(secret.begin(), secret.end());
#endif
    writeBytes(pathFor(reference), stored);
}

std::optional<std::string> CredentialStore::get(const std::string& reference) const {
#ifdef __linux__
    if (const auto secret = readFromSecretService(reference)) {
        return secret;
    }
#endif

    const auto path = pathFor(reference);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    const auto stored = readBytes(path);
    if (stored.empty()) {
        throw std::runtime_error("Credential file is empty or unreadable: " + path.string());
    }
#ifdef _WIN32
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(stored.data());
    input.cbData = static_cast<DWORD>(stored.size());
    DATA_BLOB decrypted{};
    if (!CryptUnprotectData(
            &input,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &decrypted)) {
        throw std::runtime_error("Windows DPAPI failed to unprotect credential");
    }
    std::string secret(
        reinterpret_cast<const char*>(decrypted.pbData),
        static_cast<std::size_t>(decrypted.cbData));
    LocalFree(decrypted.pbData);
    return secret;
#else
    return std::string(stored.begin(), stored.end());
#endif
}

bool CredentialStore::exists(const std::string& reference) const {
#ifdef __linux__
    if (readFromSecretService(reference).has_value()) {
        return true;
    }
#endif
    return std::filesystem::exists(pathFor(reference));
}

void CredentialStore::erase(const std::string& reference) const {
#ifdef __linux__
    eraseFromSecretService(reference);
#endif
    std::error_code error;
    std::filesystem::remove(pathFor(reference), error);
    if (error) {
        throw std::runtime_error("Failed to erase credential: " + error.message());
    }
}

}  // namespace routerai
