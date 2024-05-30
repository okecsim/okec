#include <okec/algorithms/classic/cloud_edge_end_default_decision_engine.h>
#include <okec/common/message.h>
#include <okec/devices/base_station.h>
#include <okec/devices/client_device.h>
#include <okec/devices/cloud_server.h>
#include <okec/devices/edge_device.h>
#include <okec/utils/log.h>
#include <functional> // bind_front


namespace okec
{

cloud_edge_end_default_decision_engine::cloud_edge_end_default_decision_engine(
    client_device_container* clients,
    base_station_container* base_stations,
    cloud_server* cloud)
    : clients_{clients}
    , base_stations_{base_stations}
{
    // 设置决策设备
    m_decision_device = base_stations->get(0);

    // 初始化资源缓存信息
    this->initialize_device(base_stations, cloud);

    // Capture decision message
    base_stations->set_request_handler(message_decision, std::bind_front(&this_type::on_bs_decision_message, this));
    base_stations->set_request_handler(message_response, std::bind_front(&this_type::on_bs_response_message, this));

    // Capture es handling message
    base_stations->set_es_request_handler(message_handling, std::bind_front(&this_type::on_es_handling_message, this));

    // Capture clients response message
    clients->set_request_handler(message_response, std::bind_front(&this_type::on_clients_reponse_message, this));

    // Capture cloud handling message
    cloud->set_request_handler(message_handling, std::bind_front(&this_type::on_cloud_handling_message, this));

    // okec::print("cache: \n{}\n", this->cache().dump(4));
}

auto cloud_edge_end_default_decision_engine::make_decision(
    const task_element &header) -> result_t
{
    // 获取边缘设备数据
    auto edge_cache = this->cache().data();
    edge_cache.erase(std::remove_if(edge_cache.begin(), edge_cache.end(), [](const device_cache::value_type& item) {
        return item["device_type"] == "cs";
    }));
    // okec::print("edge cache: {}\n", edge_cache.dump(2));

    auto edge_max = *std::max_element(edge_cache.begin(), edge_cache.end(), 
        [](const device_cache::value_type& lhs, const device_cache::value_type& rhs) {
            return TO_DOUBLE(lhs["cpu"]) < TO_DOUBLE(rhs["cpu"]);
        });
    // fmt::print("edge max: {}\n", TO_STR(edge_max["ip"]));

    double cpu_demand = std::stod(header.get_header("cpu"));
    double cpu_supply = TO_DOUBLE(edge_max["cpu"]);
    double tolorable_time = std::stod(header.get_header("deadline"));
    [[maybe_unused]] double size = std::stod(header.get_header("size"));

    // okec::print("cpu_demand: {}, cpu_supply: {}, tolorable_time: {}, size: {}\n", cpu_demand, cpu_supply, tolorable_time, size);

    if (cpu_supply >= cpu_demand) {
        double processing_time = cpu_demand / cpu_supply;
        // okec::print("processing time: {}\n", processing_time);

        // 能够满足时延要求
        if (processing_time < tolorable_time) {
            return {
                { "ip", edge_max["ip"] },
                { "port", edge_max["port"] },
                { "cpu_supply", std::to_string(cpu_supply) },
                { "type", "es" }
            };
        }
    }

    // Otherwise, dispatch the task to cloud.
    auto it = this->cache().find_if([](const device_cache::value_type& item) {
        return item["device_type"] == "cs";
    });
    if (it != this->cache().end()) {
        const auto& device = *it;
        return {
            { "ip", device["ip"] },
            { "port", device["port"] },
            { "type", "cs" }
        };
    }

    return result_t();
}

auto cloud_edge_end_default_decision_engine::local_test(
    const task_element &header,
    client_device *client) -> bool
{
    return false;
}

auto cloud_edge_end_default_decision_engine::send(
    task_element t,
    std::shared_ptr<client_device> client) -> bool
{
    static double launch_delay = 0.3;

    client->response_cache().emplace_back({
        { "task_id", t.get_header("task_id") },
        { "group", t.get_header("group") },
        { "finished", "0" }, // 0: unfinished, Y: finished, N: offloading failure
        { "device_type", "" },
        { "device_address", "" },
        { "time_consuming", "" }
    });

    // fmt::print("Received tasks:\n{}\n", t.j_data().dump(4));

    // 不管本地，全部往边缘服务器卸载
    t.set_header("from_ip", fmt::format("{:ip}", client->get_address()));
    t.set_header("from_port", std::to_string(client->get_port()));
    message msg;
    msg.type(message_decision);
    msg.content(t);
    const auto bs = this->get_decision_device();
    auto write = [client, bs, content = msg.to_packet()]() {
        client->write(content, bs->get_address(), bs->get_port());
    };
    ns3::Simulator::Schedule(ns3::Seconds(launch_delay), write);
    launch_delay += 0.01;

    return true;
}

auto cloud_edge_end_default_decision_engine::initialize() ->void
{
    if (clients_) {
        clients_->set_decision_engine(shared_from_base<this_type>());
    }

    if (base_stations_) {
        base_stations_->set_decision_engine(shared_from_base<this_type>());
    }
}

auto cloud_edge_end_default_decision_engine::train(const task &t, int episode) -> void
{
}

auto cloud_edge_end_default_decision_engine::handle_next() -> void
{
    auto& task_sequence = m_decision_device->task_sequence();
    log::info("handle_next.... current task sequence size: {}", task_sequence.size());

    if (auto it = std::ranges::find_if(task_sequence, [](auto const& item) {
        return item.get_header("status") == "0";
    }); it != std::end(task_sequence)) {
        auto target = make_decision(*it);
        // 决策失败，无法处理任务
        if (target.is_null()) {
            log::error("No device can handle the task({})!", it->get_header("task_id"));
            return;
        }

        message msg;
        msg.type(message_handling);
        msg.content(*it);

        // 卸载到边缘
        if (target["type"] == "es") {
            // okec::print("target ip: {}\n", TO_STR(target["ip"]));
            msg.attribute("cpu_supply", TO_STR(target["cpu_supply"]));
            it->set_header("status", "1"); // 更改任务分发状态
        }

        // 卸载到云端
        if (target["type"] == "cs") {
            log::warning("Offloading to cloud");
        }

        it->set_header("status", "1"); // 更改任务分发状态
        m_decision_device->write(msg.to_packet(), ns3::Ipv4Address(TO_STR(target["ip"]).c_str()), TO_INT(target["port"]));
    }
}

auto cloud_edge_end_default_decision_engine::on_bs_decision_message(
    base_station *bs,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
    // fmt::print("Resource cache:\n{}\n", this->cache().dump(4));

    // task_element 为单位
    auto item = okec::task_element::from_msg_packet(packet);
    item.set_header("status", "0"); // 增加处理状态信息 0: 未处理 1: 已处理
    bs->task_sequence(std::move(item));

    this->handle_next();
}

auto cloud_edge_end_default_decision_engine::on_bs_response_message(
    base_station *bs,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
    message msg(packet);
    auto& task_sequence = bs->task_sequence();

    if (auto it = std::ranges::find_if(task_sequence, [&msg](auto const& item) {
        return item.get_header("task_id") == msg.get_value("task_id");
    }); it != std::end(task_sequence)) {
        msg.attribute("group", (*it).get_header("group"));
        auto from_ip = (*it).get_header("from_ip");
        auto from_port = (*it).get_header("from_port");
        bs->write(msg.to_packet(), ns3::Ipv4Address(from_ip.c_str()), std::stoi(from_port));

        // 处理过的任务从队列中清除
        task_sequence.erase(it);
    }

    this->handle_next();
}

auto cloud_edge_end_default_decision_engine::on_es_handling_message(
    edge_device *es,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
    auto ipv4_remote = ns3::InetSocketAddress::ConvertFrom(remote_address).GetIpv4();
    message msg(packet);
    auto task_item = msg.get_task_element(); // task_element::from_msg_packet(packet);
    auto task_id = task_item.get_header("task_id");

    log::info("edge server({:ip}) has received a task({}).", es->get_address(), task_id);

    auto es_resource = es->get_resource();
    auto cpu_supply = std::stod(es_resource->get_value("cpu"));
    auto cpu_demand = std::stod(task_item.get_header("cpu"));
    auto uncertain_cpu_supply = std::stod(msg.get_value("cpu_supply"));

    // 存在冲突，需要重新决策
    if (uncertain_cpu_supply != cpu_supply || cpu_supply < cpu_demand) {
        // 需要重新分配
        log::error("Conflict! cpu_demand: {}, cpu_supply: {}, real_supply: {}.", cpu_demand, uncertain_cpu_supply, cpu_supply);
        this->conflict(es, task_item, ipv4_remote, es->get_port());
        return;
    }

    // 更改CPU资源
    es_resource->reset_value("cpu", std::to_string(cpu_supply - cpu_demand));
    this->resource_changed(es, ipv4_remote, es->get_port());

    // 处理任务
    double processing_time = cpu_demand / cpu_supply; // 任务能分发过来，cpu_supply 就不可能为0

    log::info("edge server({:ip}) consumes resources: {} --> {}", es->get_address(), cpu_supply, cpu_supply - cpu_demand);
    log::info("task(id={}) demand: {}, supply: {}, processing_time: {}", task_id, cpu_demand, cpu_supply, processing_time);

    auto self = shared_from_base<this_type>();
    ns3::Simulator::Schedule(ns3::Seconds(processing_time), [self, es, ipv4_remote, task_id, processing_time, cpu_demand]() {
        // 处理完成，释放内存
        auto device_resource = es->get_resource();
        auto cur_cpu = std::stod(device_resource->get_value("cpu"));
        device_resource->reset_value("cpu", std::to_string(cur_cpu + cpu_demand));
        auto device_address = fmt::format("{:ip}", es->get_address());

        log::info("edge server({}) restores resources: {} --> {:.2f}(demand: {})", device_address, cur_cpu, cur_cpu + cpu_demand, cpu_demand);

        self->resource_changed(es, ipv4_remote, es->get_port());

        message response {
            { "msgtype", "response" },
            { "task_id", task_id },
            { "device_type", "es" },
            { "device_address", device_address },
            { "processing_time", fmt::format("{:.9f}", processing_time) }
        };
        es->write(response.to_packet(), ipv4_remote, es->get_port());
    });
}

auto cloud_edge_end_default_decision_engine::on_clients_reponse_message(
    client_device *client,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
    message msg(packet);

    auto it = client->response_cache().find_if([&msg](const response::value_type& item) {
        return item["group"] == msg.get_value("group") && item["task_id"] == msg.get_value("task_id");
    });
    if (it != client->response_cache().end()) {
        (*it)["device_type"] = msg.get_value("device_type");
        (*it)["device_address"] = msg.get_value("device_address");
        (*it)["time_consuming"] = msg.get_value("processing_time");
        (*it)["finished"] = msg.get_value("device_type") != "null" ? "Y" : "N";

        log::success("client({:ip}) has received a response for task(id={}).", client->get_address(), msg.get_value("task_id"));
    }

    // 检查是否存在当前任务的信息
    auto exist = client->response_cache().find_if([&msg](const auto& item) {
        return item["group"] == msg.get_value("group");
    });
    if (exist == client->response_cache().end()) {
        log::error("Fatal error! Invalid response."); // 说明发出去的数据被修改，或是 m_response 被无意间删除了信息
        return;
    }

    // 全部完成
    auto unfinished = client->response_cache().find_if([&msg](const auto& item) {
        return item["group"] == msg.get_value("group") && item["finished"] == "0";
    });
    if (unfinished == client->response_cache().end()) {
        client->when_done(client->response_cache().dump_with({ "group", msg.get_value("group") }));
    }
}

auto cloud_edge_end_default_decision_engine::on_cloud_handling_message(
    cloud_server *cs,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
    log::warning("cloud handling");
    auto ipv4_remote = ns3::InetSocketAddress::ConvertFrom(remote_address).GetIpv4();
    message msg(packet);
    auto task_item = msg.get_task_element(); // task_element::from_msg_packet(packet);
    auto task_id = task_item.get_header("task_id");

    auto cs_resource = cs->get_resource();
    auto cpu_supply = std::stod(cs_resource->get_value("cpu"));
    auto cpu_demand = std::stod(task_item.get_header("cpu"));

    NS_ASSERT_MSG(cpu_supply > 0, "cloud cpu cupply is not greater than 0");

    // 处理任务
    double processing_time = cpu_demand / cpu_supply;
    auto self = shared_from_base<this_type>();
    ns3::Simulator::Schedule(ns3::Seconds(processing_time), [self, cs, ipv4_remote, task_id, processing_time, cpu_demand]() {
        // 处理完成，释放内存
        auto device_address = fmt::format("{:ip}", cs->get_address());

        message response {
            { "msgtype", "response" },
            { "task_id", task_id },
            { "device_type", "cs" },
            { "device_address", device_address },
            { "processing_time", fmt::format("{:.9f}", processing_time) }
        };
        cs->write(response.to_packet(), ipv4_remote, cs->get_port());
    });
}

} // namespace okec
