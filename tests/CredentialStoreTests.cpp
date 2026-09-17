#include "security/CredentialStore.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const std::filesystem::path root = "credential-store-test-runtime";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    try {
        routerai::CredentialStore store(root);
        const std::string reference = "routerai-test-credential";
        const std::string first = "secret-one";
        const std::string second = "secret-two";

        store.erase(reference);
        require(!store.exists(reference), "credential must not exist before put");
        require(!store.get(reference).has_value(), "missing credential lookup must return nullopt");

        store.put(reference, first);
        require(store.exists(reference), "credential must exist after put");
        require(store.get(reference).value_or("") == first, "credential round-trip mismatch");

        store.put(reference, second);
        require(store.get(reference).value_or("") == second, "credential overwrite mismatch");

        store.erase(reference);
        require(!store.exists(reference), "credential must not exist after erase");
        require(!store.get(reference).has_value(), "erased credential lookup must return nullopt");

        bool emptyRejected = false;
        try {
            store.put("empty-secret", "");
        } catch (const std::exception&) {
            emptyRejected = true;
        }
        require(emptyRejected, "empty credential secrets must be rejected");

        std::filesystem::remove_all(root, ignored);
        std::cout << "CredentialStoreTests: OK\n";
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, ignored);
        std::cerr << "CredentialStoreTests: FAILED: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
