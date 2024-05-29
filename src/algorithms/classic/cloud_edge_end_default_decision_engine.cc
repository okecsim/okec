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

    okec::print("cache: \n{}\n", this->cache().dump(4));

}

auto cloud_edge_end_default_decision_engine::make_decision(
    const task_element &header) -> result_t
{
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
    return false;
}

auto cloud_edge_end_default_decision_engine::initialize() ->void
{
}

auto cloud_edge_end_default_decision_engine::handle_next() -> void
{
}

auto cloud_edge_end_default_decision_engine::on_bs_decision_message(
    base_station *bs,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
}

auto cloud_edge_end_default_decision_engine::on_bs_response_message(
    base_station *bs,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
}

auto cloud_edge_end_default_decision_engine::on_es_handling_message(
    edge_device *es,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
}

auto cloud_edge_end_default_decision_engine::on_clients_reponse_message(
    client_device *client,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
}

auto cloud_edge_end_default_decision_engine::on_cloud_handling_message(
    cloud_server *es,
    ns3::Ptr<ns3::Packet> packet,
    const ns3::Address &remote_address) -> void
{
}

} // namespace okec
