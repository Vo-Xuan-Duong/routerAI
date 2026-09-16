#include "security/CredentialStore.hpp"

#include <cctype>
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
    return std::filesystem::exists(pathFor(reference));
}

void CredentialStore::erase(const std::string& reference) const {
    std::error_code error;
    std::filesystem::remove(pathFor(reference), error);
    if (error) {
        throw std::runtime_error("Failed to erase credential: " + error.message());
    }
}

}  // namespace routerai
