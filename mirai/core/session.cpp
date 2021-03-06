#include "session.h"
#include <cpr/cpr.h>
#include "common.h"

namespace mirai
{
    std::vector<std::string> Session::send_image_message(
        const utils::OptionalParam<uid_t> qq,
        const utils::OptionalParam<gid_t> group,
        const utils::ArrayProxy<std::string> urls) const
    {
        utils::json json{
            { "sessionKey", key_ },
            { "urls", std::vector(urls.begin(), urls.end()) }
        };
        if (qq) json["qq"] = *qq;
        if (group) json["group"] = *group;
        const auto res = utils::post_json("/sendImageMessage", json);
        return res.get<std::vector<std::string>>();
    }

    std::vector<Event> Session::get_events(const std::string_view url, const size_t count) const
    {
        const utils::json res = utils::get(url, {
            { "sessionKey", key_ },
            { "count", std::to_string(count) }
        });
        utils::check_response(res);
        return res.at("data").get<std::vector<Event>>();
    }

    Session::Session(const std::string_view auth_key, const uid_t qq)
    {
        // Authorize
        {
            const auto res = utils::post_json("/auth", {
                { "authKey", std::string(auth_key) }
            });
            utils::check_response(res);
            res.at("session").get_to(key_);
        }
        // Verify
        {
            const auto res = utils::post_json("/verify", {
                { "sessionKey", key_ },
                { "qq", qq }
            });
            utils::check_response(res);
            qq_ = qq; // QQ ID set (not 0) means the initialization has completed
        }
    }

    Session::~Session() noexcept
    {
        if (qq_ == 0) return; // Invalid session
        try
        {
            close_websocket_client();
            destroy_thread_pool();
            utils::post_json("/release", {
                { "sessionKey", key_ },
                { "qq", qq_ }
            });
        }
        catch (...) { std::abort(); }
    }

    Session::Session(Session&& other) noexcept :
        qq_(std::exchange(other.qq_, {})),
        key_(std::move(other.key_)),
        client_(std::move(other.client_)),
        thread_pool_(std::move(other.thread_pool_)) {}

    Session& Session::operator=(Session&& other) noexcept
    {
        Session copy(std::move(other));
        swap(copy);
        return *this;
    }

    void Session::swap(Session& other) noexcept
    {
        std::swap(qq_, other.qq_);
        std::swap(key_, other.key_);
        std::swap(client_, other.client_);
        std::swap(thread_pool_, other.thread_pool_);
    }

    void Session::start_thread_pool(const utils::OptionalParam<size_t> thread_count)
    {
        if (!thread_pool_)
        {
            if (thread_count)
                thread_pool_ = std::make_unique<asio::thread_pool>(*thread_count);
            else
                thread_pool_ = std::make_unique<asio::thread_pool>();
        }
    }

    void Session::destroy_thread_pool()
    {
        if (thread_pool_)
            thread_pool_->join();
        thread_pool_.reset();
    }

    msgid_t Session::send_message(const uid_t friend_,
        const Message& msg, const utils::OptionalParam<msgid_t> quote) const
    {
        utils::json json{
            { "sessionKey", key_ },
            { "target", friend_ },
            { "messageChain", msg }
        };
        if (quote.has_value()) json["quote"] = *quote;
        const auto res = utils::post_json("/sendFriendMessage", json);
        utils::check_response(res);
        return res.at("messageId").get<msgid_t>();
    }

    msgid_t Session::send_message(const uid_t qq, const gid_t group,
        const Message& msg, const utils::OptionalParam<msgid_t> quote) const
    {
        utils::json json{
            { "sessionKey", key_ },
            { "qq", qq },
            { "group", group },
            { "messageChain", msg }
        };
        if (quote.has_value()) json["quote"] = *quote;
        const auto res = utils::post_json("/sendTempMessage", json);
        utils::check_response(res);
        return res.at("messageId").get<msgid_t>();
    }

    msgid_t Session::send_message(const gid_t target,
        const Message& msg, const utils::OptionalParam<msgid_t> quote) const
    {
        utils::json json{
            { "sessionKey", key_ },
            { "target", target },
            { "messageChain", msg }
        };
        if (quote.has_value()) json["quote"] = *quote;
        const auto res = utils::post_json("/sendGroupMessage", json);
        utils::check_response(res);
        return res.at("messageId").get<msgid_t>();
    }

    msgid_t Session::send_quote_message(const FriendMessage& quote, const Message& msg) const
    {
        return send_message(quote.sender.id, msg, quote.message.source.id);
    }

    msgid_t Session::send_quote_message(const TempMessage& quote, const Message& msg) const
    {
        return send_message(quote.sender.id, quote.sender.group.id, msg, quote.message.source.id);
    }

    msgid_t Session::send_quote_message(const GroupMessage& quote, const Message& msg) const
    {
        return send_message(quote.sender.group.id, msg, quote.message.source.id);
    }

    std::vector<std::string> Session::send_image_message(
        const uid_t friend_, const utils::ArrayProxy<std::string> urls) const
    {
        return send_image_message(friend_, {}, urls);
    }

    std::vector<std::string> Session::send_image_message(
        const gid_t group, const utils::ArrayProxy<std::string> urls) const
    {
        return send_image_message({}, group, urls);
    }

    std::vector<std::string> Session::send_image_message(const uid_t qq, const gid_t group,
        const utils::ArrayProxy<std::string> urls) const
    {
        return send_image_message(utils::OptionalParam(qq), group, urls);
    }

    msg::Image Session::upload_image(const TargetType type, const std::string& path) const
    {
        static constexpr std::array type_names{ "friend", "group", "temp" };
        const cpr::Response response = Post(
            cpr::Url{ std::string(base_url) += "/uploadImage" },
            cpr::Multipart{
                { "sessionKey", key_ },
                { "type", type_names[size_t(type)] },
                { "img", cpr::File(path) }
            });
        if (response.status_code != 200) // Status code not OK
            throw RuntimeError(response.error.message);
        return utils::json::parse(response.text).get<msg::Image>();
    }

    void Session::recall(const msgid_t message_id) const
    {
        const utils::json json{
            { "sessionKey", key_ },
            { "target", message_id }
        };
        const auto res = utils::post_json("/recall", json);
        utils::check_response(res);
    }

    std::vector<Event> Session::fetch_events(const size_t count) const
    {
        return get_events("/fetchMessage", count);
    }

    std::vector<Event> Session::fetch_latest_events(const size_t count) const
    {
        return get_events("/fetchLatestMessage", count);
    }

    std::vector<Event> Session::peek_events(const size_t count) const
    {
        return get_events("/peekMessage", count);
    }

    std::vector<Event> Session::peek_latest_events(const size_t count) const
    {
        return get_events("/peekLatestMessage", count);
    }

    size_t Session::count_events() const
    {
        const utils::json res = utils::get("/countMessage",
            { { "sessionKey", key_ } });
        utils::check_response(res);
        return res.at("data").get<size_t>();
    }

    Event Session::message_from_id(const msgid_t id) const
    {
        const utils::json res = utils::get("/messageFromId", {
            { "sessionKey", key_ },
            { "id", std::to_string(id) }
        });
        utils::check_response(res);
        return res.at("data").get<Event>();
    }

    std::vector<Friend> Session::friend_list() const
    {
        const utils::json res = utils::get("/friendList",
            { { "sessionKey", key_ } });
        return res.get<std::vector<Friend>>();
    }

    std::vector<Group> Session::group_list() const
    {
        const utils::json res = utils::get("/groupList",
            { { "sessionKey", key_ } });
        return res.get<std::vector<Group>>();
    }

    std::vector<Member> Session::member_list(const gid_t target) const
    {
        const utils::json res = utils::get("/memberList", {
            { "sessionKey", key_ },
            { "target", std::to_string(target) }
        });
        utils::check_response(res);
        return res.get<std::vector<Member>>();
    }

    void Session::mute_all(const gid_t target) const
    {
        const utils::json res = utils::post_json("/muteAll", {
            { "sessionKey", key_ },
            { "target", target }
        });
        utils::check_response(res);
    }

    void Session::unmute_all(const gid_t target) const
    {
        const utils::json res = utils::post_json("/unmuteAll", {
            { "sessionKey", key_ },
            { "target", target }
        });
        utils::check_response(res);
    }

    void Session::mute(const gid_t group, const uid_t member,
        const std::chrono::seconds duration) const
    {
        const utils::json res = utils::post_json("/mute", {
            { "sessionKey", key_ },
            { "target", group },
            { "memberId", member },
            { "time", duration.count() }
        });
        utils::check_response(res);
    }

    void Session::unmute(const gid_t group, const uid_t member) const
    {
        const utils::json res = utils::post_json("/unmute", {
            { "sessionKey", key_ },
            { "target", group },
            { "memberId", member }
        });
        utils::check_response(res);
    }

    void Session::kick(const gid_t group, const uid_t member,
        const std::string_view message) const
    {
        const utils::json res = utils::post_json("/kick", {
            { "sessionKey", key_ },
            { "target", group },
            { "memberId", member },
            { "msg", message }
        });
        utils::check_response(res);
    }

    void Session::quit(const gid_t group) const
    {
        const utils::json res = utils::post_json("/quit", {
            { "sessionKey", key_ },
            { "target", group }
        });
        utils::check_response(res);
    }

    void Session::respond_new_friend_request(const NewFriendRequestEvent& event,
        const NewFriendResponseType type, const std::string_view message) const
    {
        const utils::json res = utils::post_json("/resp/newFriendRequestEvent", {
            { "sessionKey", key_ },
            { "eventId", event.event_id },
            { "fromId", event.from_id },
            { "groupId", event.group_id.value_or(gid_t{}) },
            { "operate", int32_t(type) },
            { "message", message }
        });
        utils::check_response(res);
    }

    void Session::respond_member_join_request(const MemberJoinRequestEvent& event,
        const MemberJoinResponseType type, const std::string_view message) const
    {
        const utils::json res = utils::post_json("/resp/memberJoinRequestEvent", {
            { "sessionKey", key_ },
            { "eventId", event.event_id },
            { "fromId", event.from_id },
            { "groupId", event.group_id },
            { "operate", int32_t(type) },
            { "message", message }
        });
        utils::check_response(res);
    }

    void Session::group_config(const gid_t target, const GroupConfig& config) const
    {
        const utils::json res = utils::post_json("/groupConfig", {
            { "sessionKey", key_ },
            { "target", target },
            { "config", config }
        });
        utils::check_response(res);
    }

    GroupConfig Session::group_config(const gid_t target) const
    {
        const utils::json res = utils::get("/groupConfig", {
            { "sessionKey", key_ },
            { "target", std::to_string(target) }
        });
        return res.get<GroupConfig>();
    }

    void Session::member_info(const gid_t group, const uid_t member,
        const utils::OptionalParam<std::string_view> name,
        const utils::OptionalParam<std::string_view> special_title) const
    {
        utils::json res = utils::post_json("/memberInfo", {
            { "sessionKey", key_ },
            { "target", group },
            { "memberId", member },
            { "info", {} }
        });
        if (name) res["info"]["name"] = *name;
        if (special_title) res["info"]["specialTitle"] = *special_title;
        utils::check_response(res);
    }

    MemberInfo Session::member_info(const gid_t group, const uid_t member) const
    {
        const utils::json res = utils::get("/memberInfo", {
            { "sessionKey", key_ },
            { "target", std::to_string(group) },
            { "memberId", std::to_string(member) },
        });
        return res.get<MemberInfo>();
    }

    void Session::close_connection(ws::Connection& connection) { client_->close(connection); }

    void Session::config(const utils::OptionalParam<size_t> cache_size,
        const utils::OptionalParam<bool> enable_websocket) const
    {
        utils::json json{ { "sessionKey", key_ } };
        if (cache_size) json["cacheSize"] = *cache_size;
        if (enable_websocket) json["enableWebsocket"] = *enable_websocket;
        (void)utils::post_json_no_parse("/config", json);
    }

    SessionConfig Session::config() const
    {
        return utils::get("/config", { { "sessionKey", key_ } })
           .get<SessionConfig>();
    }
}
