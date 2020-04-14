#include "received_message.h"

namespace mirai
{
    void from_json(const utils::json& json, ReceivedMessage& value)
    {
        MessageChain chain;
        for (auto& node : json)
        {
            if (node["type"] == "Source")
                value.source = node.get<msg::Source>();
            else if (node["type"] == "Quote")
                value.quote = node.get<msg::Quote>();
            else
                chain.emplace_back(node.get<Segment>());
        }
        value.content = std::move(chain);
    }
}
