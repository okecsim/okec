#include "resource.h"
#include "format_helper.hpp"
#include <random>


#define CHECK_INDEX(index) \
if (index > size()) throw std::out_of_range{"index out of range"}


namespace okec
{

NS_OBJECT_ENSURE_REGISTERED(resource);


auto resource::GetTypeId() -> TypeId
{
    static TypeId tid = TypeId("okec::resource")
                        .SetParent(Object::GetTypeId())
                        .AddConstructor<resource>();
    return tid;
}

auto resource::install(Ptr<Node> node) -> void
{
    node->AggregateObject(this);
    node_ = node;
}

resource::resource(json item) noexcept
{
    if (item.contains("/resource"_json_pointer)) {
        j_ = std::move(item);
    }
}

auto resource::attribute(std::string_view key, std::string_view value) -> void
{
    j_["resource"][key] = value;
}

auto resource::reset_value(std::string_view key, std::string_view value) -> std::string
{
    auto old_value = std::exchange(j_["resource"][key], value);
    if (monitor_) {
        monitor_(fmt::format("{:ip}", get_address()), key, old_value.get<std::string>(), value);
    }
    
    return old_value;
}

auto resource::set_monitor(monitor_type monitor) -> void
{
    monitor_ = monitor;
}

auto resource::get_value(std::string_view key) const -> std::string
{
    std::string result{};
    json::json_pointer j_key{ "/resource/" + std::string(key) };
    if (j_.contains(j_key))
        j_.at(j_key).get_to(result);
    
    return result;
}

auto resource::get_address() -> Ipv4Address
{
    auto ipv4 = node_->GetObject<Ipv4>();
    return ipv4->GetAddress(1, 0).GetLocal();
}

auto resource::dump(const int indent) -> std::string
{
    return j_.dump(indent);
}

auto resource::empty() const -> bool
{
    return !j_.contains("/resource"_json_pointer);
}

auto resource::j_data() const -> json
{
    return j_;
}

auto resource::set_data(json item) -> bool
{
    if (item.contains("/resource"_json_pointer)) {
        j_ = std::move(item);
        return true;
    }

    return false;
}

auto resource::from_msg_packet(Ptr<Packet> packet) -> resource
{
    json j = packet_helper::to_json(packet);
    if (!j.is_null() && j.contains("/content/resource"_json_pointer))
        return resource(j["content"]);
    
    return resource{};
}

auto make_resource() -> Ptr<resource>
{
    return ns3::CreateObject<resource>();
}

resource_container::resource_container(std::size_t n)
{
    m_resources.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        m_resources.emplace_back(make_resource());
}

auto resource_container::operator[](std::size_t index) -> Ptr<resource>
{
    return get(index);
}

auto resource_container::operator()(std::size_t index) -> Ptr<resource>
{
    return get(index);
}

auto resource_container::get(std::size_t index) -> Ptr<resource>
{
    CHECK_INDEX(index);
    return m_resources[index];
}

auto resource_container::initialize(std::function<void(Ptr<resource>)> fn) -> void
{
    for (auto& item : m_resources) {
        fn(item);
    }
}

std::size_t resource_container::size() const
{
    return m_resources.size();
}

auto resource_container::print(std::string title) -> void
{
    fmt::print("{0:=^{1}}\n", title, 150);

    int index = 1;
    for (const auto& item : m_resources)
    {
        fmt::print("[{:>3}] ", index++);
        for (auto it = item->begin(); it != item->end(); ++it)
        {
            fmt::print("{}: {} ", it.key(), it.value());
        }

        fmt::print("\n");
    }

    fmt::print("{0:=^{1}}\n", "", 150);
}

auto resource_container::trace_resource() -> void
{
    static std::ofstream file;
    if (!file.is_open()) {
        file.open("resource_tracer.csv", std::ios::out/* | std::ios::app*/);
        if (!file.is_open()) {
            return;
        }
    }

    // for (const auto& item : m_resources) {
    //     file << fmt::format("At time {:.2f}s,{:ip}", Simulator::Now().GetSeconds(), item->get_address());
    //     for (auto it = item->begin(); it != item->end(); ++it) {
    //         file << fmt::format(",{}: {}", it.key(), it.value());
    //     }
    //     file << "\n";
    // }
    // file << "\n";
    file << fmt::format("{:.2f}", Simulator::Now().GetSeconds());
    for (const auto& item : m_resources) {
        for (auto it = item->begin(); it != item->end(); ++it) {
            file << fmt::format(",{}", it.value());
        }
    }
    file << "\n";
}

auto resource_container::save_to_file(const std::string& file) -> void
{
    json data;
    for (const auto& item : m_resources) {
        data["resource"]["items"].emplace_back(item->j_data());
    }

    std::ofstream fout(file);
    fout << std::setw(4) << data << std::endl;
}

auto resource_container::load_from_file(const std::string& file) -> bool
{
    std::ifstream fin(file);
    if (!fin.is_open())
        return false;

    json data;
    fin >> data;


    if (!data.contains("/resource/items"_json_pointer) || data["resource"]["items"].size() != this->size())
        return false;
    
    
    fmt::print("Items: \n");
    auto&& items = data["resource"]["items"];
    for (auto i = 0uz; i < size(); ++i) {
        fmt::print("{}\n", items[i].dump(4));
        m_resources[i]->set_data(std::move(items[i]));
    }

    return true;
}

auto resource_container::set_monitor(resource::monitor_type monitor) -> void
{
    for (const auto& item : m_resources) {
        item->set_monitor(monitor);
    }
}

} // namespace okec