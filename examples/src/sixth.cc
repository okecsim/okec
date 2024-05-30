#include <okec/okec.hpp>

namespace olog = okec::log;


void generate_task(okec::task& t, int number, std::string const& group)
{
    for (auto i = number; i-- > 0;)
    {
        t.emplace_back({
            { "task_id", okec::task::unique_id() },
            { "group", group },
            { "size",  okec::rand_range(2, 5).to_string() },
            { "cpu", okec::rand_range(0.5, 1.5).to_string() },
            { "deadline", okec::rand_range(2.0, 2.5).to_string() },
        });
    }
}

okec::awaitable offloading(auto user, okec::task t) {
    olog::debug("offloading begin");

    co_await user->async_send(std::move(t));
    auto resp = co_await user->async_read();
    olog::success("received response.");

    okec::print("{:r}", resp);
}

int main()
{
    olog::set_level(olog::level::all);

    okec::simulator sim;

    // Create 1 base station
    okec::base_station_container base_stations(sim, 1);
    // Create 5 edge servers
    okec::edge_device_container edge_servers(sim, 5);
    // Create 2 user devices
    okec::client_device_container user_devices(sim, 2);
    // Create a cloud
    okec::cloud_server cloud(sim);

    // std::vector<okec::client_device_container> client_groups;
    // client_groups.push_back(std::move(user_devices));

    // Connect the bs and edge servers
    base_stations.connect_device(edge_servers);

    // Set the network model for every device
    okec::cloud_edge_end_model model;
    okec::network_initializer(model, user_devices, base_stations.get(0), cloud);

    // Initialize the resources for each edge server.
    okec::resource_container resources(edge_servers.size());
    resources.initialize([](auto res) {
        res->attribute("cpu", okec::rand_range(2.1, 2.2).to_string());
    });

    // Install each resource on each edge server.
    edge_servers.install_resources(resources);

    // Install resource on cloud server
    auto cloud_res = okec::make_resource();
    cloud_res->attribute("cpu", "50");
    cloud.install_resource(cloud_res);

    // Specify the default offloading strategy
    auto decision_engine = std::make_shared<okec::cloud_edge_end_default_decision_engine>(&user_devices, &base_stations, &cloud);
    decision_engine->initialize();


    okec::task t;
    generate_task(t, 10, "dummy");

    auto user1 = user_devices.get_device(0);
    co_spawn(sim, offloading(user1, t));

    sim.run();
}