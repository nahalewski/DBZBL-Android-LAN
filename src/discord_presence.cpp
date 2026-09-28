#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>

#include <cstdint>
#include <ctime>
#include <memory>

namespace {

constexpr std::uint64_t kDiscordApplicationId = 1522060011671519383ULL;

class DiscordPresence {
public:
    DiscordPresence() {
        client_ = std::make_shared<discordpp::Client>();
        client_->SetApplicationId(kDiscordApplicationId);

        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Playing);
        activity.SetName("Dragon Ball Z: Burst Limit Recomp");
        activity.SetDetails("In Battle");

        discordpp::ActivityAssets assets;
        assets.SetLargeImage("burstlimit");
        assets.SetLargeText("Dragon Ball Z: Burst Limit Recomp");
        activity.SetAssets(assets);

        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(std::time(nullptr));
        activity.SetTimestamps(timestamps);

        client_->UpdateRichPresence(
            activity,
            [](const discordpp::ClientResult&) {});
    }

private:
    std::shared_ptr<discordpp::Client> client_;
};

DiscordPresence g_discord_presence;

}
