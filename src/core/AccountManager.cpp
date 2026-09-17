#include "core/AccountManager.hpp"

#include "providers/antigravity/AntigravityApiClient.hpp"
#include "providers/antigravity/AntigravityProvider.hpp"
#include "providers/codex/CodexProvider.hpp"
#include "providers/zai/ZaiClient.hpp"
#include "providers/zai/ZaiProvider.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace routerai {

namespace {

void applyProfile(Account& account, const AccountProfile& profile) {
    if (!profile.email.empty()) {
        account.email = profile.email;
        account.displayName = profile.email;
    }
    if (!profile.planType.empty()) {
        account.planType = profile.planType;
    }
}

void appendDetail(std::string& detail, const std::string& extra) {
    if (extra.empty()) return;
    if (!detail.empty()) detail += " | ";
    detail += extra;
}

std::optional<AccountStatus> statusFromQuota(const QuotaSnapshot& snapshot) {
    if (!snapshot.ordinaryUsageAllowed.has_value()) return std::nullopt;
    if (!*snapshot.ordinaryUsageAllowed) return AccountStatus::Limited;

    double highestUsedPercent = 0.0;
    bool reachedSignal = false;
    for (const auto& bucket : snapshot.buckets) {
        reachedSignal = reachedSignal || !bucket.reachedType.empty();
        for (const auto& window : bucket.windows) {
            highestUsedPercent = std::max(highestUsedPercent, window.usedPercent);
        }
    }
    if (reachedSignal || highestUsedPercent >= 90.0) return AccountStatus::Warning;
    return AccountStatus::Ready;
}

std::optional<double> latestUsedPercent(const std::vector<QuotaHistoryEntry>& history) {
    if (history.empty()) return std::nullopt;
    const std::int64_t latestSnapshotId = history.front().snapshotId;
    std::optional<double> highest;
    for (const auto& entry : history) {
        if (entry.snapshotId != latestSnapshotId) break;
        if (!highest || entry.usedPercent > *highest) highest = entry.usedPercent;
    }
    return highest;
}

std::vector<std::string> parseOpenAiModelIds(const std::string& body) {
    const auto json = nlohmann::json::parse(body);
    std::vector<std::string> models;
    const auto data = json.find("data");
    if (data != json.end() && data->is_array()) {
        for (const auto& item : *data) {
            if (!item.is_object()) continue;
            const auto id = item.find("id");
            if (id != item.end() && id->is_string()) models.push_back(id->get<std::string>());
        }
    }
    return models;
}

std::string httpFailureDetail(const std::string& provider, const HttpResponse& response) {
    if (!response.error.empty()) return provider + " validation failed: " + response.error;
    return provider + " validation failed with HTTP " + std::to_string(response.statusCode);
}

void markCredentialFailure(Account& account, const HttpResponse& response) {
    account.status = response.statusCode == 401 || response.statusCode == 403
        ? AccountStatus::AuthExpired
        : AccountStatus::Error;
}

}  // namespace

AccountManager::AccountManager(SQLiteDatabase& database, CredentialStore& credentials)
    : database_(database), credentials_(credentials) {}

Account AccountManager::addCodexAccount() {
    CodexProvider provider;
    Account account = provider.createPlaceholderAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
}

Account AccountManager::addAntigravityAccount() {
    for (const auto& account : database_.listAccounts()) {
        if (account.provider == "antigravity" && account.providerMode == "consumer-cli") return account;
    }
    AntigravityProvider provider;
    Account account = provider.createPlaceholderAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
}

Account AccountManager::addAntigravityApiProject() {
    AntigravityProvider provider;
    Account account = provider.createApiProjectAccount(nextAccountId(provider.name()));
    database_.insertAccount(account);
    return account;
}

Account AccountManager::addZaiAccount(const std::string& mode) {
    ZaiProvider provider;
    Account account = provider.createAccount(nextAccountId(provider.name()), mode);
    database_.insertAccount(account);
    return account;
}

AccountAuthOutcome AccountManager::configureAntigravityApiKey(
    const std::string& accountId,
    const std::string& apiKey) {
    auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);
    if (account->provider != "antigravity" || account->providerMode != "api-project") {
        throw std::runtime_error("Account is not an Antigravity API project: " + accountId);
    }

    const HttpResponse validation = AntigravityApiClient::listModels(apiKey);
    if (!validation.succeeded()) {
        markCredentialFailure(*account, validation);
        account->lastError = httpFailureDetail("Gemini API", validation);
        database_.updateAccount(*account);
        return {*account, AuthStatus{false, account->lastError}};
    }

    const std::string reference = account->id + "-api-key";
    credentials_.put(reference, apiKey);
    account->credentialRef = reference;
    account->displayName = "Antigravity API project";
    account->planType = "gemini-api";
    account->status = AccountStatus::Ready;
    account->lastError.clear();
    account->consecutiveFailures = 0;
    account->cooldownUntilUnix.reset();
    database_.updateAccount(*account);

    const auto models = parseOpenAiModelIds(validation.body);
    return {
        *account,
        AuthStatus{true, "Gemini API credential validated; " + std::to_string(models.size()) + " models visible"}};
}

AccountAuthOutcome AccountManager::configureZaiApiKey(
    const std::string& accountId,
    const std::string& apiKey,
    const std::string& mode) {
    auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);
    if (account->provider != "zai") {
        throw std::runtime_error("Account is not a Z.ai provider account: " + accountId);
    }
    if (mode != "general-api" && mode != "coding-plan") {
        throw std::runtime_error("Unsupported Z.ai account mode: " + mode);
    }

    const std::string reference = account->id + "-api-key";
    credentials_.put(reference, apiKey);
    account->credentialRef = reference;
    account->providerMode = mode;
    account->planType = mode;
    account->displayName = mode == "coding-plan" ? "Z.ai Coding Plan" : "Z.ai General API";
    account->status = AccountStatus::Warning;
    account->lastError.clear();
    account->consecutiveFailures = 0;
    account->cooldownUntilUnix.reset();
    database_.updateAccount(*account);

    const std::string detail = mode == "general-api"
        ? "Z.ai General API credential stored; it will be verified by the first documented API request"
        : "Z.ai Coding Plan credential stored; automatic general-purpose routing is disabled for this mode";
    return {*account, AuthStatus{true, detail}};
}

AccountLoginOutcome AccountManager::loginAccount(const std::string& accountId, bool useBrowser) {
    auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);

    LoginResult result;
    if (account->provider == "codex") {
        CodexProvider provider;
        result = provider.login(*account, LoginOptions{useBrowser});
        if (result.success) {
            try {
                applyProfile(*account, provider.readProfile(*account));
            } catch (const std::exception& exception) {
                appendDetail(result.detail, "profile sync warning: " + std::string(exception.what()));
            }
        }
    } else if (account->provider == "antigravity" && account->providerMode == "consumer-cli") {
        AntigravityProvider provider;
        result = provider.login(*account, LoginOptions{});
        if (result.success) applyProfile(*account, provider.readProfile(*account));
    } else {
        throw std::runtime_error(
            "Interactive login is not implemented for provider/mode: " +
            account->provider + "/" + account->providerMode);
    }

    database_.updateAccount(*account);
    return {*account, result};
}

AccountAuthOutcome AccountManager::refreshAccountStatus(const std::string& accountId) {
    auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);

    if (account->provider == "zai") {
        const bool configured = !account->credentialRef.empty() && credentials_.exists(account->credentialRef);
        account->status = configured ? AccountStatus::Warning : AccountStatus::AuthExpired;
        database_.updateAccount(*account);
        return {
            *account,
            AuthStatus{
                configured,
                configured
                    ? "Z.ai credential is configured; validation occurs on a documented provider request"
                    : "Z.ai API key is not configured"}};
    }

    if (account->provider == "antigravity" && account->providerMode == "api-project") {
        if (account->credentialRef.empty()) {
            account->status = AccountStatus::AuthExpired;
            database_.updateAccount(*account);
            return {*account, AuthStatus{false, "Antigravity Gemini API key is not configured"}};
        }
        const auto secret = credentials_.get(account->credentialRef);
        if (!secret || secret->empty()) {
            account->status = AccountStatus::AuthExpired;
            database_.updateAccount(*account);
            return {*account, AuthStatus{false, "Antigravity Gemini API credential is unavailable"}};
        }

        const HttpResponse validation = AntigravityApiClient::listModels(*secret);
        if (!validation.succeeded()) {
            markCredentialFailure(*account, validation);
            account->lastError = httpFailureDetail("Gemini API", validation);
            database_.updateAccount(*account);
            return {*account, AuthStatus{false, account->lastError}};
        }
        account->status = AccountStatus::Ready;
        account->lastError.clear();
        database_.updateAccount(*account);
        return {*account, AuthStatus{true, "Antigravity Gemini API credential validated"}};
    }

    AuthStatus auth;
    if (account->provider == "codex") {
        CodexProvider provider;
        auth = provider.authStatus(*account);
        if (auth.authenticated) {
            if (account->status == AccountStatus::AuthExpired || account->status == AccountStatus::Error) {
                account->status = AccountStatus::Ready;
            }
            try {
                applyProfile(*account, provider.readProfile(*account));
            } catch (const std::exception& exception) {
                appendDetail(auth.detail, "profile sync warning: " + std::string(exception.what()));
            }
        }
    } else if (account->provider == "antigravity" && account->providerMode == "consumer-cli") {
        AntigravityProvider provider;
        auth = provider.authStatus(*account);
        if (auth.authenticated) {
            if (account->status == AccountStatus::AuthExpired || account->status == AccountStatus::Error) {
                account->status = AccountStatus::Ready;
            }
            applyProfile(*account, provider.readProfile(*account));
        }
    } else {
        throw std::runtime_error(
            "Status refresh is not implemented for provider/mode: " +
            account->provider + "/" + account->providerMode);
    }

    if (!auth.authenticated) {
        account->status = auth.detail.find("not installed") != std::string::npos
            ? AccountStatus::Error
            : AccountStatus::AuthExpired;
    }
    database_.updateAccount(*account);
    return {*account, auth};
}

void AccountManager::refreshAllAccountStatuses() {
    const auto accounts = database_.listAccounts();
    for (const auto& account : accounts) {
        if (account.provider == "codex" || account.provider == "antigravity" || account.provider == "zai") {
            refreshAccountStatus(account.id);
        }
    }
}

ProviderModelsOutcome AccountManager::discoverModels(const std::string& accountId) const {
    const auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);

    if (account->provider == "antigravity" && account->providerMode == "api-project") {
        if (account->credentialRef.empty()) return {false, {}, "Gemini API key is not configured"};
        const auto secret = credentials_.get(account->credentialRef);
        if (!secret || secret->empty()) return {false, {}, "Gemini API credential is unavailable"};

        const HttpResponse response = AntigravityApiClient::listModels(*secret);
        if (!response.succeeded()) return {false, {}, httpFailureDetail("Gemini API", response)};

        const auto visibleModels = parseOpenAiModelIds(response.body);
        const auto supported = AntigravityApiClient::supportedAgentModels();
        std::vector<std::string> compatible;
        for (const auto& model : supported) {
            if (std::find(visibleModels.begin(), visibleModels.end(), model) != visibleModels.end()) {
                compatible.push_back(model);
            }
        }
        if (compatible.empty()) {
            return {
                true,
                supported,
                "Gemini model listing succeeded; showing the documented Antigravity agent model set"};
        }
        return {
            true,
            compatible,
            "Models visible to this Gemini API credential and supported by Antigravity agent_config"};
    }

    if (account->provider == "zai") {
        const bool codingPlan = account->providerMode == "coding-plan";
        return {
            true,
            codingPlan
                ? ZaiClient::documentedCodingPlanModels()
                : ZaiClient::documentedChatModels(),
            codingPlan
                ? "Z.ai Coding Plan documented model set"
                : "Z.ai public API reference does not expose models.list; showing documented chat-completion models"};
    }

    return {
        false,
        {},
        "Model discovery is not implemented for " + account->provider + "/" + account->providerMode};
}

QuotaSnapshot AccountManager::readQuota(const std::string& accountId) {
    const auto account = database_.findAccount(accountId);
    if (!account) throw std::runtime_error("Account not found: " + accountId);

    QuotaSnapshot snapshot;
    if (account->provider == "codex") {
        CodexProvider provider;
        snapshot = provider.readQuota(*account);
    } else if (account->provider == "antigravity" && account->providerMode == "consumer-cli") {
        AntigravityProvider provider;
        snapshot = provider.readQuota(*account);
    } else if (account->provider == "zai") {
        ZaiProvider provider;
        snapshot = provider.readQuota(*account);
    } else {
        throw std::runtime_error(
            "Quota reads are not implemented for provider/mode: " + account->provider + "/" + account->providerMode);
    }

    database_.recordQuotaSnapshot(accountId, snapshot);
    if (const auto derivedStatus = statusFromQuota(snapshot)) {
        Account updated = *account;
        updated.status = *derivedStatus;
        database_.updateAccount(updated);
    }
    return snapshot;
}

std::vector<QuotaHistoryEntry> AccountManager::listQuotaHistory(
    const std::string& accountId,
    std::size_t limit) const {
    if (!database_.findAccount(accountId)) throw std::runtime_error("Account not found: " + accountId);
    return database_.listQuotaHistory(accountId, limit);
}

std::optional<RoutingCandidate> AccountManager::selectAccount(const std::string& provider) const {
    std::vector<RoutingCandidate> candidates;
    for (const auto& account : database_.listAccounts()) {
        if (account.provider != provider) continue;
        RoutingCandidate candidate;
        candidate.account = account;
        candidate.latestUsedPercent = latestUsedPercent(database_.listQuotaHistory(account.id, 100));
        candidates.push_back(std::move(candidate));
    }
    AccountSelector selector;
    return selector.select(candidates);
}

std::optional<Account> AccountManager::findAccount(const std::string& accountId) const {
    return database_.findAccount(accountId);
}

std::vector<Account> AccountManager::listAccounts() const {
    return database_.listAccounts();
}

std::size_t AccountManager::countAccounts() const {
    return database_.countAccounts();
}

std::string AccountManager::nextAccountId(const std::string& provider) const {
    const auto accounts = database_.listAccounts();
    std::size_t highest = 0;
    const std::string prefix = provider + '-';
    for (const auto& account : accounts) {
        if (!account.id.starts_with(prefix)) continue;
        try {
            const auto suffix = account.id.substr(prefix.size());
            highest = std::max(highest, static_cast<std::size_t>(std::stoul(suffix)));
        } catch (...) {
        }
    }
    std::ostringstream id;
    id << provider << '-' << std::setw(2) << std::setfill('0') << (highest + 1);
    return id.str();
}

}  // namespace routerai
